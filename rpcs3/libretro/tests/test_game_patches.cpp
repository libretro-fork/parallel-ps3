// RPCS3's game patches as the core carries them (patch.yml, embedded by
// make/embed.c and written to patches/ by install_patches), read by RPCS3's
// patch engine on util/yaml.cpp, the yaml-cpp replacement:
//
// - the file reads without an error, and every executable (a top-level key)
//   and every patch under one that a plain scan of the text finds is there;
// - games the patches name get their patches as core options
//   (libretro_patches::collect, the Patch Manager category): every 25th of
//   them, as each collect reads the whole file again (a tenth of a second,
//   once a game starts), and one that none name gets none.
//
// A build without the patches (EMBED_PATCHES=0, or nothing downloaded) has
// nothing to check and says so. Built by `make check` from the core's own
// objects and archives, the same link as the core's with this main.

#include "stdafx.h"
#include "Utilities/bin_patch.h"
#include "libretro_patch_manager.h"

#include <cstdio>
#include <set>
#include <sstream>

extern const unsigned char g_libretro_patch_yml[];
extern const std::size_t g_libretro_patch_yml_size;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// The top-level keys and the patches under them, from the text alone: a key
// at the start of a line, and a quoted or plain key two spaces in under it
static void scan(const std::string& text, std::set<std::string>& hashes, std::set<std::pair<std::string, std::string>>& patches)
{
	std::istringstream lines(text);
	std::string top;
	for (std::string line; std::getline(lines, line);)
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		if (line[0] != ' ' && line[0] != '\t')
		{
			top = line.substr(0, line.find(':'));
			if (top != "Version" && top != "Anchors")
				hashes.insert(top);
			continue;
		}
		if (top == "Version" || top == "Anchors" || line.size() < 4 || line.compare(0, 2, "  ") != 0)
			continue;
		if (line[2] == ' ' || line[2] == '\t' || line[2] == '#' || line[2] == '-')
			continue;
		std::string key;
		if (line[2] == '"')
		{
			const usz end = line.find("\":", 3);
			if (end == umax)
				continue;
			key = line.substr(3, end - 3);
		}
		else
		{
			const usz end = line.find(':', 2);
			if (end == umax)
				continue;
			key = line.substr(2, end - 2);
		}
		patches.emplace(top, key);
	}
}

int main()
{
	if (!g_libretro_patch_yml_size)
	{
		std::printf("test_game_patches: built without patch.yml, nothing to check\n");
		return 0;
	}

	const std::string text(reinterpret_cast<const char*>(g_libretro_patch_yml), g_libretro_patch_yml_size);
	const std::string dir = patch_engine::get_patches_path();
	fs::create_path(dir);
	CHECK(fs::write_file(dir + "patch.yml", fs::rewrite, text));

	patch_engine::patch_map map;
	std::stringstream log;
	CHECK(patch_engine::load(map, dir + "patch.yml", "", false, &log));
	CHECK(log.str().empty());
	if (!log.str().empty())
		std::fprintf(stderr, "%s\n", log.str().substr(0, 2000).c_str());

	std::set<std::string> hashes;
	std::set<std::pair<std::string, std::string>> patches;
	scan(text, hashes, patches);

	usz missing = 0;
	for (const std::string& hash : hashes)
	{
		if (!map.contains(hash) && missing++ < 5)
			std::fprintf(stderr, "executable %s not read\n", hash.c_str());
	}
	for (const auto& [hash, description] : patches)
	{
		const auto found = map.find(hash);
		if ((found == map.end() || !found->second.patch_info_map.contains(description)) && missing++ < 10)
			std::fprintf(stderr, "patch \"%s\" of %s not read\n", description.c_str(), hash.c_str());
	}
	CHECK(missing == 0);
	CHECK(hashes.size() > 500);
	CHECK(patches.size() > 1000);

	// Every game a patch names, and its options
	std::set<std::string> serials;
	for (const auto& [hash, container] : map)
		for (const auto& [description, info] : container.patch_info_map)
			for (const auto& [title, title_serials] : info.titles)
				for (const auto& [serial, versions] : title_serials)
					if (serial != patch_key::all)
						serials.insert(serial);

	usz without = 0, tried = 0, nth = 0;
	for (const std::string& serial : serials)
	{
		if (nth++ % 25)
			continue;
		tried++;
		libretro_patches::collect(serial);
		if ((!libretro_patches::any() || libretro_patches::definitions().empty()) && without++ < 5)
			std::fprintf(stderr, "%s has no patch options\n", serial.c_str());
	}
	CHECK(without == 0);
	CHECK(serials.size() > 500);

	libretro_patches::collect("XXXX00000");
	CHECK(!libretro_patches::any());

	std::printf("test_game_patches: %zu executables, %zu patches; %zu of %zu games tried have their options\n",
		map.size(), patches.size(), tried - without, tried);
	return failures ? 1 : 0;
}
