#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {
void RuntimeBuilder::generateMemoryManagement() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_free_object", fn_ty);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* is_string_bb = BasicBlock::Create(m_ctx, "is_string", fn);
        auto* is_list_bb = BasicBlock::Create(m_ctx, "is_list", fn);
        auto* is_record_bb = BasicBlock::Create(m_ctx, "is_record", fn);
        auto* is_exception_bb = BasicBlock::Create(m_ctx, "is_exception", fn);
        auto* is_closure_bb = BasicBlock::Create(m_ctx, "is_closure", fn);
        auto* is_bound_bb = BasicBlock::Create(m_ctx, "is_bound", fn);
        auto* is_native_bb = BasicBlock::Create(m_ctx, "is_native", fn);
        auto* default_bb = BasicBlock::Create(m_ctx, "default_free", fn);

        IRBuilder<> b(entry);
        auto* obj_ptr = fn->arg_begin();

        auto* obj_type_addr = b.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
        auto* obj_type = b.CreateLoad(i32_ty, obj_type_addr, "obj_type");

        auto* switch_inst = b.CreateSwitch(obj_type, default_bb, 7);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_STRING), is_string_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_LIST), is_list_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_RECORD), is_record_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_EXCEPTION), is_exception_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_CLOSURE), is_closure_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD), is_bound_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_NATIVE_INSTANCE), is_native_bb);

        {
            IRBuilder<> bs(is_string_bb);
            auto* str_ptr = b.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
            auto* chars_ptr = bs.CreateStructGEP(m_string_type, str_ptr, 2);
            auto* chars = bs.CreateLoad(PointerType::get(m_ctx, 0), chars_ptr, "chars");
            auto* free_fn = m_module.getFunction("free");
            bs.CreateCall(free_fn, {chars});
            bs.CreateCall(free_fn, {obj_ptr});
            bs.CreateRetVoid();
        }

        {
            IRBuilder<> bl(is_list_bb);
            auto* list_ptr = b.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
            auto* count_ptr = bl.CreateStructGEP(m_list_type, list_ptr, 1);
            auto* count = bl.CreateLoad(i64_ty, count_ptr, "count");
            auto* elems_ptr = bl.CreateStructGEP(m_list_type, list_ptr, 3);
            auto* elems = bl.CreateLoad(PointerType::get(m_ctx, 0), elems_ptr, "elems");

            auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
            auto* body_bb = BasicBlock::Create(m_ctx, "body", fn);
            auto* done_bb = BasicBlock::Create(m_ctx, "done_elems", fn);

            auto* zero = ConstantInt::get(i64_ty, 0);
            bl.CreateBr(loop_bb);

            IRBuilder<> bl2(loop_bb);
            auto* i_phi = bl2.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(zero, is_list_bb);
            auto* cmp = bl2.CreateICmpSLT(i_phi, count, "cmp");
            bl2.CreateCondBr(cmp, body_bb, done_bb);

            IRBuilder<> bb(body_bb);
            auto* elem_ptr = bb.CreateGEP(obj_ty, elems, {i_phi});
            auto* elem_val = bb.CreateLoad(obj_ty, elem_ptr, "elem");
            auto* decref_ty = FunctionType::get(void_ty, {obj_ty}, false);
            auto decref_callee = m_module.getOrInsertFunction("__ang_decref", decref_ty);
            bb.CreateCall(decref_callee, {elem_val});
            auto* next = bb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1), "next");
            bb.CreateBr(loop_bb);
            i_phi->addIncoming(next, body_bb);

            IRBuilder<> bd(done_bb);
            auto* free_fn = m_module.getFunction("free");
            bd.CreateCall(free_fn, {b.CreateBitCast(elems, i8_ptr)});
            bd.CreateCall(free_fn, {obj_ptr});
            bd.CreateRetVoid();
        }

        {
            IRBuilder<> br(is_record_bb);
            auto* rec_ptr = b.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
            auto* count_ptr = br.CreateStructGEP(m_record_type, rec_ptr, 1);
            auto* count = br.CreateLoad(i64_ty, count_ptr, "count");
            auto* entries_ptr_addr = br.CreateStructGEP(m_record_type, rec_ptr, 3);
            auto* entries = br.CreateLoad(PointerType::get(m_ctx, 0), entries_ptr_addr, "entries");

            auto* loop_bb = BasicBlock::Create(m_ctx, "rloop", fn);
            auto* body_bb = BasicBlock::Create(m_ctx, "rbody", fn);
            auto* done_bb = BasicBlock::Create(m_ctx, "rdone", fn);

            auto* zero = ConstantInt::get(i64_ty, 0);
            br.CreateBr(loop_bb);

            IRBuilder<> bl2(loop_bb);
            auto* i_phi = bl2.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(zero, is_record_bb);
            auto* cmp = bl2.CreateICmpSLT(i_phi, count, "cmp");
            bl2.CreateCondBr(cmp, body_bb, done_bb);

            IRBuilder<> bb(body_bb);
            auto* entry_ptr = bb.CreateGEP(m_record_entry_type, entries, {i_phi});
            auto* key_ptr = bb.CreateStructGEP(m_record_entry_type, entry_ptr, 0);
            auto* key = bb.CreateLoad(i8_ptr, key_ptr, "key");
            auto* val_ptr = bb.CreateStructGEP(m_record_entry_type, entry_ptr, 1);
            auto* val = bb.CreateLoad(obj_ty, val_ptr, "val");
            auto* free_fn = m_module.getFunction("free");
            bb.CreateCall(free_fn, {key});
            auto* decref_fn = m_module.getFunction("__ang_decref");
            bb.CreateCall(decref_fn, {val});
            auto* next = bb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1), "next");
            bb.CreateBr(loop_bb);
            i_phi->addIncoming(next, body_bb);

            IRBuilder<> bd(done_bb);
            auto* free_fn2 = m_module.getFunction("free");
            bd.CreateCall(free_fn2, {b.CreateBitCast(entries, i8_ptr)});
            bd.CreateCall(free_fn2, {obj_ptr});
            bd.CreateRetVoid();
        }

        {
            IRBuilder<> be(is_exception_bb);
            auto* exc_ptr = b.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
            auto* msg_ptr = be.CreateStructGEP(m_exception_type, exc_ptr, 1);
            auto* msg = be.CreateLoad(obj_ty, msg_ptr, "msg");
            auto* decref_fn = m_module.getFunction("__ang_decref");
            be.CreateCall(decref_fn, {msg});
            auto* free_fn = m_module.getFunction("free");
            be.CreateCall(free_fn, {obj_ptr});
            be.CreateRetVoid();
        }

        {
            IRBuilder<> bc(is_closure_bb);
            auto* free_fn = m_module.getFunction("free");
            bc.CreateCall(free_fn, {obj_ptr});
            bc.CreateRetVoid();
        }

        {
            IRBuilder<> bb(is_bound_bb);
            auto* bm_ptr = b.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
            auto* recv_ptr = bb.CreateStructGEP(m_bound_method_type, bm_ptr, 1);
            auto* recv = bb.CreateLoad(obj_ty, recv_ptr);
            auto* meth_ptr = bb.CreateStructGEP(m_bound_method_type, bm_ptr, 2);
            auto* meth = bb.CreateLoad(obj_ty, meth_ptr);
            auto* decref_fn = m_module.getFunction("__ang_decref");
            bb.CreateCall(decref_fn, {recv});
            bb.CreateCall(decref_fn, {meth});
            auto* free_fn = m_module.getFunction("free");
            bb.CreateCall(free_fn, {obj_ptr});
            bb.CreateRetVoid();
        }

        {
            IRBuilder<> bn(is_native_bb);
            auto* ni_ptr = b.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));

            auto* finalize_ptr = bn.CreateStructGEP(m_native_instance_type, ni_ptr, 2);
            auto* finalize_fn = bn.CreateLoad(PointerType::get(m_ctx, 0), finalize_ptr, "finalize");

            auto* data_ptr = bn.CreateStructGEP(m_native_instance_type, ni_ptr, 1);
            auto* data = bn.CreateLoad(PointerType::get(m_ctx, 0), data_ptr, "data");

            auto* has_finalize = bn.CreateICmpNE(finalize_fn,
                ConstantPointerNull::get(PointerType::get(m_ctx, 0)));
            auto* call_fin_bb = BasicBlock::Create(m_ctx, "call_fin", fn);
            auto* after_fin_bb = BasicBlock::Create(m_ctx, "after_fin", fn);
            bn.CreateCondBr(has_finalize, call_fin_bb, after_fin_bb);

            IRBuilder<> bf(call_fin_bb);
            auto* fin_ty = FunctionType::get(Type::getVoidTy(m_ctx), {PointerType::get(m_ctx, 0)}, false);
            bf.CreateCall(fin_ty, finalize_fn, {data});
            bf.CreateBr(after_fin_bb);

            IRBuilder<> ba(after_fin_bb);
            auto* name_ptr = ba.CreateStructGEP(m_native_instance_type, ni_ptr, 3);
            auto* name = ba.CreateLoad(PointerType::get(m_ctx, 0), name_ptr, "name");
            auto* free_fn2 = m_module.getFunction("free");
            ba.CreateCall(free_fn2, {name});
            auto* free_fn3 = m_module.getFunction("free");
            ba.CreateCall(free_fn3, {obj_ptr});
            ba.CreateRetVoid();
        }

        {
            IRBuilder<> bd(default_bb);
            auto* free_fn = m_module.getFunction("free");
            bd.CreateCall(free_fn, {obj_ptr});
            bd.CreateRetVoid();
        }
    }

    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_incref", fn_ty);
        m_fn_incref = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* is_obj_bb = BasicBlock::Create(m_ctx, "is_obj", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0}, "tag");
        auto* is_obj = b.CreateICmpEQ(tag, ConstantInt::get(i32_ty, TAG_OBJ));
        b.CreateCondBr(is_obj, is_obj_bb, done_bb);

        IRBuilder<> b2(is_obj_bb);
        auto* payload = b2.CreateExtractValue(val, {1}, "payload");
        auto* ptr_i64 = b2.CreateBitCast(payload, i64_ty, "ptr_as_i64");
        auto* obj_ptr = b2.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0), "obj_ptr");
        auto* rc_addr = b2.CreateStructGEP(m_obj_header_type, obj_ptr, 1);
        auto* rc = b2.CreateLoad(i64_ty, rc_addr, "rc");
        auto* new_rc = b2.CreateAdd(rc, ConstantInt::get(i64_ty, 1), "new_rc");
        b2.CreateStore(new_rc, rc_addr);
        b2.CreateBr(done_bb);

        IRBuilder<> b3(done_bb);
        b3.CreateRetVoid();
    }

    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_decref", fn_ty);
        m_fn_decref = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* is_obj_bb = BasicBlock::Create(m_ctx, "is_obj", fn);
        auto* check_zero_bb = BasicBlock::Create(m_ctx, "check_zero", fn);
        auto* free_bb = BasicBlock::Create(m_ctx, "free", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0}, "tag");
        auto* is_obj = b.CreateICmpEQ(tag, ConstantInt::get(i32_ty, TAG_OBJ));
        b.CreateCondBr(is_obj, is_obj_bb, done_bb);

        IRBuilder<> b2(is_obj_bb);
        auto* payload = b2.CreateExtractValue(val, {1}, "payload");
        auto* ptr_i64 = b2.CreateBitCast(payload, i64_ty, "ptr_as_i64");
        auto* obj_ptr = b2.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0), "obj_ptr");
        auto* rc_addr = b2.CreateStructGEP(m_obj_header_type, obj_ptr, 1);
        auto* rc = b2.CreateLoad(i64_ty, rc_addr, "rc");
        auto* new_rc = b2.CreateSub(rc, ConstantInt::get(i64_ty, 1), "new_rc");
        b2.CreateStore(new_rc, rc_addr);
        b2.CreateBr(check_zero_bb);

        IRBuilder<> b3(check_zero_bb);
        auto* is_zero = b3.CreateICmpEQ(new_rc, ConstantInt::get(i64_ty, 0));
        b3.CreateCondBr(is_zero, free_bb, done_bb);

        IRBuilder<> b4(free_bb);
        auto* free_fn = m_module.getFunction("__ang_free_object");
        auto* raw_ptr = b4.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
        b4.CreateCall(free_fn, {raw_ptr});
        b4.CreateBr(done_bb);

        IRBuilder<> b5(done_bb);
        b5.CreateRetVoid();
    }
}

} // namespace angara
