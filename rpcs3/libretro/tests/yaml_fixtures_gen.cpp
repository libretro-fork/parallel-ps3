// Makes test_yaml's fixtures with yaml-cpp, the library the core used before
// (https://github.com/RPCS3/yaml-cpp at 51a5d623e3fde1f58829a56ba910f1cb33596222):
// fixtures/yaml_trees.txt, each tree's canonical form hashed, and the
// documents yaml-cpp's emitter writes. Not built by the Makefile; run by hand
// when the fixtures change:
//   g++ -std=c++17 -I<yaml-cpp>/include yaml_fixtures_gen.cpp <libyaml-cpp.a> -o gen
//   ./gen patch_excerpt.yml yaml_cpp_docs.yml fixtures/yaml_trees.txt
// patch_excerpt.yml is lines 1-3568 and 38185-39046 of RPCS3's patch.yml
// (patch API v1.2, as of November 2026), gzipped into fixtures/.
#include <yaml-cpp/yaml.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <fstream>
#include <sstream>
static void canon(const YAML::Node& n, std::string& out)
{
	switch (n.Type())
	{
	case YAML::NodeType::Scalar: out += "S" + std::to_string(n.Scalar().size()) + ":" + n.Scalar(); break;
	case YAML::NodeType::Sequence: out += "["; for (auto it : n) canon(it, out); out += "]"; break;
	case YAML::NodeType::Map: out += "{"; for (auto it : n) { out += "K" + std::to_string(it.first.Scalar().size()) + ":" + it.first.Scalar(); canon(it.second, out); } out += "}"; break;
	default: out += "N"; break;
	}
}
static uint64_t fnv(const std::string& s) { uint64_t h = 1469598103934665603ull; for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; } return h; }
static unsigned st = 1;
static unsigned rnd(unsigned n) { st = st * 1103515245u + 12345u; return (st >> 8) % n; }
static const char tricky[] = "-?:,[]{}#&*!|>'\"%@` \t\\\n\r";
static std::string text()
{
	std::string s; unsigned n = rnd(10);
	for (unsigned i = 0; i < n; i++) { unsigned r = rnd(10); if (r < 3) s += tricky[rnd(sizeof(tricky) - 1)]; else if (r == 3) { s += (char)0xc3; s += (char)0xa9; } else s += (char)('a' + rnd(26)); }
	return s;
}
static void emit(YAML::Emitter& out, int depth)
{
	unsigned r = depth > 3 ? 0 : rnd(4);
	if (r == 0) { out << text(); return; }
	if (r == 1) { out << YAML::BeginMap; unsigned n = rnd(4); for (unsigned i = 0; i < n; i++) { out << YAML::Key << (text() + (char)('A' + i)) << YAML::Value; emit(out, depth + 1); } out << YAML::EndMap; return; }
	if (r == 2) { out << YAML::BeginSeq; unsigned n = rnd(4); for (unsigned i = 0; i < n; i++) emit(out, depth + 1); out << YAML::EndSeq; return; }
	out << YAML::Flow << YAML::BeginSeq; unsigned n = rnd(4); for (unsigned i = 0; i < n; i++) out << text(); out << YAML::EndSeq;
}
int main(int argc, char** argv)
{
	std::ifstream in(argv[1], std::ios::binary); std::stringstream ss; ss << in.rdbuf();
	const YAML::Node patch = YAML::Load(ss.str());
	FILE* t = fopen(argv[3], "w");
	unsigned i = 0;
	// one line per entry of each top-level mapping (the top-level scalar
	// Version on its own), in document order
	for (auto it : patch)
	{
		if (!it.second.IsMap()) { std::string c = "K" + std::to_string(it.first.Scalar().size()) + ":" + it.first.Scalar(); canon(it.second, c); fprintf(t, "patch %u %016llx\n", i++, (unsigned long long)fnv(c)); continue; }
		for (auto e : it.second) { std::string c = "K" + std::to_string(e.first.Scalar().size()) + ":" + e.first.Scalar(); canon(e.second, c); fprintf(t, "patch %u %016llx\n", i++, (unsigned long long)fnv(c)); }
	}
	FILE* d = fopen(argv[2], "wb");
	for (int k = 0; k < 300; k++)
	{
		st = k * 2654435761u + 7;
		YAML::Emitter out; out << YAML::BeginMap; out << YAML::Key << "root" << YAML::Value; emit(out, 0); out << YAML::EndMap;
		std::string doc = out.c_str();
		std::string c; canon(YAML::Load(doc), c);
		fprintf(d, "%s\n...\n", doc.c_str());
		fprintf(t, "doc %d %016llx\n", k, (unsigned long long)fnv(c));
	}
	fclose(d); fclose(t);
}
