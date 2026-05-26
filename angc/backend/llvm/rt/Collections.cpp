#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {
void RuntimeBuilder::generateListOps() {
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto* malloc_fn = m_module.getFunction("malloc");
    auto* realloc_fn = m_module.getFunction("realloc");
    auto* memset_fn = m_module.getFunction("memset");

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
        auto* fn_ty = FunctionType::get(obj_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_list_new", fn_ty);
        m_fn_list_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        auto* list_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_list_type));
        auto* mem = b.CreateCall(malloc_fn, {list_size}, "mem");
        auto* list_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_list_type, list_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_LIST),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i64_ty, 1),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));

        b.CreateStore(ConstantInt::get(i64_ty, 0), b.CreateStructGEP(m_list_type, list_ptr, 1));
        b.CreateStore(ConstantInt::get(i64_ty, 0), b.CreateStructGEP(m_list_type, list_ptr, 2));
        b.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            b.CreateStructGEP(m_list_type, list_ptr, 3));

        b.CreateRet(pack_obj(b, list_ptr));
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {i64_ty, PointerType::get(m_ctx, 0)}, false);
        auto* fn = createRuntimeFunc("__ang_list_new_with_elements", fn_ty);
        m_fn_list_new_with_elements = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* count = fn->arg_begin();
        auto* elems = fn->arg_begin() + 1;

        auto* list_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_list_type));
        auto* mem = b.CreateCall(malloc_fn, {list_size});
        auto* list_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_list_type, list_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_LIST),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i64_ty, 1),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(count, b.CreateStructGEP(m_list_type, list_ptr, 1));
        b.CreateStore(count, b.CreateStructGEP(m_list_type, list_ptr, 2));

        auto* elem_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(obj_ty));
        auto* total = b.CreateMul(count, elem_size);
        auto* elems_mem = b.CreateCall(malloc_fn, {total});
        b.CreateCall(m_module.getFunction("memcpy"),
            {elems_mem, b.CreateBitCast(elems, i8_ptr), total});
        b.CreateStore(b.CreateBitCast(elems_mem, PointerType::get(m_ctx, 0)),
            b.CreateStructGEP(m_list_type, list_ptr, 3));

        auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
        auto* body_bb = BasicBlock::Create(m_ctx, "body", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);
        b.CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* i_phi = bl.CreatePHI(i64_ty, 2, "i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), entry);
        auto* cmp = bl.CreateICmpSLT(i_phi, count);
        bl.CreateCondBr(cmp, body_bb, done_bb);

        IRBuilder<> bb(body_bb);
        auto* new_elems = b.CreateBitCast(elems_mem, PointerType::get(m_ctx, 0));
        auto* elem_ptr = bb.CreateGEP(obj_ty, new_elems, {i_phi});
        auto* elem = bb.CreateLoad(obj_ty, elem_ptr);
        bb.CreateCall(m_module.getFunction("__ang_incref"), {elem});
        auto* next = bb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        bb.CreateBr(loop_bb);
        i_phi->addIncoming(next, body_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRet(pack_obj(bd, list_ptr));
    }

    {
        auto* fn_ty = FunctionType::get(Type::getVoidTy(m_ctx), {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_list_push", fn_ty);
        m_fn_list_push = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* grow_bb = BasicBlock::Create(m_ctx, "grow", fn);
        auto* store_bb = BasicBlock::Create(m_ctx, "store", fn);

        IRBuilder<> b(entry);
        auto* list_arg = fn->arg_begin();
        auto* val_arg = fn->arg_begin() + 1;

        auto* payload = b.CreateExtractValue(list_arg, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* list_ptr = b.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));

        auto* count_addr = b.CreateStructGEP(m_list_type, list_ptr, 1);
        auto* cap_addr = b.CreateStructGEP(m_list_type, list_ptr, 2);
        auto* elems_addr = b.CreateStructGEP(m_list_type, list_ptr, 3);

        auto* count = b.CreateLoad(i64_ty, count_addr, "count");
        auto* cap = b.CreateLoad(i64_ty, cap_addr, "cap");

        auto* need_grow = b.CreateICmpEQ(count, cap, "need_grow");
        b.CreateCondBr(need_grow, grow_bb, store_bb);

        IRBuilder<> bg(grow_bb);
        auto* new_cap1 = bg.CreateAdd(count, ConstantInt::get(i64_ty, 1));
        auto* doubled = bg.CreateShl(cap, 1);
        auto* is_neg = bg.CreateICmpSLT(doubled, new_cap1);
        auto* new_cap = bg.CreateSelect(is_neg, new_cap1, doubled, "new_cap");
        auto* elem_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(obj_ty));
        auto* alloc_size = bg.CreateMul(new_cap, elem_size);
        auto* old_elems = bg.CreateLoad(PointerType::get(m_ctx, 0), elems_addr);
        auto* old_raw = bg.CreateBitCast(old_elems, i8_ptr);
        auto* new_raw = bg.CreateCall(realloc_fn, {old_raw, alloc_size}, "new_raw");
        bg.CreateStore(bg.CreateBitCast(new_raw, PointerType::get(m_ctx, 0)), elems_addr);
        bg.CreateStore(new_cap, cap_addr);
        bg.CreateBr(store_bb);

        IRBuilder<> bs(store_bb);
        auto* count2 = bs.CreateLoad(i64_ty, count_addr, "count2");
        auto* elems2 = bs.CreateLoad(PointerType::get(m_ctx, 0), elems_addr, "elems2");
        auto* elem_ptr = bs.CreateGEP(obj_ty, elems2, {count2});
        bs.CreateStore(val_arg, elem_ptr);
        bs.CreateCall(m_module.getFunction("__ang_incref"), {val_arg});
        bs.CreateStore(bs.CreateAdd(count2, ConstantInt::get(i64_ty, 1)), count_addr);
        bs.CreateRetVoid();
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_list_get", fn_ty);
        m_fn_list_get = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* in_bounds_bb = BasicBlock::Create(m_ctx, "in_bounds", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* list_arg = fn->arg_begin();
        auto* idx_arg = fn->arg_begin() + 1;

        auto* idx_payload = b.CreateExtractValue(idx_arg, {1});
        auto* idx = b.CreateBitCast(idx_payload, i64_ty, "idx");

        auto* payload = b.CreateExtractValue(list_arg, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* list_ptr = b.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));

        auto* count = b.CreateLoad(i64_ty, b.CreateStructGEP(m_list_type, list_ptr, 1), "count");
        auto* in_bounds = b.CreateAnd(
            b.CreateICmpSGE(idx, ConstantInt::get(i64_ty, 0)),
            b.CreateICmpSLT(idx, count), "in_bounds");
        b.CreateCondBr(in_bounds, in_bounds_bb, done_bb);

        IRBuilder<> bib(in_bounds_bb);
        auto* elems = bib.CreateLoad(PointerType::get(m_ctx, 0),
            bib.CreateStructGEP(m_list_type, list_ptr, 3), "elems");
        auto* elem_ptr = bib.CreateGEP(obj_ty, elems, {idx});
        auto* result = bib.CreateLoad(obj_ty, elem_ptr, "result");
        bib.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        auto* phi = bd.CreatePHI(obj_ty, 2, "val");
        phi->addIncoming(result, in_bounds_bb);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = bd.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = bd.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        phi->addIncoming(nil_val, entry);
        bd.CreateRet(phi);
    }

    {
        auto* fn_ty = FunctionType::get(Type::getVoidTy(m_ctx), {obj_ty, obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_list_set", fn_ty);
        m_fn_list_set = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* list_arg = fn->arg_begin();
        auto* idx_arg = fn->arg_begin() + 1;
        auto* val_arg = fn->arg_begin() + 2;

        auto* idx_payload = b.CreateExtractValue(idx_arg, {1});
        auto* idx = b.CreateBitCast(idx_payload, i64_ty, "idx");

        auto* payload = b.CreateExtractValue(list_arg, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* list_ptr = b.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));

        auto* elems = b.CreateLoad(PointerType::get(m_ctx, 0),
            b.CreateStructGEP(m_list_type, list_ptr, 3), "elems");
        auto* elem_ptr = b.CreateGEP(obj_ty, elems, {idx});

        auto* old = b.CreateLoad(obj_ty, elem_ptr, "old");
        b.CreateCall(m_module.getFunction("__ang_decref"), {old});
        b.CreateStore(val_arg, elem_ptr);
        b.CreateCall(m_module.getFunction("__ang_incref"), {val_arg});

        b.CreateRetVoid();
    }
}

void RuntimeBuilder::generateRecordOps() {
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto* malloc_fn = m_module.getFunction("malloc");
    auto* realloc_fn = m_module.getFunction("realloc");

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
        auto* fn_ty = FunctionType::get(obj_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_record_new", fn_ty);
        m_fn_record_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        auto* rec_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_record_type));
        auto* mem = b.CreateCall(malloc_fn, {rec_size});
        auto* rec_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_record_type, rec_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_RECORD),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i64_ty, 1),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(ConstantInt::get(i64_ty, 0), b.CreateStructGEP(m_record_type, rec_ptr, 1));
        b.CreateStore(ConstantInt::get(i64_ty, 0), b.CreateStructGEP(m_record_type, rec_ptr, 2));
        b.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            b.CreateStructGEP(m_record_type, rec_ptr, 3));

        b.CreateRet(pack_obj(b, rec_ptr));
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_record_get", fn_ty);
        m_fn_record_get = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
        auto* body_bb = BasicBlock::Create(m_ctx, "body", fn);
        auto* found_bb = BasicBlock::Create(m_ctx, "found", fn);
        auto* not_found_bb = BasicBlock::Create(m_ctx, "not_found", fn);

        IRBuilder<> b(entry);
        auto* rec_arg = fn->arg_begin();
        auto* key_arg = fn->arg_begin() + 1;

        auto* payload = b.CreateExtractValue(rec_arg, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* rec_ptr = b.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));

        auto* count = b.CreateLoad(i64_ty, b.CreateStructGEP(m_record_type, rec_ptr, 1), "count");
        auto* entries = b.CreateLoad(PointerType::get(m_ctx, 0),
            b.CreateStructGEP(m_record_type, rec_ptr, 3), "entries");
        b.CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* i_phi = bl.CreatePHI(i64_ty, 2, "i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), entry);
        auto* cmp = bl.CreateICmpSLT(i_phi, count);
        bl.CreateCondBr(cmp, body_bb, not_found_bb);

        IRBuilder<> bb(body_bb);
        auto* entry_ptr = bb.CreateGEP(m_record_entry_type, entries, {i_phi});
        auto* entry_key = bb.CreateLoad(i8_ptr, bb.CreateStructGEP(m_record_entry_type, entry_ptr, 0));
        auto* strcmp_fn = m_module.getFunction("strcmp");
        auto* cmp_result = bb.CreateCall(strcmp_fn, {entry_key, key_arg}, "cmp");
        auto* is_match = bb.CreateICmpEQ(cmp_result, ConstantInt::get(i32_ty, 0));

        auto* next_bb = BasicBlock::Create(m_ctx, "next", fn);
        bb.CreateCondBr(is_match, found_bb, next_bb);

        {
            IRBuilder<> bn(next_bb);
            auto* next_i = bn.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            bn.CreateBr(loop_bb);
            i_phi->addIncoming(next_i, next_bb);
        }

        IRBuilder<> bf(found_bb);
        auto* found_entry = bf.CreateGEP(m_record_entry_type, entries, {i_phi});
        auto* val = bf.CreateLoad(obj_ty, bf.CreateStructGEP(m_record_entry_type, found_entry, 1));
        bf.CreateRet(val);

        IRBuilder<> bnf(not_found_bb);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = bnf.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = bnf.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        bnf.CreateRet(nil_val);
    }

    {
        auto* fn_ty = FunctionType::get(Type::getVoidTy(m_ctx), {obj_ty, i8_ptr, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_record_set", fn_ty);
        m_fn_record_set = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
        auto* body_bb = BasicBlock::Create(m_ctx, "body", fn);
        auto* found_bb = BasicBlock::Create(m_ctx, "found", fn);
        auto* not_found_bb = BasicBlock::Create(m_ctx, "not_found", fn);
        auto* grow_bb = BasicBlock::Create(m_ctx, "grow", fn);
        auto* insert_bb = BasicBlock::Create(m_ctx, "insert", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* rec_arg = fn->arg_begin();
        auto* key_arg = fn->arg_begin() + 1;
        auto* val_arg = fn->arg_begin() + 2;

        auto* payload = b.CreateExtractValue(rec_arg, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* rec_ptr = b.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));

        auto* count_addr = b.CreateStructGEP(m_record_type, rec_ptr, 1);
        auto* cap_addr = b.CreateStructGEP(m_record_type, rec_ptr, 2);
        auto* entries_addr = b.CreateStructGEP(m_record_type, rec_ptr, 3);

        auto* count = b.CreateLoad(i64_ty, count_addr, "count");
        auto* entries = b.CreateLoad(PointerType::get(m_ctx, 0), entries_addr, "entries");
        b.CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* i_phi = bl.CreatePHI(i64_ty, 2, "i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), entry);
        auto* cmp = bl.CreateICmpSLT(i_phi, count);
        bl.CreateCondBr(cmp, body_bb, not_found_bb);

        IRBuilder<> bb(body_bb);
        auto* entry_ptr = bb.CreateGEP(m_record_entry_type, entries, {i_phi});
        auto* entry_key = bb.CreateLoad(i8_ptr, bb.CreateStructGEP(m_record_entry_type, entry_ptr, 0));
        auto* strcmp_fn = m_module.getFunction("strcmp");
        auto* cmp_result = bb.CreateCall(strcmp_fn, {entry_key, key_arg});
        auto* is_match = bb.CreateICmpEQ(cmp_result, ConstantInt::get(i32_ty, 0));
        auto* next_bb = BasicBlock::Create(m_ctx, "next", fn);
        bb.CreateCondBr(is_match, found_bb, next_bb);

        IRBuilder<> bn(next_bb);
        auto* next_i = bn.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        bn.CreateBr(loop_bb);
        i_phi->addIncoming(next_i, next_bb);

        IRBuilder<> bf(found_bb);
        auto* found_entry = bf.CreateGEP(m_record_entry_type, entries, {i_phi});
        auto* val_addr = bf.CreateStructGEP(m_record_entry_type, found_entry, 1);
        auto* old_val = bf.CreateLoad(obj_ty, val_addr, "old");
        bf.CreateCall(m_module.getFunction("__ang_decref"), {old_val});
        bf.CreateStore(val_arg, val_addr);
        bf.CreateCall(m_module.getFunction("__ang_incref"), {val_arg});
        bf.CreateBr(done_bb);

        IRBuilder<> bnf(not_found_bb);
        auto* cap = bnf.CreateLoad(i64_ty, cap_addr, "cap");
        auto* need_grow = bnf.CreateICmpEQ(count, cap);
        bnf.CreateCondBr(need_grow, grow_bb, insert_bb);

        IRBuilder<> bg(grow_bb);
        auto* new_cap1 = bg.CreateAdd(count, ConstantInt::get(i64_ty, 1));
        auto* doubled = bg.CreateShl(cap, 1);
        auto* is_sm = bg.CreateICmpSLT(doubled, new_cap1);
        auto* new_cap = bg.CreateSelect(is_sm, new_cap1, doubled);
        auto* entry_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_record_entry_type));
        auto* alloc_size = bg.CreateMul(new_cap, entry_size);
        auto* old_raw = bg.CreateBitCast(entries, i8_ptr);
        auto* new_raw = bg.CreateCall(realloc_fn, {old_raw, alloc_size});
        auto* new_entries = bg.CreateBitCast(new_raw, PointerType::get(m_ctx, 0));
        bg.CreateStore(new_entries, entries_addr);
        bg.CreateStore(new_cap, cap_addr);
        bg.CreateBr(insert_bb);

        IRBuilder<> bi(insert_bb);
        auto* cur_entries = bi.CreateLoad(PointerType::get(m_ctx, 0), entries_addr);
        auto* cur_count = bi.CreateLoad(i64_ty, count_addr);
        auto* new_entry = bi.CreateGEP(m_record_entry_type, cur_entries, {cur_count});

        auto* strdup_fn = m_module.getFunction("strdup");
        auto* copied_key = bi.CreateCall(strdup_fn, {key_arg});
        bi.CreateStore(copied_key, bi.CreateStructGEP(m_record_entry_type, new_entry, 0));

        bi.CreateStore(val_arg, bi.CreateStructGEP(m_record_entry_type, new_entry, 1));
        bi.CreateCall(m_module.getFunction("__ang_incref"), {val_arg});

        bi.CreateStore(bi.CreateAdd(cur_count, ConstantInt::get(i64_ty, 1)), count_addr);
        bi.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }
}

} // namespace angara
