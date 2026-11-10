// The protobuf replacement: RPCN's messages as the core reads and writes
// them (rpcs3/Emu/NP/generated/np2_structs_gen.py, over Emu/NP/pb_wire.h),
// against what the protobuf library made of the same bytes
// (fixtures/np2_messages.bin.gz, from np2_fixtures_gen.py):
//
// - messages protobuf wrote: read to the same fields, and written again
//   byte for byte the same, so the RPCN server gets what it got before;
// - the same read in ways protobuf accepts but does not write (fields out
//   of order or given twice, numbers not packed, unknown fields and groups,
//   a known field of the wrong wire type): read to the same fields, and
//   written as protobuf writes what it read;
// - broken ones (cut short, bytes changed, strings that are not UTF-8):
//   refused where protobuf refuses them, read the same where it does not.
//
// Usage: test_np2 fixtures-dir

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <encodings/deflate.h>

#include "np2_dump.h"

static std::uint64_t fnv(std::string_view s)
{
	std::uint64_t h = 1469598103934665603ull;
	for (const unsigned char c : s)
	{
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

static bool gunzip(const char* path, std::vector<unsigned char>& out)
{
	FILE* f = std::fopen(path, "rb");
	if (!f)
		return false;
	std::vector<unsigned char> in;
	unsigned char buf[65536];
	std::size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
		in.insert(in.end(), buf, buf + n);
	std::fclose(f);

	out.resize(64u << 20);
	std::size_t taken = 0, wrote = 0;
	void* z = rinflate_new(16 + 15);
	rinflate_set_in(z, in.data(), in.size());
	rinflate_set_out(z, out.data(), out.size());
	const int r = rinflate_process(z, &taken, &wrote);
	rinflate_free(z);
	out.resize(wrote);
	return r == RDEFLATE_PROCESS_END;
}

int main(int argc, char** argv)
{
	if (argc < 2)
		return 2;

	std::vector<unsigned char> data;
	const std::string path = std::string(argv[1]) + "/np2_messages.bin.gz";
	if (!gunzip(path.c_str(), data))
	{
		std::fprintf(stderr, "test_np2: cannot read %s\n", path.c_str());
		return 1;
	}

	static const char* const kinds[] = { "written by protobuf", "read differently", "broken" };
	int count[3] = {}, bad[3] = {}, refused[3] = {};
	std::size_t i = 0;

	while (i + 6 <= data.size())
	{
		const int kind = data[i];
		const std::size_t name_len = data[i + 1];
		const std::string name(reinterpret_cast<const char*>(&data[i + 2]), name_len);
		i += 2 + name_len;
		std::uint32_t len;
		std::memcpy(&len, &data[i], 4);
		i += 4;
		const unsigned char* bytes = &data[i];
		i += len;
		const bool pb_ok = data[i++] != 0;
		std::uint64_t pb_text = 0, pb_again = 0;
		if (pb_ok)
		{
			std::memcpy(&pb_text, &data[i], 8);
			std::memcpy(&pb_again, &data[i + 8], 8);
			i += 16;
		}

		// The bytes on their own, so a read past them is caught
		std::vector<unsigned char> own(bytes, bytes + len);
		std::string text, again;
		const bool ok = np2_dump::by_name(name, own.data(), own.size(), text, again);

		count[kind]++;
		if (!pb_ok)
			refused[kind]++;

		bool same = ok == pb_ok;
		if (same && ok)
		{
			same = fnv(text) == pb_text && fnv(again) == pb_again;
			if (kind == 0)
				same = same && again.size() == len && std::memcmp(again.data(), bytes, len) == 0;
		}
		if (!same)
		{
			if (bad[kind]++ < 5)
			{
				std::fprintf(stderr, "test_np2: %s (%s), %u bytes: protobuf %s it, the core %s it%s\n  ", name.c_str(), kinds[kind],
					len, pb_ok ? "reads" : "refuses", ok ? "reads" : "refuses", ok && pb_ok ? ", differently" : "");
				for (std::uint32_t k = 0; k < len && k < 64; k++)
					std::fprintf(stderr, "%02x", bytes[k]);
				std::fprintf(stderr, "\n  %s\n", text.c_str());
			}
		}
	}

	int failures = 0;
	for (int k = 0; k < 3; k++)
	{
		std::printf("test_np2: %d messages %s, %d of them refused by protobuf: %s\n", count[k], kinds[k], refused[k],
			bad[k] ? "DIFFERENT" : "the same");
		failures += bad[k];
	}
	if (i != data.size() || !count[0])
	{
		std::fprintf(stderr, "test_np2: the fixtures did not read to their end\n");
		failures++;
	}
	if (failures)
	{
		std::fprintf(stderr, "test_np2: %d differences\n", failures);
		return 1;
	}
	std::printf("test_np2: ok\n");
	return 0;
}
