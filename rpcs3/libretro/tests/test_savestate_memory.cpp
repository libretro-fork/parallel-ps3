// In-memory save states (rpcs3/Emu/savestate_utils.hpp): a state written as
// Emulator::Kill writes one when the core lends it g_memory_savestate - a
// zstd-compressed serialization into a memory stream over the core's
// buffer - and read back as booting memory_savestate_path reads it, through
// make_savestate_reader, from that buffer where it lies.
//
// Checked: the state is a zstd frame and reads back to what was written,
// values and bulk data both; a second save into the same buffer reuses its
// memory and reads back the same; without a buffer lent the path is not
// read at all.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main, and run there (the Makefile build
// workflow runs it).

#include "stdafx.h"
#include "Emu/savestate_utils.hpp"
#include "util/serialization_ext.hpp"
#include "Utilities/File.h"
#include "Utilities/Thread.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// What is written: a header as the emulator's, values of each size, a
// string, and bulk data, some of it compressible and some not
struct state
{
	std::vector<u8> bulk;
	std::string title;
	u64 values[64];

	explicit state(u32 seed)
	{
		u64 x = seed * 0x9E3779B97F4A7C15ull + 1;
		auto next = [&x]() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; };
		bulk.resize(6u << 20);
		for (usz i = 0; i < bulk.size(); i++)
			bulk[i] = i < bulk.size() / 2 ? static_cast<u8>(i / 4096) : static_cast<u8>(next());
		title = "BLUS30000 in-memory state " + std::to_string(seed);
		for (u64& v : values)
			v = next();
	}
};

static void write_state(std::vector<u8>& buffer, const state& s)
{
	// As Emulator::Kill does with g_memory_savestate
	buffer.clear();
	fs::file memory_file = fs::make_stream<std::vector<u8>&>(buffer);

	utils::serial ar;
	ar.m_file_handler = make_compressed_zstd_serialization_file_handler(memory_file);

	ar("RPCS3SAV"_u64);
	ar(std::endian::native == std::endian::little);
	ar(s.title);
	for (const u64 v : s.values)
		ar(v);
	ar(s.bulk);

	ar.seek_end();
	ar.m_file_handler->finalize(ar);
}

static bool read_state(const state& s)
{
	// As booting memory_savestate_path reads it
	const std::shared_ptr<utils::serial> ar = make_savestate_reader(memory_savestate_path);
	if (!ar)
	{
		std::fprintf(stderr, "make_savestate_reader gave no reader\n");
		return false;
	}

	// make_savestate_reader has looked at the magic, not taken it
	bool ok = ar->pop<u64>() == "RPCS3SAV"_u64;
	ok = ok && ar->pop<bool>() == (std::endian::native == std::endian::little);
	ok = ok && ar->pop<std::string>() == s.title;
	for (const u64 v : s.values)
		ok = ok && ar->pop<u64>() == v;
	ok = ok && ar->pop<std::vector<u8>>() == s.bulk;
	return ok;
}

// On an RPCS3 thread, as Emulator::Kill writes the state on its "Emu State
// Capture Thread": the zstd writer's finalize waits on it through thread_ctrl
static void run()
{
	std::vector<u8> buffer;
	const state first(1), second(2);

	write_state(buffer, first);
	CHECK(buffer.size() > 8 && buffer.size() < first.bulk.size());
	CHECK(buffer.size() >= 4 && buffer[0] == 0x28 && buffer[1] == 0xB5 && buffer[2] == 0x2F && buffer[3] == 0xFD);

	g_memory_savestate = &buffer;
	CHECK(read_state(first));

	// The next save goes into the same buffer, whose memory it keeps
	const usz capacity = buffer.capacity();
	const u8* const memory = buffer.data();
	write_state(buffer, second);
	CHECK(buffer.capacity() >= capacity);
	CHECK(buffer.capacity() > capacity || buffer.data() == memory);
	CHECK(read_state(second));

	// Without a buffer lent, the path is a file's, and there is none
	g_memory_savestate = nullptr;
	CHECK(!make_savestate_reader(memory_savestate_path));

	std::printf("test_savestate_memory: %u bytes compressed from %u, read back from the buffer twice%s\n",
		static_cast<unsigned>(buffer.size()), static_cast<unsigned>(second.bulk.size()), failures ? "" : ", ok");
}

int main()
{
	named_thread test("Savestate Test", [] { run(); });
	test();
	return failures ? 1 : 0;
}
