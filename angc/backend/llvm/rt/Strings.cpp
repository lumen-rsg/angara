#include "RuntimeBuilder.h"
#include "MarkSweepGC.h"

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
    auto* realloc_fn = m_module.getFunction("realloc");
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

    // Helper: initialize a newly allocated AngaraString struct
    auto init_string_struct = [&](IRBuilder<>& b, Value* str_ptr, Value* len, Value* chars) {
        // Note: gc_alloc already initializes ObjHeader (type, meta, next).
        // Only set string-specific fields here.
        b.CreateStore(len, b.CreateStructGEP(m_string_type, str_ptr, 1));
        b.CreateStore(len, b.CreateStructGEP(m_string_type, str_ptr, 2)); // capacity = length
        b.CreateStore(chars, b.CreateStructGEP(m_string_type, str_ptr, 3));
    };

    // __ang_string_from_c: create an AngaraString from a C string (copies the data)
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
        auto* gc_alloc_fn = m_module.getFunction("__ang_gc_alloc");
        auto* str_ptr = b.CreateCall(gc_alloc_fn,
            {str_size, ConstantInt::get(i32_ty, OBJ_STRING)}, "str_mem");

        init_string_struct(b, str_ptr, len, b.CreateCall(strdup_fn, {chars}, "copied"));

        b.CreateRet(pack_obj(b, str_ptr));
    }

    // __ang_string_take_c: adopts a C char* into an AngaraString WITHOUT copying.
    {
        auto* fn_ty = FunctionType::get(obj_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_string_take_c", fn_ty);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* chars = fn->arg_begin();

        auto* len = b.CreateCall(strlen_fn, {chars}, "len");
        auto* str_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_string_type));
        auto* gc_alloc_fn = m_module.getFunction("__ang_gc_alloc");
        auto* str_ptr = b.CreateCall(gc_alloc_fn,
            {str_size, ConstantInt::get(i32_ty, OBJ_STRING)}, "str_mem");

        init_string_struct(b, str_ptr, len, chars);

        b.CreateRet(pack_obj(b, str_ptr));
    }

    // Pre-declare __ang_to_string so the concat slow-path can reference it.
    // The body will be filled in by generateConversions() later.
    {
        auto* to_str_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        m_module.getOrInsertFunction("__ang_to_string", to_str_ty);
    }

    // __ang_string_concat: concatenate two strings.
    // When the left operand has ref_count == 1 (unique ownership), performs an
    // in-place realloc + append instead of allocate + copy.
    // Safely handles non-string operands by converting via __ang_to_string.
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_string_concat", fn_ty);
        m_fn_string_concat = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* slow_bb = BasicBlock::Create(m_ctx, "slow", fn);
        auto* check_unique_bb = BasicBlock::Create(m_ctx, "check_unique", fn);
        auto* inplace_bb = BasicBlock::Create(m_ctx, "inplace", fn);
        auto* inplace_grow_bb = BasicBlock::Create(m_ctx, "inplace_grow", fn);
        auto* inplace_append_bb = BasicBlock::Create(m_ctx, "inplace_append", fn);
        auto* copy_bb = BasicBlock::Create(m_ctx, "copy", fn);

        IRBuilder<> b(entry);
        auto* a = fn->arg_begin();
        auto* b_arg = fn->arg_begin() + 1;

        // Verify both operands are tagged OBJ with OBJ_STRING type before
        // accessing any string-specific fields.
        auto* a_tag = b.CreateExtractValue(a, {0}, "a_tag");
        auto* b_tag = b.CreateExtractValue(b_arg, {0}, "b_tag");
        auto* both_obj = b.CreateAnd(
            b.CreateICmpEQ(a_tag, ConstantInt::get(i32_ty, TAG_OBJ)),
            b.CreateICmpEQ(b_tag, ConstantInt::get(i32_ty, TAG_OBJ)));

        auto* a_payload = b.CreateExtractValue(a, {1});
        auto* a_str = b.CreateIntToPtr(b.CreateBitCast(a_payload, i64_ty),
                                        PointerType::get(m_ctx, 0));
        auto* b_payload = b.CreateExtractValue(b_arg, {1});
        auto* b_str = b.CreateIntToPtr(b.CreateBitCast(b_payload, i64_ty),
                                        PointerType::get(m_ctx, 0));

        auto* a_obj_type = b.CreateLoad(i32_ty,
            b.CreateStructGEP(m_obj_header_type, a_str, 0), "a_obj_type");
        auto* b_obj_type = b.CreateLoad(i32_ty,
            b.CreateStructGEP(m_obj_header_type, b_str, 0), "b_obj_type");
        auto* both_string = b.CreateAnd(
            b.CreateICmpEQ(a_obj_type, ConstantInt::get(i32_ty, OBJ_STRING)),
            b.CreateICmpEQ(b_obj_type, ConstantInt::get(i32_ty, OBJ_STRING)));
        auto* both_valid = b.CreateAnd(both_obj, both_string);
        b.CreateCondBr(both_valid, check_unique_bb, slow_bb);

        // --- Slow path: convert non-string operands via __ang_to_string ---
        {
            IRBuilder<> bs(slow_bb);
            auto* to_str_fn = m_module.getFunction("__ang_to_string");
            auto* a_str_obj = bs.CreateCall(to_str_fn, {a}, "a_str");
            auto* b_str_obj = bs.CreateCall(to_str_fn, {b_arg}, "b_str");
            auto* result = bs.CreateCall(fn, {a_str_obj, b_str_obj}, "result");
            bs.CreateRet(result);
        }

        // --- Both confirmed as strings: check is_unique for in-place ---
        {
            IRBuilder<> bu(check_unique_bb);
            auto* b_chars = bu.CreateLoad(i8_ptr,
                bu.CreateStructGEP(m_string_type, b_str, 3), "b_chars");
            auto* b_len = bu.CreateLoad(i64_ty,
                bu.CreateStructGEP(m_string_type, b_str, 1), "b_len");

            // Extract is_unique bit from ObjHeader.meta (bit 8)
            auto* meta = bu.CreateLoad(i32_ty,
                bu.CreateStructGEP(m_obj_header_type, a_str, 1), "a_meta");
            auto* unique_bit = bu.CreateAnd(
                bu.CreateLShr(meta, ConstantInt::get(i32_ty, 8)),
                ConstantInt::get(i32_ty, 1), "unique_bit");
            auto* is_unique = bu.CreateICmpNE(unique_bit, ConstantInt::get(i32_ty, 0));
            bu.CreateCondBr(is_unique, inplace_bb, copy_bb);

            // --- In-place: check if buffer needs growth ---
            {
                IRBuilder<> bi(inplace_bb);
                auto* a_len = bi.CreateLoad(i64_ty,
                    bi.CreateStructGEP(m_string_type, a_str, 1), "a_len");
                auto* a_cap = bi.CreateLoad(i64_ty,
                    bi.CreateStructGEP(m_string_type, a_str, 2), "a_cap");
                auto* new_len = bi.CreateAdd(a_len, b_len, "new_len");
                bi.CreateCondBr(
                    bi.CreateICmpULE(new_len, a_cap),
                    inplace_append_bb, inplace_grow_bb);
            }

            // --- In-place: grow buffer with exponential strategy ---
            {
                IRBuilder<> bg(inplace_grow_bb);
                auto* a_len = bg.CreateLoad(i64_ty,
                    bg.CreateStructGEP(m_string_type, a_str, 1));
                auto* a_cap = bg.CreateLoad(i64_ty,
                    bg.CreateStructGEP(m_string_type, a_str, 2));
                auto* a_chars_ptr = bg.CreateStructGEP(m_string_type, a_str, 3);
                auto* a_chars = bg.CreateLoad(i8_ptr, a_chars_ptr);

                auto* new_len = bg.CreateAdd(a_len, b_len);
                auto* doubled = bg.CreateShl(a_cap, 1, "doubled");
                auto* new_cap = bg.CreateSelect(
                    bg.CreateICmpUGT(doubled, new_len), doubled, new_len);
                auto* new_buf = bg.CreateCall(realloc_fn,
                    {a_chars, bg.CreateAdd(new_cap, ConstantInt::get(i64_ty, 1))}, "grown_buf");
                bg.CreateStore(new_buf, a_chars_ptr);
                bg.CreateStore(new_cap, bg.CreateStructGEP(m_string_type, a_str, 2));
                bg.CreateBr(inplace_append_bb);
            }

            // --- In-place: append data ---
            {
                IRBuilder<> ba(inplace_append_bb);
                auto* a_len = ba.CreateLoad(i64_ty,
                    ba.CreateStructGEP(m_string_type, a_str, 1));
                auto* a_chars = ba.CreateLoad(i8_ptr,
                    ba.CreateStructGEP(m_string_type, a_str, 3), "cur_chars");

                auto* final_len = ba.CreateAdd(a_len, b_len, "final_len");
                ba.CreateCall(memcpy_fn,
                    {ba.CreateGEP(i8_ty, a_chars, {a_len}), b_chars, b_len});
                ba.CreateStore(final_len, ba.CreateStructGEP(m_string_type, a_str, 1));
                ba.CreateStore(ConstantInt::get(i8_ty, 0),
                    ba.CreateGEP(i8_ty, a_chars, {final_len}));

                ba.CreateRet(a);
            }

            // --- Copy path: traditional allocate + copy ---
            {
                IRBuilder<> bc(copy_bb);
                auto* a_len = bc.CreateLoad(i64_ty,
                    bc.CreateStructGEP(m_string_type, a_str, 1), "a_len");
                auto* a_chars = bc.CreateLoad(i8_ptr,
                    bc.CreateStructGEP(m_string_type, a_str, 3), "a_chars");

                auto* new_len = bc.CreateAdd(a_len, b_len, "new_len");
                auto* buf = bc.CreateCall(malloc_fn,
                    {bc.CreateAdd(new_len, ConstantInt::get(i64_ty, 1))}, "buf");
                bc.CreateCall(memcpy_fn, {buf, a_chars, a_len});
                bc.CreateCall(memcpy_fn,
                    {bc.CreateGEP(i8_ty, buf, {a_len}), b_chars, b_len});
                bc.CreateStore(ConstantInt::get(i8_ty, 0),
                    bc.CreateGEP(i8_ty, buf, {new_len}));

                auto* str_size = ConstantInt::get(i64_ty,
                    m_module.getDataLayout().getTypeAllocSize(m_string_type));
                auto* gc_alloc_fn = m_module.getFunction("__ang_gc_alloc");
                auto* str_ptr = bc.CreateCall(gc_alloc_fn,
                    {str_size, ConstantInt::get(i32_ty, OBJ_STRING)}, "str_mem");

                init_string_struct(bc, str_ptr, new_len, buf);

                bc.CreateRet(pack_obj(bc, str_ptr));
            }
        }
    }

    // __ang_string_repeat
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
        auto* src_chars = bp.CreateLoad(i8_ptr,
            bp.CreateStructGEP(m_string_type, str_ptr, 3), "src_chars");
        auto* src_len = bp.CreateLoad(i64_ty,
            bp.CreateStructGEP(m_string_type, str_ptr, 1), "src_len");

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
            auto* gc_alloc_fn = m_module.getFunction("__ang_gc_alloc");
            auto* new_str_ptr = bd.CreateCall(gc_alloc_fn,
                {str_size, ConstantInt::get(i32_ty, OBJ_STRING)}, "str_mem");

            init_string_struct(bd, new_str_ptr, new_len, buf);

            bd.CreateRet(pack_obj(bd, new_str_ptr));
        }
    }

    // __ang_to_string
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
            bn.CreateRet(bn.CreateCall(str_from_c, {gsptr}));
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
            bs.CreateRet(val);

            IRBuilder<> bns(not_str_bb);
            auto* gsptr = bns.CreateGlobalString("<object>");
            auto* str_from_c = m_module.getFunction("__ang_string_from_c");
            bns.CreateRet(bns.CreateCall(str_from_c, {gsptr}));
        }
    }

    // __ang_string_compare: lexicographic comparison of two strings.
    // Returns AngaraObject wrapping i64: -1 if a < b, 0 if a == b, 1 if a > b.
    // Falls back to __ang_to_string for non-string operands.
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_string_compare", fn_ty);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* fallback_bb = BasicBlock::Create(m_ctx, "fallback", fn);
        auto* compare_bb = BasicBlock::Create(m_ctx, "compare", fn);

        IRBuilder<> b(entry);
        auto* a = fn->arg_begin();
        auto* b_arg = fn->arg_begin() + 1;

        auto* a_tag = b.CreateExtractValue(a, {0}, "a_tag");
        auto* b_tag = b.CreateExtractValue(b_arg, {0}, "b_tag");
        auto* both_obj = b.CreateAnd(
            b.CreateICmpEQ(a_tag, ConstantInt::get(i32_ty, TAG_OBJ)),
            b.CreateICmpEQ(b_tag, ConstantInt::get(i32_ty, TAG_OBJ)));

        auto* a_payload = b.CreateExtractValue(a, {1});
        auto* a_str = b.CreateIntToPtr(b.CreateBitCast(a_payload, i64_ty),
                                        PointerType::get(m_ctx, 0));
        auto* b_payload = b.CreateExtractValue(b_arg, {1});
        auto* b_str = b.CreateIntToPtr(b.CreateBitCast(b_payload, i64_ty),
                                        PointerType::get(m_ctx, 0));

        auto* a_obj_type = b.CreateLoad(i32_ty,
            b.CreateStructGEP(m_obj_header_type, a_str, 0), "a_obj_type");
        auto* b_obj_type = b.CreateLoad(i32_ty,
            b.CreateStructGEP(m_obj_header_type, b_str, 0), "b_obj_type");
        auto* both_string = b.CreateAnd(
            b.CreateICmpEQ(a_obj_type, ConstantInt::get(i32_ty, OBJ_STRING)),
            b.CreateICmpEQ(b_obj_type, ConstantInt::get(i32_ty, OBJ_STRING)));
        auto* both_valid = b.CreateAnd(both_obj, both_string);
        b.CreateCondBr(both_valid, compare_bb, fallback_bb);

        // Fallback: convert to string and retry
        {
            IRBuilder<> bf(fallback_bb);
            auto* to_str_fn = m_module.getFunction("__ang_to_string");
            auto* a_str_obj = bf.CreateCall(to_str_fn, {a}, "a_str");
            auto* b_str_obj = bf.CreateCall(to_str_fn, {b_arg}, "b_str");
            auto* result = bf.CreateCall(fn, {a_str_obj, b_str_obj}, "result");
            bf.CreateRet(result);
        }

        // Compare: extract char*, call strcmp, clamp to -1/0/1
        {
            IRBuilder<> bc(compare_bb);
            auto* strcmp_fn = m_module.getFunction("strcmp");
            auto* a_chars = bc.CreateLoad(i8_ptr,
                bc.CreateStructGEP(m_string_type, a_str, 3), "a_chars");
            auto* b_chars = bc.CreateLoad(i8_ptr,
                bc.CreateStructGEP(m_string_type, b_str, 3), "b_chars");
            auto* cmp_i32 = bc.CreateCall(strcmp_fn, {a_chars, b_chars}, "cmp");
            auto* cmp_i64 = bc.CreateSExt(cmp_i32, i64_ty, "cmp_i64");

            auto* neg_one = ConstantInt::get(i64_ty, -1);
            auto* zero = ConstantInt::get(i64_ty, 0);
            auto* one = ConstantInt::get(i64_ty, 1);

            // sign = cmp < 0 ? -1 : (cmp > 0 ? 1 : 0)
            auto* is_pos = bc.CreateICmpSGT(cmp_i64, zero);
            auto* is_neg = bc.CreateICmpSLT(cmp_i64, zero);
            auto* sign = bc.CreateSelect(is_neg, neg_one,
                           bc.CreateSelect(is_pos, one, zero), "sign");

            // Pack as AngaraObject with TAG_I64
            Value* result = UndefValue::get(obj_ty);
            result = bc.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_I64), {0});
            result = bc.CreateInsertValue(result, sign, {1});
            bc.CreateRet(result);
        }
    }

}

} // namespace angara
