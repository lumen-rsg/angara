// Angara LLVM Backend — Runtime: String Operations
#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {
void RuntimeBuilder::generateStringOps() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto* malloc_fn = m_module.getFunction("malloc");
    auto* strlen_fn = m_module.getFunction("strlen");
    auto* strdup_fn = m_module.getFunction("strdup");
    auto* memcpy_fn = m_module.getFunction("memcpy");

    // Helper lambda: pack an Object* into an AngaraObject with TAG_OBJ
    auto pack_obj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* ptr_i8 = b.CreateBitCast(raw_ptr, i8_ptr);
        auto* ptr_i64 = b.CreatePtrToInt(ptr_i8, i64_ty);
        auto* payload = ptr_i64;
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        result = b.CreateInsertValue(result, payload, {1});
        return result;
    };

    // --- string_from_c(i8* chars) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_string_from_c", fn_ty);
        m_fn_string_from_c = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* chars = fn->arg_begin();

        // strlen
        auto* len = b.CreateCall(strlen_fn, {chars}, "len");

        // Allocate string struct: malloc(sizeof(AngaraString))
        auto* str_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_string_type));
        auto* mem = b.CreateCall(malloc_fn, {str_size}, "mem");
        auto* str_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0), "str_ptr");

        // Set header: obj_type = OBJ_STRING, ref_count = 1
        auto* header_ptr = b.CreateStructGEP(m_string_type, str_ptr, 0);
        auto* type_addr = b.CreateStructGEP(m_obj_header_type, header_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_STRING), type_addr);
        auto* rc_addr = b.CreateStructGEP(m_obj_header_type, header_ptr, 1);
        b.CreateStore(ConstantInt::get(i64_ty, 1), rc_addr);

        // Set length
        auto* len_addr = b.CreateStructGEP(m_string_type, str_ptr, 1);
        b.CreateStore(len, len_addr);

        // Set chars = strdup(chars)
        auto* copied = b.CreateCall(strdup_fn, {chars}, "copied");
        auto* chars_addr = b.CreateStructGEP(m_string_type, str_ptr, 2);
        b.CreateStore(copied, chars_addr);

        // Pack as AngaraObject
        b.CreateRet(pack_obj(b, str_ptr));
    }

    // --- string_concat(AngaraObject a, AngaraObject b) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_string_concat", fn_ty);
        m_fn_string_concat = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* a = fn->arg_begin();
        auto* b_arg = fn->arg_begin() + 1;

        // Extract string a
        auto* a_payload = b.CreateExtractValue(a, {1});
        auto* a_ptr_i64 = b.CreateBitCast(a_payload, i64_ty);
        auto* a_str = b.CreateIntToPtr(a_ptr_i64, PointerType::get(m_ctx, 0));
        auto* a_chars_ptr = b.CreateStructGEP(m_string_type, a_str, 2);
        auto* a_chars = b.CreateLoad(i8_ptr, a_chars_ptr);
        auto* a_len_ptr = b.CreateStructGEP(m_string_type, a_str, 1);
        auto* a_len = b.CreateLoad(i64_ty, a_len_ptr);

        // Extract string b
        auto* b_payload = b.CreateExtractValue(b_arg, {1});
        auto* b_ptr_i64 = b.CreateBitCast(b_payload, i64_ty);
        auto* b_str = b.CreateIntToPtr(b_ptr_i64, PointerType::get(m_ctx, 0));
        auto* b_chars_ptr = b.CreateStructGEP(m_string_type, b_str, 2);
        auto* b_chars = b.CreateLoad(i8_ptr, b_chars_ptr);
        auto* b_len_ptr = b.CreateStructGEP(m_string_type, b_str, 1);
        auto* b_len = b.CreateLoad(i64_ty, b_len_ptr);

        // new_len = a_len + b_len
        auto* new_len = b.CreateAdd(a_len, b_len, "new_len");
        // Allocate: new_len + 1 for null terminator
        auto* buf_size = b.CreateAdd(new_len, ConstantInt::get(i64_ty, 1));
        auto* buf = b.CreateCall(malloc_fn, {buf_size}, "buf");
        // memcpy a
        b.CreateCall(memcpy_fn, {buf, a_chars, a_len});
        // memcpy b
        auto* dest = b.CreateGEP(i8_ty, buf, {a_len});
        b.CreateCall(memcpy_fn, {dest, b_chars, b_len});
        // Null terminator
        auto* null_pos = b.CreateGEP(i8_ty, buf, {new_len});
        b.CreateStore(ConstantInt::get(i8_ty, 0), null_pos);

        // Allocate new string struct
        auto* str_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_string_type));
        auto* mem = b.CreateCall(malloc_fn, {str_size}, "mem");
        auto* str_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        // Header
        auto* header_ptr = b.CreateStructGEP(m_string_type, str_ptr, 0);
        auto* type_addr = b.CreateStructGEP(m_obj_header_type, header_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_STRING), type_addr);
        auto* rc_addr = b.CreateStructGEP(m_obj_header_type, header_ptr, 1);
        b.CreateStore(ConstantInt::get(i64_ty, 1), rc_addr);

        // Length and chars
        auto* len_addr = b.CreateStructGEP(m_string_type, str_ptr, 1);
        b.CreateStore(new_len, len_addr);
        auto* chars_addr = b.CreateStructGEP(m_string_type, str_ptr, 2);
        b.CreateStore(buf, chars_addr);

        b.CreateRet(pack_obj(b, str_ptr));
    }

    // --- to_string(AngaraObject val) -> AngaraObject ---
    // Converts any Angara value to its string representation.
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_to_string", fn_ty);
        m_fn_to_string = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0}, "tag");

        auto* nil_bb = BasicBlock::Create(m_ctx, "nil", fn);
        auto* bool_bb = BasicBlock::Create(m_ctx, "bool", fn);
        auto* i64_bb = BasicBlock::Create(m_ctx, "i64", fn);
        auto* f64_bb = BasicBlock::Create(m_ctx, "f64", fn);
        auto* obj_bb = BasicBlock::Create(m_ctx, "obj", fn);
        auto* str_bb = BasicBlock::Create(m_ctx, "is_string", fn);
        auto* merge_bb = BasicBlock::Create(m_ctx, "merge", fn);

        auto* sw = b.CreateSwitch(tag, nil_bb, 5);
        sw->addCase(ConstantInt::get(i32_ty, TAG_NIL), nil_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), bool_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_I64), i64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_F64), f64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_OBJ), obj_bb);

        // nil -> "nil"
        {
            IRBuilder<> bn(nil_bb);
            auto* gsptr = bn.CreateGlobalString("nil");
            auto* str_from_c = m_module.getFunction("__ang_string_from_c");
            auto* result = bn.CreateCall(str_from_c, {gsptr});
            bn.CreateRet(result);
        }

        // bool -> "true" or "false"
        {
            IRBuilder<> bb(bool_bb);
            auto* payload = bb.CreateExtractValue(val, {1});
            auto* bool_val = bb.CreateTrunc(bb.CreateBitCast(payload, i64_ty), Type::getInt1Ty(m_ctx), "b");
            auto* true_bb = BasicBlock::Create(m_ctx, "true", fn);
            auto* false_bb = BasicBlock::Create(m_ctx, "false", fn);
            bb.CreateCondBr(bool_val, true_bb, false_bb);

            IRBuilder<> bt(true_bb);
            auto* gsptr_t = bt.CreateGlobalString("true");
            auto* str_from_c = m_module.getFunction("__ang_string_from_c");
            bt.CreateRet(bt.CreateCall(str_from_c, {gsptr_t}));

            IRBuilder<> bf(false_bb);
            auto* gsptr_f = bf.CreateGlobalString("false");
            bf.CreateRet(bf.CreateCall(str_from_c, {gsptr_f}));
        }

        // i64 -> snprintf
        {
            IRBuilder<> bi(i64_bb);
            auto* payload = bi.CreateExtractValue(val, {1});
            auto* i64_val = bi.CreateBitCast(payload, i64_ty, "ival");
            // Buffer for int64: max 21 chars
            auto* buf = bi.CreateAlloca(ArrayType::get(i8_ty, 32));
            auto* buf_ptr = bi.CreateBitCast(buf, i8_ptr);
            auto* fmt = bi.CreateGlobalString("%ld");
            auto* snprintf_fn = m_module.getFunction("snprintf");
            bi.CreateCall(snprintf_fn, {buf_ptr, ConstantInt::get(i64_ty, 32), fmt, i64_val});
            auto* str_from_c = m_module.getFunction("__ang_string_from_c");
            bi.CreateRet(bi.CreateCall(str_from_c, {buf_ptr}));
        }

        // f64 -> snprintf
        {
            IRBuilder<> bf(f64_bb);
            auto* payload = bf.CreateExtractValue(val, {1});
            auto* f64_val = bf.CreateBitCast(payload, Type::getDoubleTy(m_ctx), "dval");
            auto* buf = bf.CreateAlloca(ArrayType::get(i8_ty, 64));
            auto* buf_ptr = bf.CreateBitCast(buf, i8_ptr);
            auto* fmt = bf.CreateGlobalString("%.15g");
            auto* snprintf_fn = m_module.getFunction("snprintf");
            bf.CreateCall(snprintf_fn, {buf_ptr, ConstantInt::get(i64_ty, 64), fmt, f64_val});
            auto* str_from_c = m_module.getFunction("__ang_string_from_c");
            bf.CreateRet(bf.CreateCall(str_from_c, {buf_ptr}));
        }

        // obj -> check if string, otherwise "<object>"
        {
            IRBuilder<> bo(obj_bb);
            auto* payload = bo.CreateExtractValue(val, {1});
            auto* ptr_i64 = bo.CreateBitCast(payload, i64_ty);
            auto* obj_ptr = bo.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
            auto* obj_type_addr = bo.CreateStructGEP(m_obj_header_type, obj_ptr, 0);
            auto* obj_type = bo.CreateLoad(i32_ty, obj_type_addr, "obj_type");
            auto* is_str = bo.CreateICmpEQ(obj_type, ConstantInt::get(i32_ty, OBJ_STRING));
            auto* not_str_bb = BasicBlock::Create(m_ctx, "not_str", fn);
            bo.CreateCondBr(is_str, str_bb, not_str_bb);

            IRBuilder<> bs(str_bb);
            // Return the same string (but we should incref it)
            auto* incref_fn = m_module.getFunction("__ang_incref");
            bs.CreateCall(incref_fn, {val});
            bs.CreateRet(val);

            IRBuilder<> bns(not_str_bb);
            auto* gsptr = bns.CreateGlobalString("<object>");
            auto* str_from_c = m_module.getFunction("__ang_string_from_c");
            bns.CreateRet(bns.CreateCall(str_from_c, {gsptr}));
        }

        // merge (unreachable for now, but needed for phi)
        {
            IRBuilder<> bm(merge_bb);
            bm.CreateRet(UndefValue::get(obj_ty));
        }
    }
}

} // namespace angara
