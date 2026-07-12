#include "RuntimeBuilder.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Intrinsics.h>

using namespace llvm;

namespace angara {

// ============================================================================
// generateFreestandingAllocRuntime — freestanding mode WITH a built-in heap.
//
// Unlike bare --freestanding (which stubs collections/strings/records to
// no-ops and has no allocator), --freestanding-alloc enables the FULL runtime
// — strings, lists, records, conversions — backed by a built-in bump allocator.
// This mirrors --kernel mode (which keeps the full runtime and lets the kernel
// shim supply malloc/realloc/free), but instead of an external C allocator we
// emit a self-contained libc layer (generateFreestandingCLib) that defines
// malloc/realloc/free/memcpy/memset/strlen/strcmp/strdup/snprintf as in-module
// LLVM IR. The result is a freestanding object with zero undefined libc symbols.
//
// The data-structure generators (Strings.cpp, Collections.cpp, Conversions.cpp)
// look up libc functions by name via m_module.getFunction("memcpy"), etc. As
// long as generateFreestandingCLib defines them BEFORE those generators run,
// the lookups find real definitions instead of null.
//
// try/throw/spawn/Mutex/native-attach remain hard-errored (E910-E914), so we
// stub the exception surface (stubKernelExceptions) exactly like --kernel does.
// IO is stubbed to no-ops — bare metal has no stdout; the user drives MMIO via
// peek/poke intrinsics.
// ============================================================================

void RuntimeBuilder::generateFreestandingAllocRuntime(uint64_t heap_size) {
    // Default 1 MiB if no size was given.
    if (heap_size == 0) heap_size = 1024ULL * 1024;

    // Define all libc functions in-module (malloc = bump allocator, etc.).
    // Must run BEFORE generateMemoryManagement and the data-structure generators,
    // which look up "malloc"/"memcpy"/etc. by name.
    generateFreestandingCLib(heap_size);

    // The full memory layer: vtable (AngaraAllocator), __ang_rt_alloc/free,
    // __ang_allocator_set/get, __ang_rt_finalize. The default allocator wraps
    // the now-defined malloc/realloc/free.
    generateMemoryManagement();

    // Heap-dependent data-structure generators (same set as --kernel mode).
    generateStringOps();
    generateEquality();
    generateObjectHash();
    generateListOps();
    generateRawArrayOps();
    generateVectorOps();
    generateRecordOps();
    generateConversions();
    generateDeepClone();
    generateClosureOps();
    generateDeferOps();

    // Exception stubs: try/throw are hard-errored upstream (E910/E911), but
    // generateModuleAPIVTable references __ang_exception_new/__ang_throw by name,
    // so they must exist as no-op stubs or getFunction() returns null → segfault.
    stubKernelExceptions();

    // IO: no-op stubs. Bare metal has no stdout; the user drives MMIO.
    generateFreestandingIOStubs();

    generateModuleAPIVTable();

    // __ang_api_throw_error: route to llvm.trap (bare metal has no exception
    // machinery — a runtime error like div-by-zero is a hard fault). Mirrors
    // the bare-freestanding stub at Freestanding.cpp. The user-overridable
    // __ang_fs_panic hook is called first so diagnostics can be written before
    // the trap.
    {
        auto* void_ty = Type::getVoidTy(m_ctx);
        auto* i8_ptr  = PointerType::get(m_ctx, 0);
        Function* existing = m_module.getFunction("__ang_api_throw_error");
        if (!existing) {
            existing = cast<Function>(
                m_module.getOrInsertFunction("__ang_api_throw_error",
                    FunctionType::get(void_ty, {i8_ptr}, false)).getCallee());
        }
        if (existing->empty()) {
            auto* entry = BasicBlock::Create(m_ctx, "entry", existing);
            IRBuilder<> b(entry);
            b.CreateCall(m_module.getFunction("__ang_fs_panic"), {existing->arg_begin()});
            auto* trap = Intrinsic::getOrInsertDeclaration(&m_module, Intrinsic::trap);
            b.CreateCall(trap, {});
            b.CreateUnreachable();
        }
    }
}

// ---------------------------------------------------------------------------
// generateFreestandingIOStubs — no-op stubs for the IO runtime symbols that
// codegen may reference. Mirrors the IO subset of generateFreestandingStubs.
// ---------------------------------------------------------------------------

void RuntimeBuilder::generateFreestandingIOStubs() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i32_ty  = Type::getInt32Ty(m_ctx);
    auto* i64_ty  = Type::getInt64Ty(m_ctx);
    auto* obj_ty  = m_angara_obj_type;

    auto make_nil = [&](IRBuilder<>& b) -> Value* {
        Value* v = UndefValue::get(obj_ty);
        v = b.CreateInsertValue(v, ConstantInt::get(i32_ty, TAG_NIL), {0});
        v = b.CreateInsertValue(v, ConstantInt::get(i64_ty, 0), {1});
        return v;
    };

    auto stub_void = [&](const std::string& name, FunctionType* ty, FunctionCallee& fc) {
        auto* fn = createRuntimeFunc(name, ty); fc = FunctionCallee(fn);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn); IRBuilder<>(e).CreateRetVoid();
    };
    auto stub_nil = [&](const std::string& name, FunctionType* ty, FunctionCallee& fc) {
        auto* fn = createRuntimeFunc(name, ty); fc = FunctionCallee(fn);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn); IRBuilder<> b(e); b.CreateRet(make_nil(b));
    };

    stub_void("__ang_io_print",   FunctionType::get(void_ty, {obj_ty, obj_ty}, false), m_fn_io_print);
    stub_void("__ang_io_println", FunctionType::get(void_ty, {obj_ty, obj_ty}, false), m_fn_io_println);
    stub_void("__ang_io_write",   FunctionType::get(void_ty, {obj_ty, obj_ty}, false), m_fn_io_write);
    stub_void("__ang_io_flush",   FunctionType::get(void_ty, {obj_ty}, false),       m_fn_io_flush);
    stub_nil ("__ang_io_read_line", FunctionType::get(obj_ty, {}, false),            m_fn_io_read_line);
    stub_nil ("__ang_io_read_all",  FunctionType::get(obj_ty, {}, false),            m_fn_io_read_all);
}

// ============================================================================
// generateFreestandingCLib — self-contained libc for freestanding+alloc mode.
//
// Defines the libc functions the data-structure generators reference, as
// in-module LLVM IR (not external declarations). This keeps the freestanding
// object free of undefined libc symbols.
//
//   malloc(size)    — bump allocator over a BSS heap region.
//   realloc(p,old,new) — in-place if shrinking, else malloc+memcpy.
//   free(ptr,size)  — no-op (bump semantics; never reclaims).
//   memcpy/memset   — llvm.memcpy/memset intrinsics.
//   strlen          — IR loop.
//   strcmp          — IR loop.
//   strdup(s)       — malloc(strlen+1) + memcpy.
//   snprintf        — minimal formatter (%ld, %d, %u, %s, %c, %.15g basic).
//
// All are ExternalLinkage so the generators' getFunction("malloc") lookups find
// them. The bump allocator globals (__ang_fs_heap / __ang_fs_bump / __ang_fs_end)
// are InternalLinkage — not visible to the user's linker.
// ============================================================================

void RuntimeBuilder::generateFreestandingCLib(uint64_t heap_size) {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i8_ty   = Type::getInt8Ty(m_ctx);
    auto* i32_ty  = Type::getInt32Ty(m_ctx);
    auto* i64_ty  = Type::getInt64Ty(m_ctx);
    auto* f64_ty  = Type::getDoubleTy(m_ctx);
    auto* i8_ptr  = PointerType::get(m_ctx, 0);

    // ----------------------------------------------------------------------
    // Bump allocator state: a BSS-placed heap region + a mutable bump pointer.
    // ----------------------------------------------------------------------
    auto* heap_type = ArrayType::get(i8_ty, heap_size);
    auto* heap_gv = new GlobalVariable(
        m_module, heap_type, false, GlobalValue::InternalLinkage,
        ConstantAggregateZero::get(heap_type), "__ang_fs_heap");
    heap_gv->setAlignment(Align(16));

    std::vector<Constant*> base_indices = {
        ConstantInt::get(i64_ty, 0), ConstantInt::get(i64_ty, 0)};
    auto* heap_base = ConstantExpr::getGetElementPtr(
        heap_type, heap_gv, base_indices, true);
    auto* bump_gv = new GlobalVariable(
        m_module, i8_ptr, false, GlobalValue::InternalLinkage,
        heap_base, "__ang_fs_bump");

    std::vector<Constant*> end_indices = {
        ConstantInt::get(i64_ty, 0), ConstantInt::get(i64_ty, heap_size)};
    auto* heap_end = ConstantExpr::getGetElementPtr(
        heap_type, heap_gv, end_indices, true);
    auto* end_gv = new GlobalVariable(
        m_module, i8_ptr, true, GlobalValue::InternalLinkage,
        heap_end, "__ang_fs_end");

    // Helper to create an external-linkage libc function in the module.
    auto define_cfunc = [&](const std::string& name, FunctionType* ty) -> Function* {
        auto* fn = cast<Function>(m_module.getOrInsertFunction(name, ty).getCallee());
        fn->setLinkage(Function::ExternalLinkage);
        fn->setDSOLocal(true);
        return fn;
    };

    // Define functions in dependency order:
    //   memcpy, memset, strlen, strcmp  (no dependencies)
    //   → malloc, free                  (no dependencies)
    //   → realloc                       (depends on malloc + memcpy)
    //   → strdup                        (depends on malloc + strlen + memcpy)

    // ----------------------------------------------------------------------
    // memcpy(i8* dst, i8* src, i64 n) -> i8* : llvm.memcpy intrinsic.
    // ----------------------------------------------------------------------
    {
        auto* fn = define_cfunc("memcpy", FunctionType::get(i8_ptr, {i8_ptr, i8_ptr, i64_ty}, false));
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        auto* dst = fn->arg_begin();
        auto* src = fn->arg_begin() + 1;
        auto* n   = fn->arg_begin() + 2;
        b.CreateMemCpy(dst, Align(1), src, Align(1), n);
        b.CreateRet(dst);
    }

    // ----------------------------------------------------------------------
    // memset(i8* s, i32 c, i64 n) -> i8* : llvm.memset intrinsic.
    // ----------------------------------------------------------------------
    {
        auto* fn = define_cfunc("memset", FunctionType::get(i8_ptr, {i8_ptr, i32_ty, i64_ty}, false));
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        auto* s = fn->arg_begin();
        auto* c = fn->arg_begin() + 1;
        auto* n = fn->arg_begin() + 2;
        b.CreateMemSet(s, b.CreateTrunc(c, i8_ty), n, Align(1));
        b.CreateRet(s);
    }

    // ----------------------------------------------------------------------
    // strlen(i8* s) -> i64 : IR loop.
    // ----------------------------------------------------------------------
    {
        auto* fn = define_cfunc("strlen", FunctionType::get(i64_ty, {i8_ptr}, false));
        auto* s = fn->arg_begin();

        auto* entry_bb = BasicBlock::Create(m_ctx, "entry", fn);
        auto* loop_bb  = BasicBlock::Create(m_ctx, "loop", fn);
        auto* body_bb  = BasicBlock::Create(m_ctx, "body", fn);
        auto* done_bb  = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<>(entry_bb).CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* i_phi = bl.CreatePHI(i64_ty, 2, "i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), entry_bb);
        auto* c = bl.CreateLoad(i8_ty, bl.CreateGEP(i8_ty, s, {i_phi}), "c");
        bl.CreateCondBr(bl.CreateICmpEQ(c, ConstantInt::get(i8_ty, 0)), done_bb, body_bb);

        IRBuilder<> bb(body_bb);
        auto* next = bb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1), "next");
        bb.CreateBr(loop_bb);
        i_phi->addIncoming(next, body_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRet(i_phi);
    }

    // ----------------------------------------------------------------------
    // strcmp(i8* a, i8* b) -> i32 : IR loop.
    // ----------------------------------------------------------------------
    {
        auto* fn = define_cfunc("strcmp", FunctionType::get(i32_ty, {i8_ptr, i8_ptr}, false));
        auto* a = fn->arg_begin();
        auto* b_arg = fn->arg_begin() + 1;

        auto* entry_bb = BasicBlock::Create(m_ctx, "entry", fn);
        auto* loop_bb  = BasicBlock::Create(m_ctx, "loop", fn);
        auto* diff_bb  = BasicBlock::Create(m_ctx, "diff", fn);

        IRBuilder<>(entry_bb).CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* i_phi = bl.CreatePHI(i64_ty, 2, "i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), entry_bb);
        auto* ca = bl.CreateLoad(i8_ty, bl.CreateGEP(i8_ty, a, {i_phi}));
        auto* cb = bl.CreateLoad(i8_ty, bl.CreateGEP(i8_ty, b_arg, {i_phi}));
        auto* both_null = bl.CreateAnd(
            bl.CreateICmpEQ(ca, ConstantInt::get(i8_ty, 0)),
            bl.CreateICmpEQ(cb, ConstantInt::get(i8_ty, 0)));
        auto* differ = bl.CreateICmpNE(ca, cb);
        auto* cont = bl.CreateAnd(bl.CreateNot(both_null), bl.CreateNot(differ));
        auto* next = bl.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        i_phi->addIncoming(next, loop_bb);
        bl.CreateCondBr(cont, loop_bb, diff_bb);

        IRBuilder<> bd(diff_bb);
        bd.CreateRet(bd.CreateSub(
            bd.CreateSExt(ca, i32_ty), bd.CreateSExt(cb, i32_ty)));
    }

    // ----------------------------------------------------------------------
    // malloc(i64 size) -> i8* : bump allocator with 16-byte alignment.
    // ----------------------------------------------------------------------
    {
        auto* fn = define_cfunc("malloc", FunctionType::get(i8_ptr, {i64_ty}, false));
        auto* size_arg = fn->arg_begin();

        auto* entry_bb = BasicBlock::Create(m_ctx, "entry", fn);
        auto* oob_bb   = BasicBlock::Create(m_ctx, "oob", fn);
        auto* alloc_bb = BasicBlock::Create(m_ctx, "alloc", fn);

        IRBuilder<> b(entry_bb);
        auto* size_plus = b.CreateAdd(size_arg, ConstantInt::get(i64_ty, 15));
        auto* aligned   = b.CreateAnd(size_plus, ConstantInt::get(i64_ty, ~(uint64_t)15));
        auto* old_bump  = b.CreateLoad(i8_ptr, bump_gv, "old_bump");
        auto* new_bump  = b.CreateGEP(i8_ty, old_bump, {aligned}, "new_bump");
        auto* end_ptr   = b.CreateLoad(i8_ptr, end_gv, "end");
        auto* oob       = b.CreateICmpUGT(new_bump, end_ptr, "oob");
        b.CreateCondBr(oob, oob_bb, alloc_bb);

        IRBuilder<> bo(oob_bb);
        auto* trap = Intrinsic::getOrInsertDeclaration(&m_module, Intrinsic::trap);
        bo.CreateCall(trap, {});
        bo.CreateUnreachable();

        IRBuilder<> ba(alloc_bb);
        ba.CreateStore(new_bump, bump_gv);
        ba.CreateRet(old_bump);
    }

    // ----------------------------------------------------------------------
    // free(i8* ptr) -> void : no-op.
    // ----------------------------------------------------------------------
    {
        auto* fn = define_cfunc("free", FunctionType::get(void_ty, {i8_ptr}, false));
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRetVoid();
    }

    // ----------------------------------------------------------------------
    // realloc(i8* ptr, i64 new_size) -> i8* : malloc + memcpy.
    // ----------------------------------------------------------------------
    {
        auto* fn = define_cfunc("realloc", FunctionType::get(i8_ptr, {i8_ptr, i64_ty}, false));
        auto* ptr_arg  = fn->arg_begin();
        auto* size_arg = fn->arg_begin() + 1;

        auto* entry    = BasicBlock::Create(m_ctx, "entry", fn);
        auto* malloc_bb = BasicBlock::Create(m_ctx, "malloc_only", fn);
        auto* copy_bb   = BasicBlock::Create(m_ctx, "malloc_copy", fn);

        IRBuilder<> b(entry);
        auto* is_null = b.CreateICmpEQ(ptr_arg, ConstantPointerNull::get(i8_ptr), "is_null");
        b.CreateCondBr(is_null, malloc_bb, copy_bb);

        IRBuilder<> bm(malloc_bb);
        auto* malloc_fn = m_module.getFunction("malloc");
        auto* new_mem_m = bm.CreateCall(malloc_fn, {size_arg}, "new_mem");
        bm.CreateRet(new_mem_m);

        IRBuilder<> bc(copy_bb);
        auto* new_mem = bc.CreateCall(malloc_fn, {size_arg}, "new_mem");
        auto* memcpy_fn = m_module.getFunction("memcpy");
        bc.CreateCall(memcpy_fn, {new_mem, ptr_arg, size_arg});
        bc.CreateRet(new_mem);
    }

    // ----------------------------------------------------------------------
    // strdup(i8* s) -> i8* : malloc(strlen+1) + memcpy.
    // ----------------------------------------------------------------------
    {
        auto* fn = define_cfunc("strdup", FunctionType::get(i8_ptr, {i8_ptr}, false));
        auto* s = fn->arg_begin();
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        auto* strlen_fn = m_module.getFunction("strlen");
        auto* malloc_fn = m_module.getFunction("malloc");
        auto* memcpy_fn = m_module.getFunction("memcpy");
        auto* len = b.CreateCall(strlen_fn, {s}, "len");
        auto* alloc_size = b.CreateAdd(len, ConstantInt::get(i64_ty, 1), "alloc_size");
        auto* mem = b.CreateCall(malloc_fn, {alloc_size}, "mem");
        b.CreateCall(memcpy_fn, {mem, s, alloc_size});
        b.CreateRet(mem);
    }

    // ----------------------------------------------------------------------
    // Helper: __ang_fs_putc(i64* pos_ptr, i8* buf, i64 size, i8 ch)
    //
    // Writes one byte at buf[*pos] if *pos < size, then increments *pos.
    // Used by snprintf below. InternalLinkage.
    // ----------------------------------------------------------------------
    Function* fs_putc = nullptr;
    {
        auto* pos_ptr_ty = PointerType::get(i64_ty, 0);
        auto* fn = createRuntimeFunc("__ang_fs_putc",
            FunctionType::get(void_ty, {pos_ptr_ty, i8_ptr, i64_ty, i8_ty}, false));
        fs_putc = fn;

        auto* pos_ptr = fn->arg_begin();
        auto* buf_arg = fn->arg_begin() + 1;
        auto* size_arg = fn->arg_begin() + 2;
        auto* ch_arg   = fn->arg_begin() + 3;

        auto* entry    = BasicBlock::Create(m_ctx, "entry", fn);
        auto* write_bb = BasicBlock::Create(m_ctx, "write", fn);
        auto* done_bb  = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* pos = b.CreateLoad(i64_ty, pos_ptr, "pos");
        auto* in_bounds = b.CreateICmpULT(pos, size_arg, "in_bounds");
        b.CreateCondBr(in_bounds, write_bb, done_bb);

        IRBuilder<> bw(write_bb);
        auto* dst = bw.CreateGEP(i8_ty, buf_arg, {pos});
        bw.CreateStore(ch_arg, dst);
        bw.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        auto* pos_val = bd.CreateLoad(i64_ty, pos_ptr, "pos_val");
        bd.CreateStore(bd.CreateAdd(pos_val, ConstantInt::get(i64_ty, 1)), pos_ptr);
        bd.CreateRetVoid();
    }

    // ----------------------------------------------------------------------
    // Helper: __ang_fs_format_i64(i64* pos_ptr, i8* buf, i64 size, i64 val)
    //
    // Formats a signed i64 as decimal, writing via __ang_fs_putc.
    // InternalLinkage.
    // ----------------------------------------------------------------------
    Function* fs_format_i64 = nullptr;
    {
        auto* pos_ptr_ty = PointerType::get(i64_ty, 0);
        auto* fn = createRuntimeFunc("__ang_fs_format_i64",
            FunctionType::get(void_ty, {pos_ptr_ty, i8_ptr, i64_ty, i64_ty}, false));
        fs_format_i64 = fn;

        auto* pos_ptr = fn->arg_begin();
        auto* buf_arg = fn->arg_begin() + 1;
        auto* size_arg = fn->arg_begin() + 2;
        auto* val_arg  = fn->arg_begin() + 3;

        auto* entry   = BasicBlock::Create(m_ctx, "entry", fn);
        auto* neg_bb  = BasicBlock::Create(m_ctx, "neg", fn);
        auto* abs_bb  = BasicBlock::Create(m_ctx, "abs", fn);

        IRBuilder<> b(entry);
        auto* is_neg = b.CreateICmpSLT(val_arg, ConstantInt::get(i64_ty, 0), "is_neg");
        b.CreateCondBr(is_neg, neg_bb, abs_bb);

        // Negative: write '-', then negate.
        IRBuilder<> bn(neg_bb);
        auto* putc_fn = fs_putc;
        bn.CreateCall(putc_fn, {pos_ptr, buf_arg, size_arg,
            ConstantInt::get(i8_ty, '-')});
        auto* neg_val = bn.CreateNeg(val_arg, "neg_val");
        bn.CreateBr(abs_bb);

        // abs_bb: format the absolute value.
        IRBuilder<> ba(abs_bb);
        auto* abs_val = ba.CreatePHI(i64_ty, 2, "abs_val");
        abs_val->addIncoming(val_arg, entry);
        abs_val->addIncoming(neg_val, neg_bb);

        // Write digits into a temp buffer in reverse, then output in reverse.
        auto* temp = ba.CreateAlloca(ArrayType::get(i8_ty, 24), nullptr, "temp");
        auto* temp_i8 = ba.CreateBitCast(temp, i8_ptr);
        auto* ti_ptr = ba.CreateAlloca(i64_ty, nullptr, "ti");
        ba.CreateStore(ConstantInt::get(i64_ty, 0), ti_ptr);

        auto* divloop = BasicBlock::Create(m_ctx, "divloop", fn);
        auto* revloop = BasicBlock::Create(m_ctx, "revloop", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);
        ba.CreateBr(divloop);

        // divmod loop: extract digits in reverse.
        IRBuilder<> bdl(divloop);
        auto* remaining = bdl.CreatePHI(i64_ty, 2, "remaining");
        remaining->addIncoming(abs_val, abs_bb);
        auto* digit = bdl.CreateSRem(remaining, ConstantInt::get(i64_ty, 10), "digit");
        auto* quotient = bdl.CreateSDiv(remaining, ConstantInt::get(i64_ty, 10), "quotient");
        auto* digit_char = bdl.CreateTrunc(
            bdl.CreateAdd(digit, ConstantInt::get(i64_ty, '0')), i8_ty, "dchar");
        auto* ti_cur = bdl.CreateLoad(i64_ty, ti_ptr, "ti");
        bdl.CreateStore(digit_char, bdl.CreateGEP(i8_ty, temp_i8, {ti_cur}));
        bdl.CreateStore(bdl.CreateAdd(ti_cur, ConstantInt::get(i64_ty, 1)), ti_ptr);
        auto* is_zero = bdl.CreateICmpEQ(quotient, ConstantInt::get(i64_ty, 0), "is_zero");
        remaining->addIncoming(quotient, divloop);
        bdl.CreateCondBr(is_zero, revloop, divloop);

        // Reverse-output loop.
        auto* revbody = BasicBlock::Create(m_ctx, "revbody", fn);
        IRBuilder<> brl(revloop);
        auto* ri = brl.CreateLoad(i64_ty, ti_ptr, "ri");
        auto* ri_zero = brl.CreateICmpEQ(ri, ConstantInt::get(i64_ty, 0), "ri_zero");
        brl.CreateCondBr(ri_zero, done_bb, revbody);

        IRBuilder<> brb(revbody);
        auto* ri_dec = brb.CreateSub(ri, ConstantInt::get(i64_ty, 1), "ri_dec");
        auto* ch = brb.CreateLoad(i8_ty, brb.CreateGEP(i8_ty, temp_i8, {ri_dec}), "ch");
        brb.CreateCall(putc_fn, {pos_ptr, buf_arg, size_arg, ch});
        brb.CreateStore(ri_dec, ti_ptr);
        brb.CreateBr(revloop);

        IRBuilder<> bdd(done_bb);
        bdd.CreateRetVoid();
    }

    // ----------------------------------------------------------------------
    // Helper: __ang_fs_format_f64(i64* pos_ptr, i8* buf, i64 size, double val)
    //
    // Basic float formatting: sign, integer part, '.', up to 6 fractional digits.
    // Sufficient for __ang_to_string's "%.15g" use case.
    // InternalLinkage.
    // ----------------------------------------------------------------------
    {
        auto* pos_ptr_ty = PointerType::get(i64_ty, 0);
        auto* fn = createRuntimeFunc("__ang_fs_format_f64",
            FunctionType::get(void_ty, {pos_ptr_ty, i8_ptr, i64_ty, f64_ty}, false));

        auto* pos_ptr = fn->arg_begin();
        auto* buf_arg = fn->arg_begin() + 1;
        auto* size_arg = fn->arg_begin() + 2;
        auto* val_arg  = fn->arg_begin() + 3;
        auto* putc_fn = fs_putc;
        auto* fmt_i64_fn = fs_format_i64;

        auto* entry   = BasicBlock::Create(m_ctx, "entry", fn);
        auto* neg_bb  = BasicBlock::Create(m_ctx, "neg", fn);
        auto* main_bb = BasicBlock::Create(m_ctx, "main", fn);

        IRBuilder<> b(entry);
        auto* is_neg = b.CreateFCmpOLT(val_arg, ConstantFP::get(f64_ty, 0.0), "is_neg");
        b.CreateCondBr(is_neg, neg_bb, main_bb);

        IRBuilder<> bn(neg_bb);
        bn.CreateCall(putc_fn, {pos_ptr, buf_arg, size_arg, ConstantInt::get(i8_ty, '-')});
        auto* neg_val = bn.CreateFNeg(val_arg, "neg_val");
        bn.CreateBr(main_bb);

        IRBuilder<> bm(main_bb);
        auto* abs_val = bm.CreatePHI(f64_ty, 2, "abs_val");
        abs_val->addIncoming(val_arg, entry);
        abs_val->addIncoming(neg_val, neg_bb);

        auto* int_part = bm.CreateFPToSI(abs_val, i64_ty, "int_part");
        auto* frac_part = bm.CreateFSub(abs_val,
            bm.CreateSIToFP(int_part, f64_ty), "frac_part");

        // Format integer part.
        bm.CreateCall(fmt_i64_fn, {pos_ptr, buf_arg, size_arg, int_part});

        // Write decimal point.
        bm.CreateCall(putc_fn, {pos_ptr, buf_arg, size_arg, ConstantInt::get(i8_ty, '.')});

        // Write up to 6 fractional digits.
        auto* fi_ptr = bm.CreateAlloca(i64_ty, nullptr, "fi");
        bm.CreateStore(ConstantInt::get(i64_ty, 0), fi_ptr);
        auto* frac_ptr = bm.CreateAlloca(f64_ty, nullptr, "frac_rem");
        bm.CreateStore(frac_part, frac_ptr);

        auto* floop = BasicBlock::Create(m_ctx, "floop", fn);
        auto* fbody = BasicBlock::Create(m_ctx, "fbody", fn);
        auto* fdone = BasicBlock::Create(m_ctx, "fdone", fn);
        bm.CreateBr(floop);

        IRBuilder<> bfl(floop);
        auto* fi = bfl.CreateLoad(i64_ty, fi_ptr, "fi");
        auto* fi_done = bfl.CreateICmpUGT(fi, ConstantInt::get(i64_ty, 5), "fi_done");
        bfl.CreateCondBr(fi_done, fdone, fbody);

        IRBuilder<> bfb(fbody);
        auto* frac_rem = bfb.CreateLoad(f64_ty, frac_ptr, "frac_rem");
        auto* scaled = bfb.CreateFMul(frac_rem, ConstantFP::get(f64_ty, 10.0), "scaled");
        auto* fdigit = bfb.CreateFPToSI(scaled, i64_ty, "fdigit");
        auto* fdchar = bfb.CreateTrunc(
            bfb.CreateAdd(fdigit, ConstantInt::get(i64_ty, '0')), i8_ty, "fdchar");
        bfb.CreateCall(putc_fn, {pos_ptr, buf_arg, size_arg, fdchar});
        auto* new_frac = bfb.CreateFSub(scaled, bfb.CreateSIToFP(fdigit, f64_ty), "new_frac");
        bfb.CreateStore(new_frac, frac_ptr);
        bfb.CreateStore(bfb.CreateAdd(fi, ConstantInt::get(i64_ty, 1)), fi_ptr);
        bfb.CreateBr(floop);

        IRBuilder<> bfd(fdone);
        bfd.CreateRetVoid();
    }

    // ----------------------------------------------------------------------
    // snprintf(i8* buf, i64 size, i8* fmt, ...) -> i32
    //
    // Minimal variadic formatter supporting: %ld, %d, %u, %s, %c, %.15g, %%.
    // Returns the number of characters written (excluding null terminator).
    //
    // Implementation: walk the format string character by character. On '%',
    // peek the next char(s) to determine the conversion, then use va_arg to
    // read the corresponding variadic argument and format it via the helpers.
    // ----------------------------------------------------------------------
    {
        auto* snprintf_ty = FunctionType::get(i32_ty,
            {i8_ptr, i64_ty, i8_ptr}, /*variadic=*/true);
        auto* fn = define_cfunc("snprintf", snprintf_ty);
        fn->addFnAttr(Attribute::OptimizeNone);
        fn->addFnAttr(Attribute::NoInline);

        auto* buf_arg  = fn->arg_begin();
        auto* size_arg = fn->arg_begin() + 1;
        auto* fmt_arg  = fn->arg_begin() + 2;
        auto* putc_fn = fs_putc;
        auto* fmt_i64_fn = fs_format_i64;

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        // Write position (passed by pointer to __ang_fs_putc).
        auto* pos_ptr = b.CreateAlloca(i64_ty, nullptr, "pos");
        b.CreateStore(ConstantInt::get(i64_ty, 0), pos_ptr);

        // Format string index.
        auto* fi_ptr = b.CreateAlloca(i64_ty, nullptr, "fi");
        b.CreateStore(ConstantInt::get(i64_ty, 0), fi_ptr);

        // va_list for reading variadic arguments.
        auto* va_list_ty = ArrayType::get(i8_ty, 64);  // conservative
        auto* va_list = b.CreateAlloca(va_list_ty, nullptr, "va_list");
        // Declare llvm.va_start / llvm.va_end manually (the intrinsic's overload
        // resolution in getOrInsertDeclaration crashes on some LLVM 22 builds;
        // getOrInsertFunction with the explicit void(ptr) signature is equivalent).
        // The name must be mangled with the pointer overload suffix (.p0).
        auto* vastart = cast<Function>(m_module.getOrInsertFunction("llvm.va_start.p0",
            FunctionType::get(void_ty, {i8_ptr}, false)).getCallee());
        b.CreateCall(vastart, {b.CreateBitCast(va_list, i8_ptr)});

        auto* loop_bb  = BasicBlock::Create(m_ctx, "loop", fn);
        auto* loop2_bb = BasicBlock::Create(m_ctx, "loop2", fn);
        auto* pct_bb   = BasicBlock::Create(m_ctx, "pct", fn);
        auto* lit_bb   = BasicBlock::Create(m_ctx, "lit", fn);
        auto* done_bb  = BasicBlock::Create(m_ctx, "done", fn);
        b.CreateBr(loop_bb);

        // --- Loop: read next fmt char ---
        IRBuilder<> bl(loop_bb);
        auto* fi = bl.CreateLoad(i64_ty, fi_ptr, "fi");
        auto* ch = bl.CreateLoad(i8_ty, bl.CreateGEP(i8_ty, fmt_arg, {fi}), "ch");
        auto* is_null = bl.CreateICmpEQ(ch, ConstantInt::get(i8_ty, 0), "is_null");
        auto* is_pct_c = bl.CreateICmpEQ(ch, ConstantInt::get(i8_ty, '%'), "is_pct_c");
        bl.CreateCondBr(is_null, done_bb, loop2_bb);

        IRBuilder<> bl2(loop2_bb);
        bl2.CreateCondBr(is_pct_c, pct_bb, lit_bb);

        // --- Literal char: write it, advance ---
        IRBuilder<> blit(lit_bb);
        blit.CreateCall(putc_fn, {pos_ptr, buf_arg, size_arg, ch});
        blit.CreateStore(blit.CreateAdd(fi, ConstantInt::get(i64_ty, 1)), fi_ptr);
        blit.CreateBr(loop_bb);

        // --- Percent: peek next char(s) to determine conversion ---
        IRBuilder<> bp(pct_bb);
        auto* next_fi = bp.CreateAdd(fi, ConstantInt::get(i64_ty, 1), "next_fi");
        auto* c1 = bp.CreateLoad(i8_ty, bp.CreateGEP(i8_ty, fmt_arg, {next_fi}), "c1");

        auto* c1_is_l   = bp.CreateICmpEQ(c1, ConstantInt::get(i8_ty, 'l'), "c1_l");
        auto* c1_is_d   = bp.CreateICmpEQ(c1, ConstantInt::get(i8_ty, 'd'), "c1_d");
        auto* c1_is_u   = bp.CreateICmpEQ(c1, ConstantInt::get(i8_ty, 'u'), "c1_u");
        auto* c1_is_s   = bp.CreateICmpEQ(c1, ConstantInt::get(i8_ty, 's'), "c1_s");
        auto* c1_is_c   = bp.CreateICmpEQ(c1, ConstantInt::get(i8_ty, 'c'), "c1_c");
        auto* c1_is_pct = bp.CreateICmpEQ(c1, ConstantInt::get(i8_ty, '%'), "c1_pct");
        auto* c1_is_dot = bp.CreateICmpEQ(c1, ConstantInt::get(i8_ty, '.'), "c1_dot");

        // Dispatch blocks.
        auto* ld_bb  = BasicBlock::Create(m_ctx, "ld", fn);
        auto* u_bb   = BasicBlock::Create(m_ctx, "u", fn);
        auto* s_bb   = BasicBlock::Create(m_ctx, "s", fn);
        auto* c_bb   = BasicBlock::Create(m_ctx, "c", fn);
        auto* pct2_bb = BasicBlock::Create(m_ctx, "pct2", fn);
        auto* dot_bb = BasicBlock::Create(m_ctx, "dot", fn);
        auto* skip_bb = BasicBlock::Create(m_ctx, "skip", fn);  // unknown: skip

        // Dispatch chain: test each specifier in sequence, branching to its
        // handler or falling through to the next test. CreateCondBr returns
        // void, so we use a linear chain of intermediate basic blocks.
        auto* chk_d   = BasicBlock::Create(m_ctx, "chk_d", fn);
        auto* chk_u   = BasicBlock::Create(m_ctx, "chk_u", fn);
        auto* chk_s   = BasicBlock::Create(m_ctx, "chk_s", fn);
        auto* chk_c   = BasicBlock::Create(m_ctx, "chk_c", fn);
        auto* chk_pct = BasicBlock::Create(m_ctx, "chk_pct", fn);
        auto* chk_dot = BasicBlock::Create(m_ctx, "chk_dot", fn);

        bp.CreateCondBr(c1_is_l, ld_bb, chk_d);
        { IRBuilder<>(chk_d).CreateCondBr(c1_is_d, ld_bb, chk_u); }
        { IRBuilder<>(chk_u).CreateCondBr(c1_is_u, u_bb, chk_s); }
        { IRBuilder<>(chk_s).CreateCondBr(c1_is_s, s_bb, chk_c); }
        { IRBuilder<>(chk_c).CreateCondBr(c1_is_c, c_bb, chk_pct); }
        { IRBuilder<>(chk_pct).CreateCondBr(c1_is_pct, pct2_bb, chk_dot); }
        { IRBuilder<>(chk_dot).CreateCondBr(c1_is_dot, dot_bb, skip_bb); }

        // %ld / %d : va_arg i64, format via __ang_fs_format_i64.
        // Advance fi by 2 for %d, 3 for %ld.
        {
            IRBuilder<> bd(ld_bb);
            auto* val = bd.CreateVAArg(va_list, i64_ty, "i64_val");
            bd.CreateCall(fmt_i64_fn, {pos_ptr, buf_arg, size_arg, val});
            // Advance fi: +2 for %d, +3 for %ld.
            auto* adv = bd.CreateSelect(c1_is_l,
                ConstantInt::get(i64_ty, 3), ConstantInt::get(i64_ty, 2), "adv");
            bd.CreateStore(bd.CreateAdd(fi, adv), fi_ptr);
            bd.CreateBr(loop_bb);
        }

        // %u : va_arg i64, format as unsigned (reuse format_i64 — it handles
        // the value as-is; for non-negative u64 values the signed path is fine).
        {
            IRBuilder<> bd(u_bb);
            auto* val = bd.CreateVAArg(va_list, i64_ty, "u64_val");
            bd.CreateCall(fmt_i64_fn, {pos_ptr, buf_arg, size_arg, val});
            bd.CreateStore(bd.CreateAdd(fi, ConstantInt::get(i64_ty, 2)), fi_ptr);
            bd.CreateBr(loop_bb);
        }

        // %s : va_arg i8*, copy char by char.
        {
            IRBuilder<> bd(s_bb);
            auto* str_val = bd.CreateVAArg(va_list, i8_ptr, "str_val");
            bd.CreateStore(bd.CreateAdd(fi, ConstantInt::get(i64_ty, 2)), fi_ptr);
            auto* si_ptr = bd.CreateAlloca(i64_ty, nullptr, "si");
            bd.CreateStore(ConstantInt::get(i64_ty, 0), si_ptr);
            auto* sloop = BasicBlock::Create(m_ctx, "sloop", fn);
            auto* sdone = BasicBlock::Create(m_ctx, "sdone", fn);
            auto* sbody = BasicBlock::Create(m_ctx, "sbody", fn);
            bd.CreateBr(sloop);

            IRBuilder<> bsl(sloop);
            auto* si = bsl.CreateLoad(i64_ty, si_ptr, "si");
            auto* sch = bsl.CreateLoad(i8_ty, bsl.CreateGEP(i8_ty, str_val, {si}), "sch");
            auto* s_null = bsl.CreateICmpEQ(sch, ConstantInt::get(i8_ty, 0), "s_null");
            bsl.CreateCondBr(s_null, sdone, sbody);

            IRBuilder<> bsb(sbody);
            bsb.CreateCall(putc_fn, {pos_ptr, buf_arg, size_arg, sch});
            bsb.CreateStore(bsb.CreateAdd(si, ConstantInt::get(i64_ty, 1)), si_ptr);
            bsb.CreateBr(sloop);

            IRBuilder<> bsd(sdone);
            bsd.CreateBr(loop_bb);
        }

        // %c : va_arg i32, write as single byte.
        {
            IRBuilder<> bd(c_bb);
            auto* val = bd.CreateVAArg(va_list, i32_ty, "c_val");
            bd.CreateCall(putc_fn, {pos_ptr, buf_arg, size_arg, bd.CreateTrunc(val, i8_ty)});
            bd.CreateStore(bd.CreateAdd(fi, ConstantInt::get(i64_ty, 2)), fi_ptr);
            bd.CreateBr(loop_bb);
        }

        // %% : write literal '%'.
        {
            IRBuilder<> bd(pct2_bb);
            bd.CreateCall(putc_fn, {pos_ptr, buf_arg, size_arg, ConstantInt::get(i8_ty, '%')});
            bd.CreateStore(bd.CreateAdd(fi, ConstantInt::get(i64_ty, 2)), fi_ptr);
            bd.CreateBr(loop_bb);
        }

        // %.15g : va_arg double, format via __ang_fs_format_f64.
        // Advance past "%.15g" (5 chars).
        {
            IRBuilder<> bd(dot_bb);
            auto* val = bd.CreateVAArg(va_list, f64_ty, "f64_val");
            auto* fs_fmt_f64 = m_module.getFunction("__ang_fs_format_f64");
            bd.CreateCall(fs_fmt_f64, {pos_ptr, buf_arg, size_arg, val});
            bd.CreateStore(bd.CreateAdd(fi, ConstantInt::get(i64_ty, 5)), fi_ptr);
            bd.CreateBr(loop_bb);
        }

        // Unknown specifier: skip the '%'.
        {
            IRBuilder<> bd(skip_bb);
            bd.CreateStore(bd.CreateAdd(fi, ConstantInt::get(i64_ty, 1)), fi_ptr);
            bd.CreateBr(loop_bb);
        }

        // --- Done: va_end, null-terminate, return count ---
        IRBuilder<> bdd(done_bb);
        auto* vaend = cast<Function>(m_module.getOrInsertFunction("llvm.va_end.p0",
            FunctionType::get(void_ty, {i8_ptr}, false)).getCallee());
        bdd.CreateCall(vaend, {bdd.CreateBitCast(va_list, i8_ptr)});

        auto* final_pos = bdd.CreateLoad(i64_ty, pos_ptr, "final_pos");
        // Null-terminate if within bounds.
        auto* in_bounds = bdd.CreateICmpULT(final_pos, size_arg, "term_bounds");
        auto* term_bb = BasicBlock::Create(m_ctx, "term", fn);
        auto* ret_bb  = BasicBlock::Create(m_ctx, "ret", fn);
        bdd.CreateCondBr(in_bounds, term_bb, ret_bb);

        IRBuilder<> bt(term_bb);
        bt.CreateStore(ConstantInt::get(i8_ty, 0),
            bt.CreateGEP(i8_ty, buf_arg, {final_pos}));
        bt.CreateBr(ret_bb);

        IRBuilder<> br(ret_bb);
        br.CreateRet(br.CreateTrunc(final_pos, i32_ty));
    }

    // __ang_fs_panic: user-overridable panic hook (weak default = no-op).
    // The user provides a strong definition in their C boot stub to intercept
    // runtime errors before the trap.
    {
        auto* panic_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* panic_fn = cast<Function>(
            m_module.getOrInsertFunction("__ang_fs_panic", panic_ty).getCallee());
        panic_fn->setLinkage(Function::WeakODRLinkage);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", panic_fn)).CreateRetVoid();
    }
}

} // namespace angara
