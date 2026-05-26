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

    auto pack_obj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* ptr_i8 = b.CreateBitCast(raw_ptr, i8_ptr);
        auto* ptr_i64 = b.CreatePtrToInt(ptr_i8, i64_ty);
        auto* payload = ptr_i64;
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        result = b.CreateInsertValue(result, payload, {1});
        return result;
    };

    {
        auto* fn_ty = FunctionType::get(obj_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_string_from_c", fn_ty);
        m_fn_string_from_c = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* chars = fn->arg_begin();

        auto* len = b.CreateCall(strlen_fn, {chars}, "len");

        auto* str_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_string_type));
        auto* mem = b.CreateCall(malloc_fn, {str_size}, "mem");
        auto* str_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0), "str_ptr");

        auto* header_ptr = b.CreateStructGEP(m_string_type, str_ptr, 0);
        auto* type_addr = b.CreateStructGEP(m_obj_header_type, header_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_STRING), type_addr);
        auto* rc_addr = b.CreateStructGEP(m_obj_header_type, header_ptr, 1);
        b.CreateStore(ConstantInt::get(i64_ty, 1), rc_addr);

        auto* len_addr = b.CreateStructGEP(m_string_type, str_ptr, 1);
        b.CreateStore(len, len_addr);

        auto* copied = b.CreateCall(strdup_fn, {chars}, "copied");
        auto* chars_addr = b.CreateStructGEP(m_string_type, str_ptr, 2);
        b.CreateStore(copied, chars_addr);

        b.CreateRet(pack_obj(b, str_ptr));
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_string_concat", fn_ty);
        m_fn_string_concat = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* a = fn->arg_begin();
        auto* b_arg = fn->arg_begin() + 1;

        auto* a_payload = b.CreateExtractValue(a, {1});
        auto* a_ptr_i64 = b.CreateBitCast(a_payload, i64_ty);
        auto* a_str = b.CreateIntToPtr(a_ptr_i64, PointerType::get(m_ctx, 0));
        auto* a_chars_ptr = b.CreateStructGEP(m_string_type, a_str, 2);
        auto* a_chars = b.CreateLoad(i8_ptr, a_chars_ptr);
        auto* a_len_ptr = b.CreateStructGEP(m_string_type, a_str, 1);
        auto* a_len = b.CreateLoad(i64_ty, a_len_ptr);

        auto* b_payload = b.CreateExtractValue(b_arg, {1});
        auto* b_ptr_i64 = b.CreateBitCast(b_payload, i64_ty);
        auto* b_str = b.CreateIntToPtr(b_ptr_i64, PointerType::get(m_ctx, 0));
        auto* b_chars_ptr = b.CreateStructGEP(m_string_type, b_str, 2);
        auto* b_chars = b.CreateLoad(i8_ptr, b_chars_ptr);
        auto* b_len_ptr = b.CreateStructGEP(m_string_type, b_str, 1);
        auto* b_len = b.CreateLoad(i64_ty, b_len_ptr);

        auto* new_len = b.CreateAdd(a_len, b_len, "new_len");
        auto* buf_size = b.CreateAdd(new_len, ConstantInt::get(i64_ty, 1));
        auto* buf = b.CreateCall(malloc_fn, {buf_size}, "buf");
        b.CreateCall(memcpy_fn, {buf, a_chars, a_len});
        auto* dest = b.CreateGEP(i8_ty, buf, {a_len});
        b.CreateCall(memcpy_fn, {dest, b_chars, b_len});
        auto* null_pos = b.CreateGEP(i8_ty, buf, {new_len});
        b.CreateStore(ConstantInt::get(i8_ty, 0), null_pos);

        auto* str_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_string_type));
        auto* mem = b.CreateCall(malloc_fn, {str_size}, "mem");
        auto* str_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_string_type, str_ptr, 0);
        auto* type_addr = b.CreateStructGEP(m_obj_header_type, header_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_STRING), type_addr);
        auto* rc_addr = b.CreateStructGEP(m_obj_header_type, header_ptr, 1);
        b.CreateStore(ConstantInt::get(i64_ty, 1), rc_addr);

        auto* len_addr = b.CreateStructGEP(m_string_type, str_ptr, 1);
        b.CreateStore(new_len, len_addr);
        auto* chars_addr = b.CreateStructGEP(m_string_type, str_ptr, 2);
        b.CreateStore(buf, chars_addr);

        b.CreateRet(pack_obj(b, str_ptr));
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_string_repeat", fn_ty);
        m_fn_string_repeat = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* zero_bb = BasicBlock::Create(m_ctx, "zero", fn);
        auto* prep_bb = BasicBlock::Create(m_ctx, "prep", fn);
        auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
        auto* body_bb = BasicBlock::Create(m_ctx, "body", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);
        IRBuilder<> b(entry);

        auto* str_arg = fn->arg_begin();
        auto* count_arg = fn->arg_begin() + 1;

        auto* count_payload = b.CreateExtractValue(count_arg, {1});
        auto* count_val = b.CreateBitCast(count_payload, i64_ty, "count");

        auto* is_non_positive = b.CreateICmpSLE(count_val, ConstantInt::get(i64_ty, 0));
        b.CreateCondBr(is_non_positive, zero_bb, prep_bb);

        {
            IRBuilder<> bz(zero_bb);
            auto* gsptr = bz.CreateGlobalString("");
            auto* str_from_c = m_module.getFunction("__ang_string_from_c");
            bz.CreateRet(bz.CreateCall(str_from_c, {gsptr}));
        }

        IRBuilder<> bp(prep_bb);
        auto* payload = bp.CreateExtractValue(str_arg, {1});
        auto* ptr_i64 = bp.CreateBitCast(payload, i64_ty);
        auto* str_ptr = bp.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* chars_ptr = bp.CreateStructGEP(m_string_type, str_ptr, 2);
        auto* src_chars = bp.CreateLoad(i8_ptr, chars_ptr, "src_chars");
        auto* len_ptr = bp.CreateStructGEP(m_string_type, str_ptr, 1);
        auto* src_len = bp.CreateLoad(i64_ty, len_ptr, "src_len");

        auto* new_len = bp.CreateMul(src_len, count_val, "new_len");
        auto* buf_size = bp.CreateAdd(new_len, ConstantInt::get(i64_ty, 1));
        auto* buf = bp.CreateCall(malloc_fn, {buf_size}, "buf");
        bp.CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* i_phi = bl.CreatePHI(i64_ty, 2, "i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), prep_bb);
        auto* cont = bl.CreateICmpSLT(i_phi, count_val);
        bl.CreateCondBr(cont, body_bb, done_bb);

        {
            IRBuilder<> bb(body_bb);
            auto* offset = bb.CreateMul(i_phi, src_len);
            auto* dest = bb.CreateGEP(i8_ty, buf, {offset});
            bb.CreateCall(memcpy_fn, {dest, src_chars, src_len});
            auto* next = bb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            bb.CreateBr(loop_bb);
            i_phi->addIncoming(next, body_bb);
        }

        {
            IRBuilder<> bd(done_bb);
            auto* null_pos = bd.CreateGEP(i8_ty, buf, {new_len});
            bd.CreateStore(ConstantInt::get(i8_ty, 0), null_pos);

            auto* str_size = ConstantInt::get(i64_ty,
                m_module.getDataLayout().getTypeAllocSize(m_string_type));
            auto* mem = bd.CreateCall(malloc_fn, {str_size}, "mem");
            auto* new_str_ptr = bd.CreateBitCast(mem, PointerType::get(m_ctx, 0));

            auto* header_ptr = bd.CreateStructGEP(m_string_type, new_str_ptr, 0);
            auto* type_addr = bd.CreateStructGEP(m_obj_header_type, header_ptr, 0);
            bd.CreateStore(ConstantInt::get(i32_ty, OBJ_STRING), type_addr);
            auto* rc_addr = bd.CreateStructGEP(m_obj_header_type, header_ptr, 1);
            bd.CreateStore(ConstantInt::get(i64_ty, 1), rc_addr);

            auto* len_addr = bd.CreateStructGEP(m_string_type, new_str_ptr, 1);
            bd.CreateStore(new_len, len_addr);
            auto* chars_addr = bd.CreateStructGEP(m_string_type, new_str_ptr, 2);
            bd.CreateStore(buf, chars_addr);

            bd.CreateRet(pack_obj(bd, new_str_ptr));
        }
    }

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

        {
            IRBuilder<> bn(nil_bb);
            auto* gsptr = bn.CreateGlobalString("nil");
            auto* str_from_c = m_module.getFunction("__ang_string_from_c");
            auto* result = bn.CreateCall(str_from_c, {gsptr});
            bn.CreateRet(result);
        }

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

        {
            IRBuilder<> bi(i64_bb);
            auto* payload = bi.CreateExtractValue(val, {1});
            auto* i64_val = bi.CreateBitCast(payload, i64_ty, "ival");
            auto* buf = bi.CreateAlloca(ArrayType::get(i8_ty, 32));
            auto* buf_ptr = bi.CreateBitCast(buf, i8_ptr);
            auto* fmt = bi.CreateGlobalString("%ld");
            auto* snprintf_fn = m_module.getFunction("snprintf");
            bi.CreateCall(snprintf_fn, {buf_ptr, ConstantInt::get(i64_ty, 32), fmt, i64_val});
            auto* str_from_c = m_module.getFunction("__ang_string_from_c");
            bi.CreateRet(bi.CreateCall(str_from_c, {buf_ptr}));
        }

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
            auto* incref_fn = m_module.getFunction("__ang_incref");
            bs.CreateCall(incref_fn, {val});
            bs.CreateRet(val);

            IRBuilder<> bns(not_str_bb);
            auto* gsptr = bns.CreateGlobalString("<object>");
            auto* str_from_c = m_module.getFunction("__ang_string_from_c");
            bns.CreateRet(bns.CreateCall(str_from_c, {gsptr}));
        }

        {
            IRBuilder<> bm(merge_bb);
            bm.CreateRet(UndefValue::get(obj_ty));
        }
    }
}

} // namespace angara
