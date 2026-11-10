/* The pugixml replacement: the XML the core reads, parsed by rxml as
 * Utilities/rXml.cpp and rpcs3/Emu/NP/clans_client.cpp read it, and a
 * clans request written by rxml's writer and read back.
 *
 * - TROPCONF.SFM as games ship it: a UTF-8 BOM, the XML declaration,
 *   non-ASCII names, entities, attributes, nested <trophy> children
 * - a redump .dat (content_validation.cpp): <datafile> with <game>/<rom>
 * - a clans server response, and a request with text that needs escaping */

#include <formats/rxml.h>

#include "test_common.h"

static const char tropconf[] =
   "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
   "<!--Trophy configuration-->\n"
   "<trophyconf version=\"1.0\">\n"
   "  <npcommid>NPWR00000_00</npcommid>\n"
   "  <title-name>Caf\xC3\xA9 &amp; Bar</title-name>\n"
   "  <title-detail>Line one&#10;Line two</title-detail>\n"
   "  <trophy id=\"000\" hidden=\"no\" ttype=\"P\" pid=\"-1\">\n"
   "    <name>Platinum</name>\n"
   "    <detail><![CDATA[All <the> trophies]]></detail>\n"
   "  </trophy>\n"
   "  <trophy id=\"001\" hidden=\"yes\" ttype=\"B\" pid=\"000\">\n"
   "    <name>\xE6\x97\xA5\xE6\x9C\xAC</name>\n"
   "    <detail>Secret</detail>\n"
   "  </trophy>\n"
   "</trophyconf>\n";

static const char datfile[] =
   "<?xml version=\"1.0\"?>\n"
   "<!DOCTYPE datafile PUBLIC \"-//Logiqx//DTD ROM Management Datafile//EN\" \"http://www.logiqx.com/Dats/datafile.dtd\">\n"
   "<datafile>\n"
   "\t<header><name>Sony - PlayStation 3</name></header>\n"
   "\t<game name=\"Some Game (Europe)\">\n"
   "\t\t<rom name=\"Some Game (Europe).iso\" size=\"123\" md5=\"0123456789abcdef0123456789abcdef\"/>\n"
   "\t</game>\n"
   "</datafile>\n";

static const char clans_response[] =
   "<clan result=\"00\"><list results=\"2\" total=\"0x10\">"
   "<info id=\"7\"><name>A &lt;B&gt;</name><tag>AB</tag><members>3</members></info>"
   "<msg-info id=\"1\"/>"
   "<info id=\"8\"><name>C</name></info>"
   "</list></clan>";

static rxml_node_t *child(rxml_node_t *n, const char *name)
{
   for (n = n ? n->children : NULL; n; n = n->next)
      if (n->name && !strcmp(n->name, name))
         return n;
   return NULL;
}

static rxml_node_t *next_named(rxml_node_t *n, const char *name)
{
   for (n = n ? n->next : NULL; n; n = n->next)
      if (n->name && !strcmp(n->name, name))
         return n;
   return NULL;
}

static int str_is(const char *s, const char *want)
{
   return s && !strcmp(s, want);
}

int main(void)
{
   rxml_document_t *doc;
   rxml_node_t     *root, *n;

   /* TROPCONF.SFM */
   doc  = rxml_load_document_string(tropconf);
   root = rxml_root_node(doc);
   TEST_CHECK(root && str_is(root->name, "trophyconf"));
   if (root)
   {
      TEST_CHECK(str_is(child(root, "title-name")->data, "Caf\xC3\xA9 & Bar"));
      TEST_CHECK(str_is(child(root, "title-detail")->data, "Line one\nLine two"));
      n = child(root, "trophy");
      TEST_CHECK(n && str_is(rxml_node_attrib(n, "ttype"), "P")
            && str_is(rxml_node_attrib(n, "hidden"), "no"));
      TEST_CHECK(str_is(child(n, "detail")->data, "All <the> trophies"));
      n = next_named(n, "trophy");
      TEST_CHECK(n && str_is(rxml_node_attrib(n, "id"), "001")
            && str_is(rxml_node_attrib(n, "pid"), "000"));
      TEST_CHECK(str_is(child(n, "name")->data, "\xE6\x97\xA5\xE6\x9C\xAC"));
      TEST_CHECK(!next_named(n, "trophy"));
   }
   rxml_free_document(doc);

   /* redump .dat */
   doc  = rxml_load_document_string(datfile);
   root = rxml_root_node(doc);
   TEST_CHECK(root && str_is(root->name, "datafile"));
   n = child(child(root, "game"), "rom");
   TEST_CHECK(n && str_is(rxml_node_attrib(n, "md5"), "0123456789abcdef0123456789abcdef"));
   TEST_CHECK(str_is(rxml_node_attrib(child(root, "game"), "name"), "Some Game (Europe)"));
   rxml_free_document(doc);

   /* a clans response */
   doc  = rxml_load_document_string_opts(clans_response, RXML_OPT_STRICT_EOF, NULL);
   root = rxml_root_node(doc);
   TEST_CHECK(root && str_is(rxml_node_attrib(root, "result"), "00"));
   n = child(child(root, "list"), "info");
   TEST_CHECK(n && str_is(child(n, "name")->data, "A <B>"));
   n = next_named(n, "info");
   TEST_CHECK(n && str_is(rxml_node_attrib(n, "id"), "8") && !child(n, "tag"));
   rxml_free_document(doc);

   /* a truncated response is refused */
   TEST_CHECK(!rxml_load_document_string_opts("<clan result=\"00\"><list>",
            RXML_OPT_STRICT_EOF, NULL));

   /* a clans request, written and read back */
   {
      rxml_writer_t *w = rxml_writer_new(NULL);
      size_t len = 0;
      const char *out;
      rxml_write_open(w, "clan");
      rxml_write_element(w, "ticket", "abc+/=");
      rxml_write_element(w, "description", "<b>&\"quoted\"</b>");
      rxml_write_open(w, "filter");
      rxml_write_open(w, "name");
      rxml_write_attrib(w, "op", "lk");
      rxml_write_attrib(w, "value", "a\"b<c>&");
      rxml_write_close(w);
      rxml_write_close(w);
      rxml_write_element(w, "bin-attr1", "");
      rxml_write_close(w);
      out = rxml_writer_data(w, &len);
      TEST_CHECK(out && len == strlen(out));
      doc  = out ? rxml_load_document_string(out) : NULL;
      root = rxml_root_node(doc);
      TEST_CHECK(root && str_is(child(root, "ticket")->data, "abc+/="));
      TEST_CHECK(root && str_is(child(root, "description")->data, "<b>&\"quoted\"</b>"));
      TEST_CHECK(root && str_is(rxml_node_attrib(child(child(root, "filter"), "name"), "value"), "a\"b<c>&"));
      TEST_CHECK(root && child(root, "bin-attr1") && !child(root, "bin-attr1")->data);
      rxml_free_document(doc);
      rxml_writer_free(w);
   }

   if (test_failures)
   {
      fprintf(stderr, "test_rxml: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_rxml: ok\n");
   return 0;
}
