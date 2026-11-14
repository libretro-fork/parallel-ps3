// The LLVM the core is built with (by default the Makefile's, from the
// 3rdparty/llvm submodule) compiling and running code through RPCS3's own
// JIT (Utilities/JITLLVM.cpp's jit_compiler, as the PPU and SPU recompilers
// use it): the native target found and initialised, a module of a few
// functions - arithmetic, a loop, a call between them, vector maths -
// optimised, compiled for this CPU and called, each answer checked.
//
// Built by `make check` from the core's own objects and archives, the same
// link as the core's with this main, when the core has LLVM.

#include "stdafx.h"
#include "Utilities/JIT.h"

#include "Crypto/unzip.h"
#include "Utilities/File.h"
#include "Utilities/object_pack.h"

#include <encodings/crc32.h>
#include "Emu/IdManager.h"
#include "Emu/system_config.h"
#include "Emu/Cell/PPUAnalyser.h"
#include "Emu/Cell/lv2/sys_sync.h"
#include "Emu/Cell/SPURecompiler.h"
#include "Emu/CPU/jit_host_symbols.h"

#include <retro_atomic.h>

#include <time.h> // not pacing: the benchmark's stopwatch

#include <cstdio>
#include <filesystem>
#include <unistd.h>

#ifdef _MSC_VER
#pragma warning(push, 0)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsuggest-override"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/PassInfo.h>
#include <llvm/PassRegistry.h>
#include <llvm/Support/DynamicLibrary.h>

#include <thread>
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

// The debugger's list of JIT-compiled objects (the GDB JIT interface, in
// LLVM's GDBRegistrationListener.cpp). MCJIT puts every object it loads on
// it, each as a second copy of the whole object kept for as long as its
// engine lives, under one lock for the process. The core takes itself off.
extern "C"
{
	struct test_jit_descriptor
	{
		u32 version;
		u32 action_flag;
		void* relevant_entry;
		void* first_entry;
	};

	extern test_jit_descriptor __jit_debug_descriptor;
}

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// The test's module, made in a compiler's context
static std::unique_ptr<llvm::Module> make_module(llvm::LLVMContext& ctx)
{
	auto mod = std::make_unique<llvm::Module>("test_llvm_jit", ctx);
	llvm::IRBuilder<> b(ctx);
	llvm::Type* const i64 = b.getInt64Ty();

	// mul_add(a, b) = a * b + 7
	llvm::Function* const mul_add = llvm::Function::Create(llvm::FunctionType::get(i64, {i64, i64}, false),
		llvm::Function::ExternalLinkage, "mul_add", *mod);
	b.SetInsertPoint(llvm::BasicBlock::Create(ctx, "", mul_add));
	b.CreateRet(b.CreateAdd(b.CreateMul(mul_add->getArg(0), mul_add->getArg(1)), b.getInt64(7)));

	// sum_mul_add(n) = the sum of mul_add(i, i) for i below n: a loop and a call
	llvm::Function* const sum = llvm::Function::Create(llvm::FunctionType::get(i64, {i64}, false),
		llvm::Function::ExternalLinkage, "sum_mul_add", *mod);
	{
		llvm::BasicBlock* const entry = llvm::BasicBlock::Create(ctx, "", sum);
		llvm::BasicBlock* const loop = llvm::BasicBlock::Create(ctx, "", sum);
		llvm::BasicBlock* const done = llvm::BasicBlock::Create(ctx, "", sum);
		b.SetInsertPoint(entry);
		b.CreateCondBr(b.CreateICmpSGT(sum->getArg(0), b.getInt64(0)), loop, done);
		b.SetInsertPoint(loop);
		llvm::PHINode* const i = b.CreatePHI(i64, 2);
		llvm::PHINode* const acc = b.CreatePHI(i64, 2);
		i->addIncoming(b.getInt64(0), entry);
		acc->addIncoming(b.getInt64(0), entry);
		llvm::Value* const next_acc = b.CreateAdd(acc, b.CreateCall(mul_add, {i, i}));
		llvm::Value* const next_i = b.CreateAdd(i, b.getInt64(1));
		i->addIncoming(next_i, loop);
		acc->addIncoming(next_acc, loop);
		b.CreateCondBr(b.CreateICmpSLT(next_i, sum->getArg(0)), loop, done);
		b.SetInsertPoint(done);
		llvm::PHINode* const result = b.CreatePHI(i64, 2);
		result->addIncoming(b.getInt64(0), entry);
		result->addIncoming(next_acc, loop);
		b.CreateRet(result);
	}

	// add4(dst, a, b): four floats added, through vector types
	llvm::Type* const v4f = llvm::FixedVectorType::get(b.getFloatTy(), 4);
	llvm::Type* const ptr = b.getPtrTy();
	llvm::Function* const add4 = llvm::Function::Create(llvm::FunctionType::get(b.getVoidTy(), {ptr, ptr, ptr}, false),
		llvm::Function::ExternalLinkage, "add4", *mod);
	b.SetInsertPoint(llvm::BasicBlock::Create(ctx, "", add4));
	b.CreateStore(b.CreateFAdd(b.CreateLoad(v4f, add4->getArg(1)), b.CreateLoad(v4f, add4->getArg(2))), add4->getArg(0));
	b.CreateRetVoid();

	CHECK(!llvm::verifyModule(*mod, &llvm::errs()));
	return mod;
}

// SPU programs to compile: straight runs of the arithmetic a game's SPU code
// is mostly made of, each ending in a return (bi $lr)
static spu_program spu_function(u32 index, u32 length)
{
	spu_program prog{};
	prog.entry_point = 0x1000 + index * 0x400 % 0x30000;
	prog.lower_bound = prog.entry_point;

	u32 x = index * 2654435761u + 12345;
	const auto next = [&]() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; };

	// 11-bit opcodes, rb ra rt: a sf and or xor nor fa fs fm shl rotm ceq cgt mpy andc
	static constexpr u32 rr[] = {0x0c0, 0x040, 0x0c1, 0x041, 0x241, 0x049, 0x2c4, 0x2c5, 0x2c6, 0x05b, 0x059, 0x3c0, 0x240, 0x3c4, 0x2c1};

	for (u32 i = 0; i + 1 < length; i++)
	{
		const u32 r = next();
		u32 op;
		if (r % 8 == 0)
		{
			// ai rt, ra, imm
			op = 0x1cu << 24 | (r >> 8 & 0x3ff) << 14 | (r >> 20 & 0x3f) << 7 | (r >> 26 & 0x3f);
		}
		else if (r % 8 == 1)
		{
			// lqd / stqd rt, imm(r1)
			op = ((r & 0x100) ? 0x34u : 0x24u) << 24 | (r >> 9 & 0xff) << 14 | 1u << 7 | (r >> 20 & 0x3f);
		}
		else
		{
			op = rr[(r >> 3) % std::size(rr)] << 21 | (r >> 8 & 0x3f) << 14 | (r >> 14 & 0x3f) << 7 | (r >> 20 & 0x3f);
		}
		prog.data.push_back(std::bit_cast<u32>(be_t<u32>{op}));
	}

	prog.data.push_back(std::bit_cast<u32>(be_t<u32>{0x1a8u << 21}));
	return prog;
}

static double stopwatch()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t); // not pacing: the benchmark's stopwatch
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

// Compiles the SPU functions, last first if asked, with a recompiler and a
// runtime of their own as a boot has; returns the time taken
static double spu_functions(const std::string& cache, bool backwards)
{
	g_fxo->reset();
	g_fxo->init<main_ppu_module<lv2_obj>>();
	g_fxo->get<main_ppu_module<lv2_obj>>().cache = cache;
	g_fxo->init<spu_cache>();
	g_fxo->init<spu_runtime>();

	const auto compiler = spu_recompiler_base::make_llvm_recompiler();
	compiler->init();

	constexpr u32 count = 200;
	const double a = stopwatch();
	u32 made = 0;
	for (u32 n = 0; n < count; n++)
	{
		// As an SPU thread does it: the program in local storage, analysed
		// there, then compiled
		const u32 i = backwards ? count - 1 - n : n;
		static std::vector<be_t<u32>> ls(SPU_LS_SIZE / 4);
		std::fill(ls.begin(), ls.end(), be_t<u32>{0});
		const spu_program prog = spu_function(i, 40 + i % 7 * 60);
		std::memcpy(&ls[prog.entry_point / 4], prog.data.data(), prog.data.size() * 4);
		made += !!compiler->compile(compiler->analyse(ls.data(), prog.entry_point));
	}
	const double b = stopwatch();
	CHECK(made == count);
	return b - a;
}

// The objects in a directory's pack - how many, their bytes and a checksum
// of them - and how many notes are kept beside them
static usz pack_objects(const std::string& pack, usz& notes, usz& size, u32& crc)
{
	std::vector<u8> image;
	if (fs::file f{pack, fs::read})
		image = f.to_vector<u8>();
	static object_pack_entry entries[4096];
	usz count = 0;
	object_pack_scan(image.data(), image.size(), entries, 4096, &count);
	CHECK(count <= 4096);
	usz objects = 0;
	notes = 0;
	size = 0;
	crc = 0;
	for (usz i = 0; i < count && i < 4096; i++)
	{
		const std::string_view name(entries[i].name, entries[i].name_len);
		if (name.ends_with(".key"))
		{
			notes++;
			continue;
		}
		objects++;
		size += entries[i].size;
		crc = encoding_crc32(crc, image.data() + entries[i].offset, entries[i].size);
	}
	return objects;
}

extern retro_atomic_int_t g_spu_llvm_noted_loads;

// How many objects a directory's pack holds
static usz pack_records(const std::string& pack)
{
	std::vector<u8> image;
	if (fs::file f{pack, fs::read})
		image = f.to_vector<u8>();
	usz count = 0;
	object_pack_scan(image.data(), image.size(), nullptr, 0, &count);
	return count;
}

// LLVM's process-wide tables have no lock (3rdparty/llvm: the pass
// registry, the statics made on first use, call_once, the symbols and
// libraries of DynamicLibrary), so that recompiler threads never wait on
// each other in them. Each is gone through here from several threads at
// once, the first time anything in the process uses LLVM.

// Compiles the module and runs it, a few times over, as a recompiler thread
// does with its own compiler. Returns how many answers were wrong.
static int compile_and_run(int rounds)
{
	int wrong = 0;

	for (int i = 0; i < rounds; i++)
	{
		jit_compiler jit({}, jit_compiler::cpu(""));
		jit.add(make_module(jit.get_context()));
		jit.fin();

		const auto f_mul_add = reinterpret_cast<s64 (*)(s64, s64)>(jit.get("mul_add"));
		const auto f_sum = reinterpret_cast<s64 (*)(s64)>(jit.get("sum_mul_add"));
		wrong += !f_mul_add || !f_sum || f_mul_add(3, 4) != 19 || f_sum(10) != 355;
	}

	return wrong;
}

static char s_pass_ids[4][400];

static void threads_in_llvm()
{
	constexpr int count = 4;
	std::thread threads[count];
	int wrong[count]{};
	retro_atomic_int_t go{};

	// 1. The first compiles of the process, all at once: every pass is
	// registered and every static made by whichever thread gets there
	for (int t = 0; t < count; t++)
	{
		threads[t] = std::thread([&, t]()
		{
			while (!retro_atomic_load_acquire_int(&go))
				std::this_thread::yield();
			wrong[t] = compile_and_run(6);
		});
	}
	retro_atomic_store_release_int(&go, 1);
	for (auto& th : threads)
		th.join();
	for (int t = 0; t < count; t++)
		CHECK(wrong[t] == 0);

	// 2. The pass registry alone: each thread registers passes of its own
	// while it looks up everybody's. What a lookup gives is the pass asked
	// for or nothing; afterwards every one is there, by identifier and by
	// argument.
	static std::string names[count][400];
	for (int t = 0; t < count; t++)
		for (int i = 0; i < 400; i++)
			names[t][i] = fmt::format("libretro-test-pass-%d-%d", t, i);

	llvm::PassRegistry& registry = *llvm::PassRegistry::getPassRegistry();
	retro_atomic_store_release_int(&go, 0);
	for (int t = 0; t < count; t++)
	{
		wrong[t] = 0;
		threads[t] = std::thread([&, t]()
		{
			while (!retro_atomic_load_acquire_int(&go))
				std::this_thread::yield();

			for (int i = 0; i < 400; i++)
			{
				registry.registerPass(*new llvm::PassInfo(names[t][i], names[t][i], &s_pass_ids[t][i], nullptr, false, false), true);

				for (int o = 0; o < count; o++)
				{
					const llvm::PassInfo* by_id = registry.getPassInfo(&s_pass_ids[o][i]);
					const llvm::PassInfo* by_arg = registry.getPassInfo(llvm::StringRef(names[o][i]));
					wrong[t] += by_id && by_id->getTypeInfo() != &s_pass_ids[o][i];
					wrong[t] += by_arg && by_arg->getPassArgument() != names[o][i];
				}

				wrong[t] += registry.getPassInfo(&s_pass_ids[t][i]) == nullptr;
			}
		});
	}
	retro_atomic_store_release_int(&go, 1);
	for (auto& th : threads)
		th.join();
	for (int t = 0; t < count; t++)
	{
		CHECK(wrong[t] == 0);
		for (int i = 0; i < 400; i++)
		{
			const llvm::PassInfo* by_id = registry.getPassInfo(&s_pass_ids[t][i]);
			const llvm::PassInfo* by_arg = registry.getPassInfo(llvm::StringRef(names[t][i]));
			CHECK(by_id && by_id == by_arg && by_id->getPassName() == names[t][i]);
		}
	}
	CHECK(registry.getPassInfo(&go) == nullptr && registry.getPassInfo(llvm::StringRef("libretro-test-pass-none")) == nullptr);

	// 3. Symbols given by hand, added by every thread while all look them
	// up; a name given again is the newer one
	retro_atomic_store_release_int(&go, 0);
	for (int t = 0; t < count; t++)
	{
		wrong[t] = 0;
		threads[t] = std::thread([&, t]()
		{
			while (!retro_atomic_load_acquire_int(&go))
				std::this_thread::yield();

			for (int i = 0; i < 100; i++)
			{
				llvm::sys::DynamicLibrary::AddSymbol(names[t][i], &s_pass_ids[t][i]);

				for (int o = 0; o < count; o++)
				{
					void* found = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(names[o][i].c_str());
					wrong[t] += found && found != &s_pass_ids[o][i];
				}

				wrong[t] += llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(names[t][i].c_str()) != &s_pass_ids[t][i];
			}
		});
	}
	retro_atomic_store_release_int(&go, 1);
	for (auto& th : threads)
		th.join();
	for (int t = 0; t < count; t++)
		CHECK(wrong[t] == 0);
	llvm::sys::DynamicLibrary::AddSymbol(names[0][0], &s_pass_ids[3][399]);
	CHECK(llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(names[0][0].c_str()) == &s_pass_ids[3][399]);
	// And what the process itself has is still found
	CHECK(llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("memcpy") != nullptr);
}

int main()
{
	threads_in_llvm();

	jit_compiler jit({}, jit_compiler::cpu(""));
	auto mod = make_module(jit.get_context());

	jit.add(std::move(mod));
	jit.fin();

	const auto f_mul_add = reinterpret_cast<s64 (*)(s64, s64)>(jit.get("mul_add"));
	const auto f_sum = reinterpret_cast<s64 (*)(s64)>(jit.get("sum_mul_add"));
	const auto f_add4 = reinterpret_cast<void (*)(f32*, const f32*, const f32*)>(jit.get("add4"));
	CHECK(f_mul_add && f_sum && f_add4);

	// Compiled and loaded, and not registered with a debugger: no copy of
	// the object was made for one
	CHECK(__jit_debug_descriptor.first_entry == nullptr);

	if (f_mul_add && f_sum && f_add4)
	{
		CHECK(f_mul_add(6, 7) == 49);
		CHECK(f_mul_add(-3, 5) == -8);

		s64 want = 0;
		for (s64 i = 0; i < 1000; i++)
			want += i * i + 7;
		CHECK(f_sum(1000) == want);
		CHECK(f_sum(0) == 0);

		alignas(16) const f32 x[4] = {1.5f, -2.0f, 3.25f, 0.0f};
		alignas(16) const f32 y[4] = {0.5f, 2.0f, -1.25f, 8.0f};
		alignas(16) f32 z[4] = {};
		f_add4(z, x, y);
		CHECK(z[0] == 2.0f && z[1] == 0.0f && z[2] == 2.0f && z[3] == 8.0f);
	}

	// The object cache. A directory's objects are in one file, the pack, not
	// a file each, stored as the objects they are (not compressed: inflating
	// one on every boot cost many times what reading it does). An object
	// comes back from the pack in another compiler, in the session that
	// wrote it and - lent from the pack as it was read - in a later one; an
	// object in a file of its own, as they were kept, plain or compressed,
	// loads, moves into the pack and its file goes; a pack cut short counts
	// up to there and is written on from there.
	{
		const std::string dir = fmt::format("%s/test_llvm_jit_%u/", std::filesystem::temp_directory_path().string(), static_cast<u32>(getpid()));
		std::filesystem::create_directories(dir);
		{
			jit_compiler writer({}, jit_compiler::cpu(""));
			writer.add(make_module(writer.get_context()), dir);
			writer.fin();
		}

		const std::string object = dir + "test_llvm_jit";
		const std::string pack = dir + "objects.pack";
		CHECK(fs::is_file(pack));
		CHECK(!fs::is_file(object));
		CHECK(!fs::is_file(object + ".gz"));
		CHECK(jit_compiler::check(object));
		CHECK(!jit_compiler::check(dir + "not_there"));

		const auto runs = [&](const std::string& path)
		{
			jit_compiler reader({}, jit_compiler::cpu(""));
			if (!reader.add(path))
				return false;
			reader.fin();
			const auto cached_sum = reinterpret_cast<s64 (*)(s64)>(reader.get("sum_mul_add"));
			return cached_sum && cached_sum(10) == 355;
		};

		// What the pack holds: read as the file it is
		std::vector<u8> bytes;
		const auto records = [&]() -> usz
		{
			std::vector<u8> image;
			if (fs::file f{pack, fs::read})
				image = f.to_vector<u8>();
			object_pack_entry entries[8]{};
			usz count = 0;
			const u64 valid = object_pack_scan(image.data(), image.size(), entries, 8, &count);
			if (count && count <= 8)
			{
				const object_pack_entry& last = entries[count - 1];
				CHECK(last.offset + last.size <= valid);
				bytes.assign(image.data() + entries[0].offset, image.data() + entries[0].offset + entries[0].size);
				CHECK(std::string_view(entries[0].name, entries[0].name_len) == "test_llvm_jit");
			}
			return count;
		};

		// In the session that compiled it
		CHECK(runs(object));
		CHECK(records() == 1);
		// An object file as the linker knows it, not a compressed stream
		CHECK(bytes.size() > 4 && bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F');

		// In a later one, and with the pack closed again under the compiler
		// that has it
		jit_compiler::close_object_packs();
		CHECK(jit_compiler::check(object));
		{
			jit_compiler reader({}, jit_compiler::cpu(""));
			CHECK(reader.add(object));
			jit_compiler::close_object_packs();
			reader.fin();
			const auto cached_sum = reinterpret_cast<s64 (*)(s64)>(reader.get("sum_mul_add"));
			CHECK(cached_sum && cached_sum(10) == 355);
		}

		// Objects in files of their own
		{
			fs::file old(dir + "old.gz", fs::rewrite);
			CHECK(zip(bytes.data(), bytes.size(), old));
			fs::file loose(dir + "loose", fs::rewrite);
			CHECK(loose.write(bytes.data(), bytes.size()) == bytes.size());
		}
		CHECK(jit_compiler::check(dir + "old"));
		CHECK(runs(dir + "loose"));
		CHECK(!fs::is_file(dir + "old") && !fs::is_file(dir + "old.gz") && !fs::is_file(dir + "loose"));
		CHECK(jit_compiler::check(dir + "old"));
		jit_compiler::close_object_packs();
		CHECK(records() == 3);
		CHECK(runs(dir + "old") && runs(dir + "loose") && runs(object));

		// A pack cut short in its last record, with whatever after it
		jit_compiler::close_object_packs();
		{
			const u64 size = fs::file(pack).size();
			fs::file f(pack, fs::read + fs::write);
			CHECK(f.trunc(size - 24));
			f.seek(0, fs::seek_end);
			const std::vector<u8> junk(4096, 0x5a);
			f.write(junk.data(), junk.size());
		}
		CHECK(records() == 2);
		CHECK(jit_compiler::check(object) && jit_compiler::check(dir + "old"));
		CHECK(!jit_compiler::check(dir + "loose"));
		{
			jit_compiler writer({}, jit_compiler::cpu(""));
			auto again = make_module(writer.get_context());
			again->setModuleIdentifier("again");
			writer.add(std::move(again), dir);
			writer.fin();
		}
		CHECK(runs(dir + "again"));
		jit_compiler::close_object_packs();
		CHECK(records() == 3);
		CHECK(runs(dir + "again") && runs(object));

		jit_compiler::close_object_packs();
		std::filesystem::remove_all(dir);
	}

	// The SPU recompiler's objects are kept too, each under a name made from
	// the module it was compiled from: a later boot loads every one of them
	// and compiles nothing, in whatever order the functions come; a setting
	// that changes what a function compiles to makes it another object, and
	// the old one is not taken for it.
	{
		const std::string dir = fmt::format("%s/test_spu_jit_%u/", std::filesystem::temp_directory_path().string(), static_cast<u32>(getpid()));
		const std::string pack = dir + "llvm/objects.pack";
		std::filesystem::create_directories(dir);
		g_cfg.core.spu_cache.set(true);
		g_cfg.core.spu_debug.set(false);

		const double first = spu_functions(dir, false);
		jit_compiler::close_object_packs();
		const usz made = pack_records(pack);
		usz notes = 0, object_bytes = 0;
		u32 object_crc = 0;
		// Each with its note: what a later boot loads it by, without
		// building its module (SPULLVMRecompiler.cpp, note_name)
		CHECK(pack_objects(pack, notes, object_bytes, object_crc) == 200);
		CHECK(notes == 200 && made == 400);
		CHECK(jit_host_symbols_ambiguous() == 0);
		CHECK(retro_atomic_load_acquire_int(&g_spu_llvm_noted_loads) == 0);
		// The names the compiled objects were left without, if any: an
		// object loaded by its note is linked to the same places, so no
		// name is added to them
		const u32 unplaced = jit_compiler::null_functions();

		// What was compiled, as one number: the same on the same machine
		// from run to run, and a change of it between two builds says the
		// emitted code changed (it is for this CPU, so it is not held to a
		// value here)
		std::printf("test_llvm_jit: the 200 SPU objects are %zu bytes, crc32 %08x\n", object_bytes, object_crc);

		// A later boot: the process knows none of the host functions the
		// objects call until a module that calls one has been built. The
		// first function to need each is built and finds its object by its
		// module, as before the notes; every other is loaded by its note.
		// Nothing is compiled and nothing is added to the pack.
		jit_host_symbols_reset();
		const double second = spu_functions(dir, true);
		jit_compiler::close_object_packs();
		CHECK(pack_records(pack) == made);
		const int noted_cold = retro_atomic_load_acquire_int(&g_spu_llvm_noted_loads);
		CHECK(noted_cold >= 150 && noted_cold < 200);
		CHECK(jit_host_symbols_ambiguous() == 0);

		// And with them known, all 200 by their notes
		const double third = spu_functions(dir, false);
		jit_compiler::close_object_packs();
		CHECK(pack_records(pack) == made);
		CHECK(retro_atomic_load_acquire_int(&g_spu_llvm_noted_loads) == noted_cold + 200);
		CHECK(jit_compiler::null_functions() == unplaced);
		// Loading is many times faster than compiling; twice, to leave a
		// loaded machine room
		CHECK(second * 2 < first);

		const auto accuracy = g_cfg.core.spu_xfloat_accuracy.get();
		g_cfg.core.spu_xfloat_accuracy.set(accuracy == xfloat_accuracy::accurate ? xfloat_accuracy::approximate : xfloat_accuracy::accurate);
		const int noted_before = retro_atomic_load_acquire_int(&g_spu_llvm_noted_loads);
		spu_functions(dir, false);
		jit_compiler::close_object_packs();
		const usz other = pack_records(pack);
		CHECK(other > made + 100);
		// No note of the other setting's was gone by
		CHECK(retro_atomic_load_acquire_int(&g_spu_llvm_noted_loads) == noted_before);
		spu_functions(dir, false);
		jit_compiler::close_object_packs();
		CHECK(pack_records(pack) == other);
		CHECK(retro_atomic_load_acquire_int(&g_spu_llvm_noted_loads) == noted_before + 200);
		g_cfg.core.spu_xfloat_accuracy.set(accuracy);

		std::printf("test_llvm_jit: 200 SPU functions compiled in %.0f ms, loaded from their objects in %.0f ms (%d by their notes), all by their notes in %.0f ms\n", first, second, noted_cold, third);
		// Nor were the objects loaded from the pack
		CHECK(__jit_debug_descriptor.first_entry == nullptr);

		g_fxo->clear();
		std::filesystem::remove_all(dir);
	}

	if (failures)
	{
		std::fprintf(stderr, "test_llvm_jit: %d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_llvm_jit: LLVM %s compiled and ran code for %s, ok\n", LLVM_VERSION_STRING, jit_compiler::cpu("").c_str());
	return 0;
}
