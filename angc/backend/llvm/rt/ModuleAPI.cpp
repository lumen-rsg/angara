#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {

void RuntimeBuilder::generateModuleAPIVTable() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i1_ty   = Type::getInt1Ty(m_ctx);
    auto* i8_ty   = Type::getInt8Ty(m_ctx);
    auto* i32_ty  = Type::getInt32Ty(m_ctx);
    auto* i64_ty  = Type::getInt64Ty(m_ctx);
    auto* i8_ptr  = PointerType::get(m_ctx, 0);
    auto* obj_ty  = m_angara_obj_type;
    auto* ptr_ty  = PointerType::get(m_ctx, 0);

    auto* malloc_fn = m_module.getFunction("malloc");
    auto* free_fn   = m_module.getFunction("free");
    auto* strdup_fn = m_module.getFunction("strdup");
    auto* memcpy_fn = m_module.getFunction("memcpy");
    auto* strlen_fn = m_module.getFunction("strlen");

    auto mkExt = [&](const std::string& name, FunctionType* type) -> Function* {
        auto* fn = Function::Create(type, Function::InternalLinkage, name, m_module);
        fn->setDSOLocal(true);
        return fn;
    };

    auto packObj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* p = b.CreatePtrToInt(b.CreateBitCast(raw_ptr, i8_ptr), i64_ty);
        Value* r = UndefValue::get(obj_ty);
        r = b.CreateInsertValue(r, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        r = b.CreateInsertValue(r, p, {1});
        return r;
    };

    auto unpackPtr = [&](IRBuilder<>& b, Value* val) -> Value* {
        return b.CreateIntToPtr(b.CreateExtractValue(val, {1}), ptr_ty);
    };

    auto* fn_string       = m_module.getFunction("__ang_string_from_c");
    auto* fn_string_concat= m_module.getFunction("__ang_string_concat");
    auto* fn_record_new   = m_module.getFunction("__ang_record_new");
    auto* fn_record_get   = m_module.getFunction("__ang_record_get");
    auto* fn_record_set   = m_module.getFunction("__ang_record_set");
    auto* fn_list_new     = m_module.getFunction("__ang_list_new");
    auto* fn_list_push    = m_module.getFunction("__ang_list_push");
    auto* fn_pin         = m_module.getFunction("__ang_gc_pin");
    auto* fn_unpin       = m_module.getFunction("__ang_gc_unpin");
    auto* fn_to_string    = m_module.getFunction("__ang_to_string");

    Function* fn_as_cstr;
    {
        auto* ft = FunctionType::get(i8_ptr, {obj_ty}, false);
        fn_as_cstr = mkExt("__ang_api_as_cstr", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_as_cstr);
        IRBuilder<> b(bb);
        auto* val = fn_as_cstr->arg_begin();
        auto* ptr = unpackPtr(b, val);
        auto* chars = b.CreateLoad(i8_ptr, b.CreateStructGEP(m_string_type, ptr, 3));
        b.CreateRet(chars);
    }

    Function* fn_str_len;
    {
        auto* ft = FunctionType::get(i64_ty, {obj_ty}, false);
        fn_str_len = mkExt("__ang_api_str_len", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_str_len);
        IRBuilder<> b(bb);
        auto* val = fn_str_len->arg_begin();
        auto* ptr = unpackPtr(b, val);
        auto* len = b.CreateLoad(i64_ty, b.CreateStructGEP(m_string_type, ptr, 1));
        b.CreateRet(len);
    }

    Function* fn_obj_type;
    {
        auto* ft = FunctionType::get(i32_ty, {obj_ty}, false);
        fn_obj_type = mkExt("__ang_api_obj_type", ft);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn_obj_type);
        auto* is_obj = BasicBlock::Create(m_ctx, "is_obj", fn_obj_type);
        auto* not_obj = BasicBlock::Create(m_ctx, "not_obj", fn_obj_type);
        IRBuilder<> b(entry);
        auto* val = fn_obj_type->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0});
        b.CreateCondBr(b.CreateICmpEQ(tag, ConstantInt::get(i32_ty, TAG_OBJ)), is_obj, not_obj);

        IRBuilder<> bo(is_obj);
        auto* ptr = unpackPtr(bo, val);
        auto* t = bo.CreateLoad(i32_ty, bo.CreateStructGEP(m_obj_header_type, ptr, 0));
        bo.CreateRet(t);

        IRBuilder<> bn(not_obj);
        bn.CreateRet(ConstantInt::get(i32_ty, -1));
    }

    Function* fn_list_get;
    {
        auto* ft = FunctionType::get(obj_ty, {obj_ty, i64_ty}, false);
        fn_list_get = mkExt("__ang_api_list_get", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_list_get);
        IRBuilder<> b(bb);
        auto* list = fn_list_get->arg_begin();
        auto* idx = fn_list_get->arg_begin() + 1;
        Value* wrapped = UndefValue::get(obj_ty);
        wrapped = b.CreateInsertValue(wrapped, ConstantInt::get(i32_ty, TAG_I64), {0});
        wrapped = b.CreateInsertValue(wrapped, idx, {1});
        auto* callee = m_module.getFunction("__ang_list_get");
        b.CreateRet(b.CreateCall(callee, {list, wrapped}));
    }

    Function* fn_list_set;
    {
        auto* ft = FunctionType::get(void_ty, {obj_ty, i64_ty, obj_ty}, false);
        fn_list_set = mkExt("__ang_api_list_set", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_list_set);
        IRBuilder<> b(bb);
        auto* list = fn_list_set->arg_begin();
        auto* idx  = fn_list_set->arg_begin() + 1;
        auto* val  = fn_list_set->arg_begin() + 2;
        Value* wrapped = UndefValue::get(obj_ty);
        wrapped = b.CreateInsertValue(wrapped, ConstantInt::get(i32_ty, TAG_I64), {0});
        wrapped = b.CreateInsertValue(wrapped, idx, {1});
        auto* callee = m_module.getFunction("__ang_list_set");
        b.CreateCall(callee, {list, wrapped, val});
        b.CreateRetVoid();
    }

    Function* fn_list_len;
    {
        auto* ft = FunctionType::get(i64_ty, {obj_ty}, false);
        fn_list_len = mkExt("__ang_api_list_len", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_list_len);
        IRBuilder<> b(bb);
        auto* ptr = unpackPtr(b, fn_list_len->arg_begin());
        auto* cnt = b.CreateLoad(i64_ty, b.CreateStructGEP(m_list_type, ptr, 1));
        b.CreateRet(cnt);
    }

    Function* fn_record_len;
    {
        auto* ft = FunctionType::get(i64_ty, {obj_ty}, false);
        fn_record_len = mkExt("__ang_api_record_len", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_record_len);
        IRBuilder<> b(bb);
        auto* ptr = unpackPtr(b, fn_record_len->arg_begin());
        auto* cnt = b.CreateLoad(i64_ty, b.CreateStructGEP(m_record_type, ptr, 1));
        b.CreateRet(cnt);
    }

    Function* fn_record_key_at;
    {
        auto* ft = FunctionType::get(i8_ptr, {obj_ty, i64_ty}, false);
        fn_record_key_at = mkExt("__ang_api_record_key_at", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_record_key_at);
        IRBuilder<> b(bb);
        auto* val = fn_record_key_at->arg_begin();
        auto* idx = fn_record_key_at->arg_begin() + 1;
        auto* ptr = unpackPtr(b, val);
        auto* entries = b.CreateLoad(ptr_ty, b.CreateStructGEP(m_record_type, ptr, 3));
        auto* ep = b.CreateGEP(m_record_entry_type, entries, {idx});
        auto* key = b.CreateLoad(i8_ptr, b.CreateStructGEP(m_record_entry_type, ep, 0));
        b.CreateRet(key);
    }

    Function* fn_record_val_at;
    {
        auto* ft = FunctionType::get(obj_ty, {obj_ty, i64_ty}, false);
        fn_record_val_at = mkExt("__ang_api_record_val_at", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_record_val_at);
        IRBuilder<> b(bb);
        auto* val = fn_record_val_at->arg_begin();
        auto* idx = fn_record_val_at->arg_begin() + 1;
        auto* ptr = unpackPtr(b, val);
        auto* entries = b.CreateLoad(ptr_ty, b.CreateStructGEP(m_record_type, ptr, 3));
        auto* ep = b.CreateGEP(m_record_entry_type, entries, {idx});
        auto* v = b.CreateLoad(obj_ty, b.CreateStructGEP(m_record_entry_type, ep, 1));
        b.CreateRet(v);
    }

    Function* fn_native_instance_new;
    {
        auto* ft = FunctionType::get(obj_ty, {i8_ptr, i8_ptr, i8_ptr}, false);
        fn_native_instance_new = mkExt("__ang_api_native_instance_new", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_native_instance_new);
        IRBuilder<> b(bb);
        auto* data_arg = fn_native_instance_new->arg_begin();
        auto* fin_arg   = fn_native_instance_new->arg_begin() + 1;
        auto* name_arg  = fn_native_instance_new->arg_begin() + 2;

        auto* size = ConstantInt::get(i64_ty, 40);
        auto* mem = b.CreateCall(malloc_fn, {size});
        auto* inst = b.CreateBitCast(mem, ptr_ty);

        auto* hdr = b.CreateStructGEP(m_native_instance_type, inst, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_NATIVE_INSTANCE),
                      b.CreateStructGEP(m_obj_header_type, hdr, 0));
        b.CreateStore(getGcInitialMeta(),
                      b.CreateStructGEP(m_obj_header_type, hdr, 1));
        b.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
                      b.CreateStructGEP(m_obj_header_type, hdr, 2));
        b.CreateStore(data_arg, b.CreateStructGEP(m_native_instance_type, inst, 1));
        b.CreateStore(fin_arg,   b.CreateStructGEP(m_native_instance_type, inst, 2));
        auto* name_copy = b.CreateCall(strdup_fn, {name_arg});
        b.CreateStore(name_copy, b.CreateStructGEP(m_native_instance_type, inst, 3));

        b.CreateRet(packObj(b, inst));
    }

    Function* fn_native_instance_data;
    {
        auto* ft = FunctionType::get(i8_ptr, {obj_ty}, false);
        fn_native_instance_data = mkExt("__ang_api_native_instance_data", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_native_instance_data);
        IRBuilder<> b(bb);
        auto* ptr = unpackPtr(b, fn_native_instance_data->arg_begin());
        auto* data = b.CreateLoad(i8_ptr, b.CreateStructGEP(m_native_instance_type, ptr, 1));
        b.CreateRet(data);
    }

    Function* fn_throw_error;
    {
        auto* ft = FunctionType::get(void_ty, {i8_ptr}, false);
        fn_throw_error = mkExt("__ang_api_throw_error", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_throw_error);
        IRBuilder<> b(bb);
        auto* msg = fn_throw_error->arg_begin();
        auto* msg_str = b.CreateCall(fn_string, {msg});
        auto* exc = b.CreateCall(m_module.getFunction("__ang_exception_new"), {msg_str});
        b.CreateCall(m_module.getFunction("__ang_throw"), {exc});
        b.CreateRetVoid();
    }

    Function* fn_truthy;
    {
        auto* ft = FunctionType::get(i1_ty, {obj_ty}, false);
        fn_truthy = mkExt("__ang_api_truthy", ft);
        auto* entry_bb = BasicBlock::Create(m_ctx, "entry", fn_truthy);
        auto* nil_bb   = BasicBlock::Create(m_ctx, "nil", fn_truthy);
        auto* bool_bb  = BasicBlock::Create(m_ctx, "bool", fn_truthy);
        auto* num_bb   = BasicBlock::Create(m_ctx, "num", fn_truthy);
        auto* obj_bb   = BasicBlock::Create(m_ctx, "obj", fn_truthy);
        auto* merge_bb = BasicBlock::Create(m_ctx, "merge", fn_truthy);

        IRBuilder<> b(entry_bb);
        auto* val = fn_truthy->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0});
        auto* sw = b.CreateSwitch(tag, obj_bb, 3);
        sw->addCase(ConstantInt::get(i32_ty, TAG_NIL),  nil_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), bool_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_I64),  num_bb);

        IRBuilder<> bn(nil_bb);
        bn.CreateBr(merge_bb);

        IRBuilder<> bb(bool_bb);
        auto* pl = bb.CreateExtractValue(val, {1});
        auto* bv = bb.CreateICmpNE(bb.CreateTrunc(bb.CreateBitCast(pl, i64_ty), i1_ty),
                                    ConstantInt::get(i1_ty, false));
        bb.CreateBr(merge_bb);

        IRBuilder<> bi(num_bb);
        auto* iv = bi.CreateExtractValue(val, {1});
        auto* nv = bi.CreateICmpNE(bi.CreateBitCast(iv, i64_ty), ConstantInt::get(i64_ty, 0));
        bi.CreateBr(merge_bb);

        IRBuilder<> bo(obj_bb);
        bo.CreateBr(merge_bb);

        IRBuilder<> bm(merge_bb);
        auto* phi = bm.CreatePHI(i1_ty, 4);
        phi->addIncoming(ConstantInt::get(i1_ty, false), nil_bb);
        phi->addIncoming(bv, bool_bb);
        phi->addIncoming(nv, num_bb);
        phi->addIncoming(ConstantInt::get(i1_ty, true), obj_bb);
        bm.CreateRet(phi);
    }

    Function* fn_equals;
    {
        auto* ft = FunctionType::get(i1_ty, {obj_ty, obj_ty}, false);
        fn_equals = mkExt("__ang_api_equals", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_equals);
        IRBuilder<> b(bb);
        auto* a = fn_equals->arg_begin();
        auto* e = fn_equals->arg_begin() + 1;
        auto* callee = m_module.getFunction("__ang_equals");
        auto* res = b.CreateCall(callee, {a, e});
        auto* pl = b.CreateExtractValue(res, {1});
        auto* bv = b.CreateICmpNE(b.CreateTrunc(b.CreateBitCast(pl, i64_ty), i1_ty),
                                   ConstantInt::get(i1_ty, false));
        b.CreateRet(bv);
    }

    Function* fn_string_len;
    {
        auto* ft = FunctionType::get(obj_ty, {i8_ptr, i64_ty}, false);
        fn_string_len = mkExt("__ang_api_string_len", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_string_len);
        IRBuilder<> b(bb);
        auto* src = fn_string_len->arg_begin();
        auto* len = fn_string_len->arg_begin() + 1;

        // Copy the caller's bytes into a fresh buffer.
        auto* buf_size = b.CreateAdd(len, ConstantInt::get(i64_ty, 1));
        auto* buf = b.CreateCall(malloc_fn, {buf_size});
        b.CreateCall(memcpy_fn, {buf, src, len});
        b.CreateStore(ConstantInt::get(i8_ty, 0), b.CreateGEP(i8_ty, buf, {len}));

        // Allocate the String struct through the GC so it is tracked
        // (linked into the allocation list, walked by mark/sweep, freed
        // by the runtime rather than by raw free()).  __ang_gc_alloc
        // initializes the ObjHeader (type, meta, forward/next); we only
        // set the string-specific fields below.  Using getTypeAllocSize
        // also fixes a latent under-allocation: AngaraString is 40 bytes,
        // not the 32 that was hardcoded here previously.
        auto* str_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_string_type));
        auto* gc_alloc_fn = m_module.getFunction("__ang_gc_alloc");
        auto* str_ptr = b.CreateCall(gc_alloc_fn,
            {str_size, ConstantInt::get(i32_ty, OBJ_STRING)}, "str_mem");

        b.CreateStore(len, b.CreateStructGEP(m_string_type, str_ptr, 1));
        b.CreateStore(len, b.CreateStructGEP(m_string_type, str_ptr, 2)); // capacity = length
        b.CreateStore(buf, b.CreateStructGEP(m_string_type, str_ptr, 3));

        b.CreateRet(packObj(b, str_ptr));
    }

    Function* fn_string_no_copy;
    {
        auto* ft = FunctionType::get(obj_ty, {i8_ptr, i64_ty}, false);
        fn_string_no_copy = mkExt("__ang_api_string_no_copy", ft);
        auto* bb = BasicBlock::Create(m_ctx, "entry", fn_string_no_copy);
        IRBuilder<> b(bb);
        auto* src = fn_string_no_copy->arg_begin();
        auto* len = fn_string_no_copy->arg_begin() + 1;

        // Adopt the caller's buffer without copying.  The runtime takes
        // ownership and will free(buffer) when the string is collected.
        // Same GC-allocation rationale as fn_string_len above.
        auto* str_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_string_type));
        auto* gc_alloc_fn = m_module.getFunction("__ang_gc_alloc");
        auto* str_ptr = b.CreateCall(gc_alloc_fn,
            {str_size, ConstantInt::get(i32_ty, OBJ_STRING)}, "str_mem");

        b.CreateStore(len, b.CreateStructGEP(m_string_type, str_ptr, 1));
        b.CreateStore(len, b.CreateStructGEP(m_string_type, str_ptr, 2)); // capacity = length
        b.CreateStore(src, b.CreateStructGEP(m_string_type, str_ptr, 3));

        b.CreateRet(packObj(b, str_ptr));
    }

    Function* fn_record_new_wf;
    {
        auto* ft = FunctionType::get(obj_ty, {i32_ty, i8_ptr}, false);
        fn_record_new_wf = mkExt("__ang_api_record_new_with_fields", ft);
        auto* entry_bb = BasicBlock::Create(m_ctx, "entry", fn_record_new_wf);
        auto* loop_bb  = BasicBlock::Create(m_ctx, "loop", fn_record_new_wf);
        auto* body_bb  = BasicBlock::Create(m_ctx, "body", fn_record_new_wf);
        auto* done_bb  = BasicBlock::Create(m_ctx, "done", fn_record_new_wf);
        IRBuilder<> b(entry_bb);

        auto* count = fn_record_new_wf->arg_begin();
        auto* kv    = fn_record_new_wf->arg_begin() + 1;
        auto* count_i64 = b.CreateSExt(count, i64_ty);

        auto* rec = b.CreateCall(m_module.getFunction("__ang_record_new"), {});
        b.CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* i = bl.CreatePHI(i64_ty, 2, "i");
        i->addIncoming(ConstantInt::get(i64_ty, 0), entry_bb);
        auto* cmp = bl.CreateICmpSLT(i, count_i64);
        bl.CreateCondBr(cmp, body_bb, done_bb);

        IRBuilder<> bb(body_bb);
        auto* off_key = bb.CreateMul(i, ConstantInt::get(i64_ty, 2));
        auto* off_val = bb.CreateAdd(off_key, ConstantInt::get(i64_ty, 1));
        auto* key_ptr = bb.CreateGEP(obj_ty, kv, {off_key});
        auto* val_ptr = bb.CreateGEP(obj_ty, kv, {off_val});
        auto* key_obj = bb.CreateLoad(obj_ty, key_ptr);
        auto* val_obj = bb.CreateLoad(obj_ty, val_ptr);
        auto* key_payload = bb.CreateExtractValue(key_obj, {1});
        auto* key_raw = bb.CreateIntToPtr(key_payload, ptr_ty);
        auto* key_chars = bb.CreateLoad(i8_ptr, bb.CreateStructGEP(m_string_type, key_raw, 3));
        bb.CreateCall(m_module.getFunction("__ang_record_set"), {rec, key_chars, val_obj});
        auto* next = bb.CreateAdd(i, ConstantInt::get(i64_ty, 1));
        bb.CreateBr(loop_bb);
        i->addIncoming(next, body_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRet(rec);
    }

    std::vector<Type*> api_fields(27, ptr_ty);
    auto* api_type = StructType::create(m_ctx, api_fields, "AngaraAPI");

    std::vector<Constant*> fields = {
        fn_string,
        fn_string_len,
        fn_string_no_copy,
        fn_string_concat,
        fn_as_cstr,
        fn_str_len,
        fn_list_new,
        fn_list_push,
        fn_list_get,
        fn_list_set,
        fn_list_len,
        fn_record_new,
        fn_record_new_wf,
        fn_record_set,
        fn_record_get,
        fn_record_len,
        fn_record_key_at,
        fn_record_val_at,
        fn_native_instance_new,
        fn_native_instance_data,
        fn_pin,
        fn_unpin,
        fn_to_string,
        fn_truthy,
        fn_equals,
        fn_throw_error,
        fn_obj_type,
    };

    auto* api_const = ConstantStruct::get(api_type, fields);
    m_api_vtable = new GlobalVariable(m_module, api_type, true,
        GlobalValue::InternalLinkage, api_const, "__ang_api_vtable");
}

} // namespace angara
