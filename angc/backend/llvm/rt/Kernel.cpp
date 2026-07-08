#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {

// ============================================================================
// generateKernelRuntime — the Linux-kernel-module runtime subset.
//
// Unlike --freestanding (which stubs collections/strings/IO to no-ops), --kernel
// keeps the FULL useful runtime so Angara records/lists/strings/conversions work
// in-kernel. It only drops the generators that pull in libc symbols the kernel
// does not provide:
//   - generateExceptionOps  → setjmp/longjmp/printf/exit (exceptions are hard-
//                             errored in the type-checker under --kernel).
//   - generateThreadOps     → pthread_* (spawn/Mutex are hard-errored).
//   - generateIOOps         → glibc-internal _IO_2_1_stdout_/stderr_/stdin_.
//
// IO is provided instead by generateKernelIO, which converts the value to a C
// string (via __ang_to_string) and calls an external angara_kernel_print*
// symbol supplied by the kernel libc shim (kernel/kernel_runtime.c → printk).
//
// Allocation note: every malloc/realloc/free the kept generators emit resolves
// to the single module-level declaration from declareCLibFunctions. The kernel
// shim maps those three to kmalloc/krealloc/kfree, so ALL allocation paths land
// in the kernel allocator uniformly — no per-site codegen change required.
// ============================================================================

void RuntimeBuilder::generateKernelRuntime() {
    declareCLibFunctions();

    generateMemoryManagement();
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
    generateMiscOps();

    // Exception stubs: generateExceptionOps is skipped (no setjmp/longjmp/
    // printf/exit in-kernel, and try/catch/throw are hard-errored in the
    // type-checker). But generateModuleAPIVTable's __ang_api_throw_error calls
    // __ang_exception_new + __ang_throw by name (ModuleAPI.cpp:228-229), so
    // those two must exist as no-op stubs or getFunction() returns null and
    // CreateCall(null) segfaults at codegen time. We stub the full exception
    // surface (mirroring generateFreestandingStubs) for forward-safety.
    stubKernelExceptions();

    // Kernel IO replaces generateIOOps (no glibc FILE symbols).
    generateKernelIO();

    generateModuleAPIVTable();

    // Deliberately NOT called: generateExceptionOps, generateThreadOps,
    // generateIOOps. See file header.
}

// ---------------------------------------------------------------------------
// stubKernelExceptions — emit no-op stubs for the exception runtime symbols
// that other kept generators (notably generateModuleAPIVTable) reference by
// name. Mirrors the exception subset of generateFreestandingStubs. throw is a
// no-op (no longjmp), exception_new returns nil, try_begin returns 0.
// ---------------------------------------------------------------------------

void RuntimeBuilder::stubKernelExceptions() {
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

    // __ang_exception_new(msg) -> nil  (no-op; exceptions hard-errored upstream)
    {
        auto* fn = createRuntimeFunc("__ang_exception_new",
            FunctionType::get(obj_ty, {obj_ty}, false));
        m_fn_exception_new = FunctionCallee(fn);
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        b.CreateRet(make_nil(b));
    }
    // __ang_exception_get_message(exc) -> nil
    {
        auto* fn = createRuntimeFunc("__ang_exception_get_message",
            FunctionType::get(obj_ty, {obj_ty}, false));
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        b.CreateRet(make_nil(b));
    }
    // __ang_throw(exc) -> void  (no-op; no longjmp in kernel)
    {
        auto* fn = createRuntimeFunc("__ang_throw",
            FunctionType::get(void_ty, {obj_ty}, false));
        m_fn_throw = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRetVoid();
    }
    // __ang_try_begin(jmp_buf*) -> 0  (never throws; returns "not from longjmp")
    {
        auto* fn = createRuntimeFunc("__ang_try_begin",
            FunctionType::get(i32_ty, {PointerType::get(m_ctx, 0)}, false));
        m_fn_try_begin = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn))
            .CreateRet(ConstantInt::get(i32_ty, 0));
    }
    // __ang_try_end() -> void  (no-op)
    {
        auto* fn = createRuntimeFunc("__ang_try_end",
            FunctionType::get(void_ty, {}, false));
        m_fn_try_end = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRetVoid();
    }
}

// ---------------------------------------------------------------------------
// generateKernelIO — print/println/write → angara_kernel_print* (shim → printk);
// flush is a no-op; read_line/read_all return nil (no kernel stdin).
//
// Signatures match generateIOOps / the freestanding stubs exactly so the
// m_fn_io_* members populate and codegen calls resolve.
// ---------------------------------------------------------------------------

void RuntimeBuilder::generateKernelIO() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i32_ty  = Type::getInt32Ty(m_ctx);
    auto* i64_ty  = Type::getInt64Ty(m_ctx);
    auto* i8_ptr  = PointerType::get(m_ctx, 0);
    auto* obj_ty  = m_angara_obj_type;

    auto make_nil = [&](IRBuilder<>& b) -> Value* {
        Value* v = UndefValue::get(obj_ty);
        v = b.CreateInsertValue(v, ConstantInt::get(i32_ty, TAG_NIL), {0});
        v = b.CreateInsertValue(v, ConstantInt::get(i64_ty, 0), {1});
        return v;
    };

    // Extract the char* from an Angara string object (obj payload → string
    // header → chars field at index 3). Mirrors generateIOOps::get_cstr.
    auto get_cstr = [&](IRBuilder<>& b, Value* str_obj) -> Value* {
        auto* payload  = b.CreateExtractValue(str_obj, {1});
        auto* ptr_i64  = b.CreateBitCast(payload, i64_ty);
        auto* str_ptr  = b.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* chars_ptr = b.CreateStructGEP(m_string_type, str_ptr, 3);
        return b.CreateLoad(i8_ptr, chars_ptr, "cstr");
    };

    // Stream id convention shared with hosted IO: payload 2 == stderr.
    // The shim routes the _err variant to printk(KERN_ERR).
    auto stream_is_err = [&](IRBuilder<>& b, Value* stream_arg) -> Value* {
        auto* stream_payload = b.CreateExtractValue(stream_arg, {1});
        auto* stream_id = b.CreateBitCast(stream_payload, i64_ty, "stream_id");
        return b.CreateICmpEQ(stream_id, ConstantInt::get(i64_ty, 2));
    };

    // Declare the shim entry points (provided by kernel/kernel_runtime.c).
    //   void angara_kernel_print(const char* s)        → printk(KERN_INFO)
    //   void angara_kernel_println(const char* s)      → printk(KERN_INFO "\n")
    //   void angara_kernel_print_err(const char* s)    → printk(KERN_ERR)
    auto print_fn_ty     = FunctionType::get(void_ty, {i8_ptr}, false);
    auto* print_fn       = cast<Function>(
        m_module.getOrInsertFunction("angara_kernel_print", print_fn_ty).getCallee());
    auto* println_fn     = cast<Function>(
        m_module.getOrInsertFunction("angara_kernel_println", print_fn_ty).getCallee());
    auto* print_err_fn   = cast<Function>(
        m_module.getOrInsertFunction("angara_kernel_print_err", print_fn_ty).getCallee());
    print_fn->setLinkage(Function::ExternalLinkage);
    println_fn->setLinkage(Function::ExternalLinkage);
    print_err_fn->setLinkage(Function::ExternalLinkage);

    auto* to_str_fn = m_module.getFunction("__ang_to_string");  // emitted by generateStringOps

    // --- __ang_io_print(stream, val) : convert val to string, print ---
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_io_print", fn_ty);
        m_fn_io_print = FunctionCallee(fn);

        auto* entry    = BasicBlock::Create(m_ctx, "entry", fn);
        auto* info_bb  = BasicBlock::Create(m_ctx, "info", fn);
        auto* err_bb   = BasicBlock::Create(m_ctx, "err", fn);
        IRBuilder<> b(entry);
        auto* stream_arg = fn->arg_begin();
        auto* val        = fn->arg_begin() + 1;
        auto* str_obj = b.CreateCall(to_str_fn, {val}, "str");
        auto* cstr = get_cstr(b, str_obj);
        b.CreateCondBr(stream_is_err(b, stream_arg), err_bb, info_bb);

        IRBuilder<> bi(info_bb);
        bi.CreateCall(print_fn, {cstr});
        bi.CreateRetVoid();

        IRBuilder<> be(err_bb);
        be.CreateCall(print_err_fn, {cstr});
        be.CreateRetVoid();
    }

    // --- __ang_io_println(stream, val) ---
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_io_println", fn_ty);
        m_fn_io_println = FunctionCallee(fn);

        auto* entry    = BasicBlock::Create(m_ctx, "entry", fn);
        auto* info_bb  = BasicBlock::Create(m_ctx, "info", fn);
        auto* err_bb   = BasicBlock::Create(m_ctx, "err", fn);
        IRBuilder<> b(entry);
        auto* stream_arg = fn->arg_begin();
        auto* val        = fn->arg_begin() + 1;
        auto* str_obj = b.CreateCall(to_str_fn, {val}, "str");
        auto* cstr = get_cstr(b, str_obj);
        b.CreateCondBr(stream_is_err(b, stream_arg), err_bb, info_bb);

        IRBuilder<> bi(info_bb);
        bi.CreateCall(println_fn, {cstr});
        bi.CreateRetVoid();

        IRBuilder<> be(err_bb);
        be.CreateCall(print_err_fn, {cstr});
        be.CreateRetVoid();
    }

    // --- __ang_io_write(stream, content) : same as print (no format) ---
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_io_write", fn_ty);
        m_fn_io_write = FunctionCallee(fn);

        auto* entry    = BasicBlock::Create(m_ctx, "entry", fn);
        auto* info_bb  = BasicBlock::Create(m_ctx, "info", fn);
        auto* err_bb   = BasicBlock::Create(m_ctx, "err", fn);
        IRBuilder<> b(entry);
        auto* stream_arg = fn->arg_begin();
        auto* content    = fn->arg_begin() + 1;
        auto* str_obj = b.CreateCall(to_str_fn, {content}, "str");
        auto* cstr = get_cstr(b, str_obj);
        b.CreateCondBr(stream_is_err(b, stream_arg), err_bb, info_bb);

        IRBuilder<> bi(info_bb);
        bi.CreateCall(print_fn, {cstr});
        bi.CreateRetVoid();

        IRBuilder<> be(err_bb);
        be.CreateCall(print_err_fn, {cstr});
        be.CreateRetVoid();
    }

    // --- __ang_io_flush(stream) : no-op (printk is unbuffered) ---
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_io_flush", fn_ty);
        m_fn_io_flush = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRetVoid();
    }

    // --- __ang_io_read_line() / __ang_io_read_all() : return nil ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_io_read_line", fn_ty);
        m_fn_io_read_line = FunctionCallee(fn);
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        b.CreateRet(make_nil(b));
    }
    {
        auto* fn_ty = FunctionType::get(obj_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_io_read_all", fn_ty);
        m_fn_io_read_all = FunctionCallee(fn);
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        b.CreateRet(make_nil(b));
    }
}

} // namespace angara
