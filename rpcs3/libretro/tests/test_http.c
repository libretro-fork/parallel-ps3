/* The curl replacement: libretro-common's net_http, built as the core
 * builds it (with its own TLS client), making the clans server's request
 * the way clans_client.cpp makes it - a POST of an XML body, the answer
 * taken whatever its status - against a small server on this machine:
 * the request line, the body and its type as the server sees them, and
 * the answer as the client gets it back. Then an https:// request to a
 * port nothing listens on fails, as it should, inside its deadline.
 *
 * Usage: test_http */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <net/net_http.h>
#include <net/net_socket_ssl.h>

static int fails;

#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__); fails++; } } while (0)

struct server
{
   int         fd;
   int         port;
   int         status;    /* to answer with */
   const char *answer;
   char        request[8192];
   size_t      request_len;
};

/* One connection: the request read whole (headers and the body its
 * Content-Length says), then the answer */
static void *serve(void *arg)
{
   struct server *s = (struct server*)arg;
   int    c = accept(s->fd, NULL, NULL);
   size_t need = 0;
   char   reply[1024];

   if (c < 0)
      return NULL;
   for (;;)
   {
      ssize_t n = recv(c, s->request + s->request_len,
            sizeof(s->request) - 1 - s->request_len, 0);
      char   *end;
      if (n <= 0)
         break;
      s->request_len += (size_t)n;
      s->request[s->request_len] = '\0';
      if (!need && (end = strstr(s->request, "\r\n\r\n")))
      {
         const char *cl = strstr(s->request, "Content-Length: ");
         need = (size_t)(end + 4 - s->request) + (cl ? (size_t)atoi(cl + 16) : 0);
      }
      if (need && s->request_len >= need)
         break;
   }
   snprintf(reply, sizeof(reply),
         "HTTP/1.1 %d X\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
         s->status, (unsigned)strlen(s->answer), s->answer);
   send(c, reply, strlen(reply), 0);
   close(c);
   return NULL;
}

static int listen_here(int *port)
{
   struct sockaddr_in a;
   socklen_t          len = sizeof(a);
   int                fd  = socket(AF_INET, SOCK_STREAM, 0);

   memset(&a, 0, sizeof(a));
   a.sin_family      = AF_INET;
   a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   if (fd < 0 || bind(fd, (struct sockaddr*)&a, sizeof(a)) || listen(fd, 1)
         || getsockname(fd, (struct sockaddr*)&a, &len))
      return -1;
   *port = ntohs(a.sin_port);
   return fd;
}

static double now_s(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec + t.tv_nsec * 1e-9;
}

/* As clans_client.cpp sends: the body, its form type, TLS settings for
 * https (no certificate checked), a deadline; the answer's body, NULL
 * on failure */
static char *post(const char *url, const char *xml, int timeout_ms, int *status)
{
   struct http_connection_t *conn = net_http_connection_new(url, "POST", NULL);
   struct ssl_socket_retro_config tls;
   struct http_t *http;
   char   *out = NULL;
   double  deadline = now_s() + timeout_ms / 1000.0;

   *status = 0;
   if (!conn)
      return NULL;
   net_http_connection_set_content(conn, "application/x-www-form-urlencoded",
         strlen(xml), xml);
   memset(&tls, 0, sizeof(tls));
   tls.flags = SSL_RETRO_NO_VERIFY;
   if (!strncmp(url, "https:", 6))
      net_http_connection_set_tls(conn, &tls);
   while (!net_http_connection_iterate(conn)) { }
   if (!net_http_connection_done(conn) || !(http = net_http_new(conn)))
   {
      net_http_connection_free(conn);
      return NULL;
   }
   while (!net_http_update(http, NULL, NULL))
   {
      if (now_s() >= deadline)
         goto done;
      net_http_wait(http, 100);
   }
   if (!net_http_failure(http, NULL))
   {
      size_t   len  = 0;
      uint8_t *data = net_http_data(http, &len, true);
      *status = net_http_status(http);
      if (data && (out = (char*)realloc(data, len + 1)))
         out[len] = '\0';
      else
         free(data);
   }
done:
   net_http_delete(http);
   net_http_connection_free(conn);
   return out;
}

static void exchange(int status)
{
   static const char xml[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<clan><ticket>dGlja2V0</ticket><start>1</start><max>10</max></clan>";
   static const char answer[] = "<clan result=\"00\"><list results=\"0\"/></clan>";
   struct server s;
   pthread_t     t;
   char          url[128], what[64];
   char         *got;
   int           got_status = 0;

   memset(&s, 0, sizeof(s));
   s.status = status;
   s.answer = answer;
   CHECK((s.fd = listen_here(&s.port)) >= 0, "a socket to listen on");
   if (s.fd < 0)
      return;
   pthread_create(&t, NULL, serve, &s);
   snprintf(url, sizeof(url),
         "http://127.0.0.1:%d/clan_manager_view/func/GetClanList", s.port);
   got = post(url, xml, 10000, &got_status);
   pthread_join(t, NULL);
   close(s.fd);

   sprintf(what, "status %d: the answer", status);
   CHECK(got && !strcmp(got, answer) && got_status == status, what);
   sprintf(what, "status %d: the request line", status);
   CHECK(!strncmp(s.request, "POST /clan_manager_view/func/GetClanList HTTP/1.1\r\n", 51), what);
   sprintf(what, "status %d: the body's type", status);
   CHECK(strstr(s.request, "Content-Type: application/x-www-form-urlencoded\r\n") != NULL, what);
   sprintf(what, "status %d: the body", status);
   CHECK(s.request_len >= sizeof(xml) - 1
         && !memcmp(s.request + s.request_len - (sizeof(xml) - 1), xml, sizeof(xml) - 1), what);
   free(got);
}

int main(void)
{
   net_http_init();

   exchange(200);
   /* the server's error page is the answer too, as curl handed it on */
   exchange(500);

   /* https to nothing: refused within the deadline, not hung */
   {
      int    port = 0, status = 0;
      int    fd   = listen_here(&port);
      char   url[96];
      double t0;
      char  *got;
      close(fd); /* now nothing listens there */
      snprintf(url, sizeof(url), "https://127.0.0.1:%d/clan_manager_view/func/GetClanList", port);
      t0  = now_s();
      got = post(url, "<clan/>", 5000, &status);
      CHECK(!got, "https to a closed port fails");
      CHECK(now_s() - t0 < 6.0, "https to a closed port fails within the deadline");
      free(got);
   }

   net_http_deinit();
   if (fails)
   {
      fprintf(stderr, "test_http: %d check(s) failed\n", fails);
      return 1;
   }
   printf("test_http: ok\n");
   return 0;
}
