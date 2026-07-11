#include "RuntimeBuilder.h"
#include <llvm/IR/Intrinsics.h>

using namespace llvm;

namespace angara {
void RuntimeBuilder::generateFreestandingStubs() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* obj_ty = m_angara_obj_type;
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

    // No allocator layer. The type checker hard-rejects every heap feature in
    // --freestanding mode (E915 strings, E916 lists, E917 records), so no
    // source construct can allocate and generateMemoryManagement() would only
    // emit dead functions + three external libc symbols (malloc/realloc/free)
    // that bare-metal linker scripts would otherwise have to stub. The one
    // runtime global codegen still touches — __ang_exception_chain, used by
    // emitRtPushFrame/emitRtPopFrame's exception-chain snapshot — is created by
    // generateTypes(), which runs before this early-return, so frame push/pop
    // keep working without the memory layer.

    {
        auto* fn = createRuntimeFunc("__ang_equals", FunctionType::get(obj_ty, {obj_ty, obj_ty}, false));
        m_fn_equals = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* pa = b.CreateExtractValue(fn->arg_begin(), {1});
        auto* pb = b.CreateExtractValue(fn->arg_begin() + 1, {1});
        auto* eq = b.CreateICmpEQ(pa, pb);
        auto* ext = b.CreateZExt(eq, i64_ty);
        Value* r = UndefValue::get(obj_ty);
        r = b.CreateInsertValue(r, ConstantInt::get(i32_ty, TAG_BOOL), {0});
        r = b.CreateInsertValue(r, ext, {1});
        b.CreateRet(r);
    }
    stub_nil("__ang_exception_new", FunctionType::get(obj_ty, {obj_ty}, false), m_fn_exception_new);
    stub_void("__ang_throw", FunctionType::get(void_ty, {obj_ty}, false), m_fn_throw);
    {
        auto* fn = createRuntimeFunc("__ang_try_begin", FunctionType::get(i32_ty, {PointerType::get(m_ctx, 0)}, false));
        m_fn_try_begin = FunctionCallee(fn);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(e).CreateRet(ConstantInt::get(i32_ty, 0));
    }
    stub_void("__ang_try_end", FunctionType::get(void_ty, {}, false), m_fn_try_end);
    stub_nil("__ang_string_from_c", FunctionType::get(obj_ty, {PointerType::get(m_ctx, 0)}, false), m_fn_string_from_c);
    stub_nil("__ang_string_concat", FunctionType::get(obj_ty, {obj_ty, obj_ty}, false), m_fn_string_concat);
    stub_nil("__ang_string_repeat", FunctionType::get(obj_ty, {obj_ty, obj_ty}, false), m_fn_string_repeat);
    stub_nil("__ang_to_string", FunctionType::get(obj_ty, {obj_ty}, false), m_fn_to_string);
    stub_nil("__ang_char_to_string", FunctionType::get(obj_ty, {obj_ty}, false), m_fn_char_to_string);  // LANG-4
    stub_nil("__ang_record_new", FunctionType::get(obj_ty, {}, false), m_fn_record_new);
    stub_nil("__ang_record_get", FunctionType::get(obj_ty, {obj_ty, PointerType::get(m_ctx, 0)}, false), m_fn_record_get);
    stub_void("__ang_record_set", FunctionType::get(void_ty, {obj_ty, PointerType::get(m_ctx, 0), obj_ty}, false), m_fn_record_set);
    stub_nil("__ang_len", FunctionType::get(obj_ty, {obj_ty}, false), m_fn_len);
    stub_void("__ang_io_print", FunctionType::get(void_ty, {obj_ty, obj_ty}, false), m_fn_io_print);
    stub_void("__ang_io_println", FunctionType::get(void_ty, {obj_ty, obj_ty}, false), m_fn_io_println);
    stub_void("__ang_io_write", FunctionType::get(void_ty, {obj_ty, obj_ty}, false), m_fn_io_write);
    stub_void("__ang_io_flush", FunctionType::get(void_ty, {obj_ty}, false), m_fn_io_flush);
    stub_nil("__ang_io_read_line", FunctionType::get(obj_ty, {}, false), m_fn_io_read_line);
    stub_nil("__ang_io_read_all", FunctionType::get(obj_ty, {}, false), m_fn_io_read_all);
    stub_nil("__ang_list_new", FunctionType::get(obj_ty, {}, false), m_fn_list_new);
    stub_nil("__ang_list_get", FunctionType::get(obj_ty, {obj_ty, obj_ty}, false), m_fn_list_get);
    stub_void("__ang_list_push", FunctionType::get(void_ty, {obj_ty, obj_ty}, false), m_fn_list_push);

    // Shift/division/modulo bounds checks (H12) route runtime errors here. Bare
    // metal has no exception machinery, so a runtime error is a hard fault —
    // emit llvm.trap so the CPU halts cleanly instead of calling an undefined
    // symbol. Signature: void(ptr msg).
    {
        auto* i8_ptr = PointerType::get(m_ctx, 0);
        auto* fn = createRuntimeFunc("__ang_api_throw_error",
            FunctionType::get(void_ty, {i8_ptr}, false));
        auto* e = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(e);
        auto* trap = Intrinsic::getOrInsertDeclaration(&m_module, Intrinsic::trap);
        b.CreateCall(trap, {});
        b.CreateUnreachable();
    }
}

} // namespace angara
