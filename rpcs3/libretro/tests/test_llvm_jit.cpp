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

#include <cstdio>

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
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main()
{
	jit_compiler jit({}, jit_compiler::cpu(""));
	llvm::LLVMContext& ctx = jit.get_context();
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

	jit.add(std::move(mod));
	jit.fin();

	const auto f_mul_add = reinterpret_cast<s64 (*)(s64, s64)>(jit.get("mul_add"));
	const auto f_sum = reinterpret_cast<s64 (*)(s64)>(jit.get("sum_mul_add"));
	const auto f_add4 = reinterpret_cast<void (*)(f32*, const f32*, const f32*)>(jit.get("add4"));
	CHECK(f_mul_add && f_sum && f_add4);

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

	if (failures)
	{
		std::fprintf(stderr, "test_llvm_jit: %d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_llvm_jit: LLVM %s compiled and ran code for %s, ok\n", LLVM_VERSION_STRING, jit_compiler::cpu("").c_str());
	return 0;
}
