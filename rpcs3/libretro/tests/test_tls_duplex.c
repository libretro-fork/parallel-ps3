/* The wolfSSL replacement in rpcn_client.cpp: libretro-common's TLS client
 * in duplex mode, driven the way the RPCN client drives it - the
 * handshake stepped with poll() and a deadline, a writer thread sending
 * packets (send, then flush, poll() for POLLOUT when the socket is full)
 * while the reader thread receives (poll() for POLLIN only when nothing is
 * pending) - against an openssl s_server -rev, which sends each line back
 * reversed. Lines of up to 6000 bytes, so many span several records and
 * the socket fills both ways at once. Run once under AddressSanitizer and
 * once under ThreadSanitizer.
 *
 * Usage: test_tls_duplex host port lines */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <net/net_socket_ssl.h>

#define MAX_LINE 6000

static void *tls;
static int   sockfd;
static int   nlines;
static int   send_failed;

static double now_s(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec + t.tv_nsec * 1e-9;
}

/* Line i: its length and its bytes, the same on both sides */
static size_t line_len(int i)
{
   return 1 + (size_t)((i * 2654435761u) % MAX_LINE);
}

static char line_byte(int i, size_t k)
{
   return (char)('a' + (char)((i * 7 + (int)k * 13) % 26));
}

/* As rpcn_client::send_packet: send what the socket takes, flush once all
 * of it is taken, wait for POLLOUT when the socket is full */
static int send_packet(const char *data, size_t len)
{
   size_t n_sent   = 0;
   double deadline = now_s() + 10.0;
   for (;;)
   {
      struct pollfd p;
      if (n_sent != len)
      {
         ssize_t res = ssl_socket_send_all_nonblocking(tls, data + n_sent, len - n_sent, true);
         if (res < 0)
            return -1;
         n_sent += (size_t)res;
      }
      if (n_sent == len)
      {
         int flushed = ssl_socket_flush_nonblocking(tls);
         if (flushed < 0)
            return -1;
         if (flushed > 0)
            return 0;
      }
      if (now_s() >= deadline)
         return -1;
      p.fd      = sockfd;
      p.events  = POLLOUT;
      p.revents = 0;
      poll(&p, 1, 100);
   }
}

static void *writer(void *arg)
{
   static char buf[MAX_LINE + 1];
   int i;
   (void)arg;
   for (i = 0; i < nlines; i++)
   {
      size_t n = line_len(i), k;
      for (k = 0; k < n; k++)
         buf[k] = line_byte(i, k);
      buf[n] = '\n';
      if (send_packet(buf, n + 1) != 0)
      {
         send_failed = 1;
         break;
      }
   }
   return NULL;
}

/* As rpcn_client::recvn: receive, and when nothing came and nothing is
 * pending, wait for POLLIN */
static int recvn(char *buf, size_t n)
{
   size_t n_recv   = 0;
   double deadline = now_s() + 10.0;
   while (n_recv != n)
   {
      bool    error = false;
      ssize_t res   = ssl_socket_receive_all_nonblocking(tls, &error, buf + n_recv, n - n_recv);
      if (res < 0 || error)
         return -1;
      if (res == 0)
      {
         bool          write = false;
         struct pollfd p;
         if (ssl_socket_retro_pending(tls, &write))
            continue;
         if (now_s() >= deadline)
            return -1;
         p.fd      = sockfd;
         p.events  = POLLIN;
         p.revents = 0;
         poll(&p, 1, 100);
         continue;
      }
      n_recv += (size_t)res;
   }
   return 0;
}

int main(int argc, char **argv)
{
   static char buf[MAX_LINE + 1];
   struct ssl_socket_retro_config cfg;
   struct addrinfo hints, *ai = NULL;
   pthread_t t;
   double    deadline;
   int       i, bad = 0;

   if (argc < 4)
      return 2;
   nlines = atoi(argv[3]);

   memset(&hints, 0, sizeof(hints));
   hints.ai_family   = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   if (getaddrinfo(argv[1], argv[2], &hints, &ai) || !ai)
      return 1;
   sockfd = socket(AF_INET, SOCK_STREAM, 0);
   if (sockfd < 0 || connect(sockfd, ai->ai_addr, ai->ai_addrlen) != 0)
   {
      fprintf(stderr, "test_tls_duplex: no server at %s:%s\n", argv[1], argv[2]);
      return 1;
   }
   freeaddrinfo(ai);
   fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

   /* As rpcn_client::connect */
   tls = ssl_socket_init(sockfd, argv[1]);
   memset(&cfg, 0, sizeof(cfg));
   cfg.flags = SSL_RETRO_NO_VERIFY | SSL_RETRO_DUPLEX;
   if (!tls || ssl_socket_retro_configure(tls, &cfg) != 0)
      return 1;
   deadline = now_s() + 10.0;
   for (;;)
   {
      bool want_read = false, want_write = false;
      int  r = ssl_socket_handshake_step(tls, &want_read, &want_write);
      if (r > 0)
         break;
      if (r < 0 || now_s() >= deadline)
      {
         fprintf(stderr, "test_tls_duplex: handshake failed (%d)\n", ssl_socket_last_error(tls));
         return 1;
      }
      if (want_read || want_write)
      {
         struct pollfd p;
         p.fd      = sockfd;
         p.events  = (short)((want_read ? POLLIN : 0) | (want_write ? POLLOUT : 0));
         p.revents = 0;
         poll(&p, 1, 100);
      }
   }

   pthread_create(&t, NULL, writer, NULL);
   for (i = 0; i < nlines; i++)
   {
      size_t n = line_len(i), k;
      if (recvn(buf, n + 1) != 0)
      {
         fprintf(stderr, "test_tls_duplex: line %d not received (%d)\n", i, ssl_socket_last_error(tls));
         bad++;
         break;
      }
      for (k = 0; k < n; k++)
         if (buf[k] != line_byte(i, n - 1 - k))
            break;
      if (k != n || buf[n] != '\n')
      {
         fprintf(stderr, "test_tls_duplex: line %d came back wrong\n", i);
         bad++;
         break;
      }
   }
   pthread_join(t, NULL);
   if (send_failed)
   {
      fprintf(stderr, "test_tls_duplex: sending failed (%d)\n", ssl_socket_last_error(tls));
      bad++;
   }
   if (!bad)
      printf("test_tls_duplex: ok, %d lines sent and read back at once over TLS %s\n", nlines,
            ssl_socket_retro_version(tls) == 0x0304 ? "1.3" : "1.2");
   ssl_socket_close(tls);
   ssl_socket_free(tls);
   return bad ? 1 : 0;
}
