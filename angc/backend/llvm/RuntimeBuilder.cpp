//
// Angara LLVM Backend — Runtime IR Builder
// Generates all runtime functions as LLVM IR directly into the module.
// No external runtime library dependency — only libc/libpthread.
//

#include "RuntimeBuilder.h"
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

namespace angara {

// ============================================================================
// Constructor
// ============================================================================

RuntimeBuilder::RuntimeBuilder(LLVMContext& context, Module& module, IRBuilder<>& builder)
    : m_ctx(context), m_module(module), m_builder(builder) {}

// ============================================================================
// Top-level: generate everything
// ============================================================================

void RuntimeBuilder::generateRuntime() {
    generateTypes();
    declareCLibFunctions();
    generateMemoryManagement();  // incref defined first, then free_object, then decref
    generateStringOps();
    generateListOps();
    generateRecordOps();
    generateConversions();
    generateEquality();
    generateClosureOps();
    generateExceptionOps();
    generateThreadOps();
    generateMiscOps();
    generateIOOps();
}

// ============================================================================
// Helper: create an internal runtime function
// ============================================================================

Function* RuntimeBuilder::createRuntimeFunc(const std::string& name, FunctionType* type, bool variadic) {
    auto callee = m_module.getOrInsertFunction(name, type);
    auto* fn = cast<Function>(callee.getCallee());
    fn->setLinkage(Function::InternalLinkage);
    fn->setDSOLocal(true);
    return fn;
}

// ============================================================================
// Type generation
// ============================================================================

void RuntimeBuilder::generateTypes() {
    // AngaraObject = { i32 type_tag, i64 payload }
    // payload holds: bool (i8 zext), i64, f64 (via bitcast), or Object* (via ptrtoint)
    m_angara_obj_type = StructType::create(m_ctx, {
        Type::getInt32Ty(m_ctx),     // type tag
        Type::getInt64Ty(m_ctx)      // payload (8 bytes — fits i64, f64 via bitcast, ptr via ptrtoint)
    }, "AngaraObject");

    // Object header = { i32 obj_type, i64 ref_count }
    m_obj_header_type = StructType::create(m_ctx, {
        Type::getInt32Ty(m_ctx),     // ObjectType
        Type::getInt64Ty(m_ctx)      // ref_count
    }, "ObjHeader");

    // String = { ObjHeader, i64 length, i8* chars }
    m_string_type = StructType::create(m_ctx, {
        m_obj_header_type,           // header
        Type::getInt64Ty(m_ctx),     // length
        PointerType::get(m_ctx, 0)  // chars
    }, "AngaraString");

    // List = { ObjHeader, i64 count, i64 capacity, AngaraObject* elements }
    m_list_type = StructType::create(m_ctx, {
        m_obj_header_type,           // header
        Type::getInt64Ty(m_ctx),     // count
        Type::getInt64Ty(m_ctx),     // capacity
        PointerType::get(m_ctx, 0)  // elements
    }, "AngaraList");

    // RecordEntry = { i8* key, AngaraObject value }
    m_record_entry_type = StructType::create(m_ctx, {
        PointerType::get(m_ctx, 0),  // key
        m_angara_obj_type            // value
    }, "RecordEntry");

    // Record = { ObjHeader, i64 count, i64 capacity, RecordEntry* entries }
    m_record_type = StructType::create(m_ctx, {
        m_obj_header_type,           // header
        Type::getInt64Ty(m_ctx),     // count
        Type::getInt64Ty(m_ctx),     // capacity
        PointerType::get(m_ctx, 0)  // entries
    }, "AngaraRecord");

    // Exception = { ObjHeader, AngaraObject message }
    m_exception_type = StructType::create(m_ctx, {
        m_obj_header_type,           // header
        m_angara_obj_type            // message
    }, "AngaraException");

    // Closure = { ObjHeader, void* fn, i32 arity, i1 is_native }
    auto* fn_ptr_type = PointerType::get(m_ctx, 0);
    m_closure_type = StructType::create(m_ctx, {
        m_obj_header_type,           // header
        fn_ptr_type,                 // fn
        Type::getInt32Ty(m_ctx),     // arity
        Type::getInt1Ty(m_ctx)       // is_native
    }, "AngaraClosure");

    // BoundMethod = { ObjHeader, AngaraObject receiver, AngaraObject method_closure }
    m_bound_method_type = StructType::create(m_ctx, {
        m_obj_header_type,           // header
        m_angara_obj_type,           // receiver
        m_angara_obj_type            // method_closure
    }, "AngaraBoundMethod");

    // ExceptionFrame = { void* jmp_buf, void* prev }
    m_thread_type = StructType::create(m_ctx, {
        m_obj_header_type,           // header
        PointerType::get(m_ctx, 0),  // pthread_t (opaque)
        m_angara_obj_type            // return_value
    }, "AngaraThread");

    // Mutex = { ObjHeader, opaque pthread_mutex_t[40 bytes] }
    m_mutex_type = StructType::create(m_ctx, {
        m_obj_header_type,           // header
        ArrayType::get(Type::getInt8Ty(m_ctx), 64)  // pthread_mutex_t (padded)
    }, "AngaraMutex");

    // Exception globals
    m_g_exception_chain = new GlobalVariable(
        m_module, PointerType::get(m_ctx, 0),
        false, GlobalValue::CommonLinkage,
        ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
        "__ang_exception_chain");

    m_g_current_exception = new GlobalVariable(
        m_module, m_angara_obj_type,
        false, GlobalValue::InternalLinkage,
        ConstantAggregateZero::get(m_angara_obj_type),
        "__ang_current_exception");
}

// ============================================================================
// libc declarations
// ============================================================================

void RuntimeBuilder::declareCLibFunctions() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* f64_ty = Type::getDoubleTy(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);

    // malloc(size_t) -> void*
    FunctionType* malloc_ty = FunctionType::get(i8_ptr, {i64_ty}, false);
    m_module.getOrInsertFunction("malloc", malloc_ty);

    // free(void*)
    FunctionType* free_ty = FunctionType::get(void_ty, {i8_ptr}, false);
    m_module.getOrInsertFunction("free", free_ty);

    // realloc(void*, size_t) -> void*
    FunctionType* realloc_ty = FunctionType::get(i8_ptr, {i8_ptr, i64_ty}, false);
    m_module.getOrInsertFunction("realloc", realloc_ty);

    // memcpy(void*, void*, size_t) -> void*
    FunctionType* memcpy_ty = FunctionType::get(i8_ptr, {i8_ptr, i8_ptr, i64_ty}, false);
    m_module.getOrInsertFunction("memcpy", memcpy_ty);

    // strlen(const char*) -> size_t
    FunctionType* strlen_ty = FunctionType::get(i64_ty, {i8_ptr}, false);
    m_module.getOrInsertFunction("strlen", strlen_ty);

    // strcmp(const char*, const char*) -> i32
    FunctionType* strcmp_ty = FunctionType::get(i32_ty, {i8_ptr, i8_ptr}, false);
    m_module.getOrInsertFunction("strcmp", strcmp_ty);

    // strdup(const char*) -> char*
    FunctionType* strdup_ty = FunctionType::get(i8_ptr, {i8_ptr}, false);
    m_module.getOrInsertFunction("strdup", strdup_ty);

    // printf(const char*, ...) -> i32
    FunctionType* printf_ty = FunctionType::get(i32_ty, {i8_ptr}, true);
    m_fn_printf = m_module.getOrInsertFunction("printf", printf_ty);

    // fprintf(void*, const char*, ...) -> i32
    FunctionType* fprintf_ty = FunctionType::get(i32_ty, {PointerType::get(m_ctx, 0), i8_ptr}, true);
    m_module.getOrInsertFunction("fprintf", fprintf_ty);

    // snprintf(char*, size_t, const char*, ...) -> i32
    FunctionType* snprintf_ty = FunctionType::get(i32_ty, {i8_ptr, i64_ty, i8_ptr}, true);
    m_module.getOrInsertFunction("snprintf", snprintf_ty);

    // strtoll(const char*, char**, int) -> i64
    FunctionType* strtoll_ty = FunctionType::get(i64_ty, {i8_ptr, PointerType::get(m_ctx, 0), i32_ty}, false);
    m_module.getOrInsertFunction("strtoll", strtoll_ty);

    // strtod(const char*, char**) -> double
    FunctionType* strtod_ty = FunctionType::get(f64_ty, {i8_ptr, PointerType::get(m_ctx, 0)}, false);
    m_module.getOrInsertFunction("strtod", strtod_ty);

    // setjmp(void*) -> i32
    FunctionType* setjmp_ty = FunctionType::get(i32_ty, {i8_ptr}, false);
    m_module.getOrInsertFunction("setjmp", setjmp_ty);

    // longjmp(void*, i32)
    FunctionType* longjmp_ty = FunctionType::get(void_ty, {i8_ptr, i32_ty}, false);
    m_module.getOrInsertFunction("longjmp", longjmp_ty);

    // exit(i32)
    FunctionType* exit_ty = FunctionType::get(void_ty, {i32_ty}, false);
    m_module.getOrInsertFunction("exit", exit_ty);

    // pthread_create, pthread_join etc
    FunctionType* pthread_create_ty = FunctionType::get(
        i32_ty,
        {PointerType::get(m_ctx, 0),
         PointerType::get(m_ctx, 0),
         PointerType::get(m_ctx, 0),
         PointerType::get(m_ctx, 0)},
        false);
    m_module.getOrInsertFunction("pthread_create", pthread_create_ty);

    FunctionType* pthread_join_ty = FunctionType::get(i32_ty, {
        PointerType::get(m_ctx, 0),
        PointerType::get(m_ctx, 0)
    }, false);
    m_module.getOrInsertFunction("pthread_join", pthread_join_ty);

    // memset(void*, int, size_t)
    FunctionType* memset_ty = FunctionType::get(i8_ptr, {i8_ptr, i32_ty, i64_ty}, false);
    m_module.getOrInsertFunction("memset", memset_ty);
}

// ============================================================================
// Memory Management: incref, decref, free_object
// ============================================================================

void RuntimeBuilder::generateMemoryManagement() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    // --- free_object(void* obj_ptr) ---
    // Deallocates a heap object based on its ObjectType.
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
        auto* default_bb = BasicBlock::Create(m_ctx, "default_free", fn);

        IRBuilder<> b(entry);
        auto* obj_ptr = fn->arg_begin();

        // Get object type: *(i32*)(obj_ptr)
        auto* obj_type_addr = b.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
        auto* obj_type = b.CreateLoad(i32_ty, obj_type_addr, "obj_type");

        auto* switch_inst = b.CreateSwitch(obj_type, default_bb, 6);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_STRING), is_string_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_LIST), is_list_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_RECORD), is_record_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_EXCEPTION), is_exception_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_CLOSURE), is_closure_bb);
        switch_inst->addCase(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD), is_bound_bb);

        // --- String free: free(chars), free(string) ---
        {
            IRBuilder<> bs(is_string_bb);
            // chars is at offset: header(16) + length(8) = 24
            auto* str_ptr = b.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
            auto* chars_ptr = bs.CreateStructGEP(m_string_type, str_ptr, 2);
            auto* chars = bs.CreateLoad(PointerType::get(m_ctx, 0), chars_ptr, "chars");
            auto* free_fn = m_module.getFunction("free");
            bs.CreateCall(free_fn, {chars});
            bs.CreateCall(free_fn, {obj_ptr});
            bs.CreateRetVoid();
        }

        // --- List free: decref all elements, free(elements), free(list) ---
        {
            IRBuilder<> bl(is_list_bb);
            auto* list_ptr = b.CreateBitCast(obj_ptr, PointerType::get(m_ctx, 0));
            auto* count_ptr = bl.CreateStructGEP(m_list_type, list_ptr, 1);
            auto* count = bl.CreateLoad(i64_ty, count_ptr, "count");
            auto* elems_ptr = bl.CreateStructGEP(m_list_type, list_ptr, 3);
            auto* elems = bl.CreateLoad(PointerType::get(m_ctx, 0), elems_ptr, "elems");

            // Loop to decref each element
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
            // Call decref on element (may be forward-declared, will be defined later)
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

        // --- Record free: free each key, decref each value, free(entries), free(record) ---
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

        // --- Exception free: decref message, free(exception) ---
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

        // --- Closure free: free(closure) ---
        {
            IRBuilder<> bc(is_closure_bb);
            auto* free_fn = m_module.getFunction("free");
            bc.CreateCall(free_fn, {obj_ptr});
            bc.CreateRetVoid();
        }

        // --- BoundMethod free: decref receiver, decref method_closure, free ---
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

        // --- Default free: just free the pointer ---
        {
            IRBuilder<> bd(default_bb);
            auto* free_fn = m_module.getFunction("free");
            bd.CreateCall(free_fn, {obj_ptr});
            bd.CreateRetVoid();
        }
    }

    // --- incref(AngaraObject val) ---
    // If val.type == TAG_OBJ, increment ref_count.
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
        // Extract pointer from payload
        auto* payload = b2.CreateExtractValue(val, {1}, "payload");
        auto* ptr_i64 = b2.CreateBitCast(payload, i64_ty, "ptr_as_i64");
        auto* obj_ptr = b2.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0), "obj_ptr");
        // ref_count is at offset 1 in header
        auto* rc_addr = b2.CreateStructGEP(m_obj_header_type, obj_ptr, 1);
        auto* rc = b2.CreateLoad(i64_ty, rc_addr, "rc");
        auto* new_rc = b2.CreateAdd(rc, ConstantInt::get(i64_ty, 1), "new_rc");
        b2.CreateStore(new_rc, rc_addr);
        b2.CreateBr(done_bb);

        IRBuilder<> b3(done_bb);
        b3.CreateRetVoid();
    }

    // --- decref(AngaraObject val) ---
    // If val.type == TAG_OBJ, decrement ref_count; if 0, call __ang_free_object.
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

// ============================================================================
// String Operations
// ============================================================================

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

// ============================================================================
// List Operations
// ============================================================================

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

    // --- list_new() -> AngaraObject ---
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

        // Header
        auto* header_ptr = b.CreateStructGEP(m_list_type, list_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_LIST),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i64_ty, 1),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));

        // count = 0, capacity = 0, elements = null
        b.CreateStore(ConstantInt::get(i64_ty, 0), b.CreateStructGEP(m_list_type, list_ptr, 1));
        b.CreateStore(ConstantInt::get(i64_ty, 0), b.CreateStructGEP(m_list_type, list_ptr, 2));
        b.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            b.CreateStructGEP(m_list_type, list_ptr, 3));

        b.CreateRet(pack_obj(b, list_ptr));
    }

    // --- list_new_with_elements(i64 count, AngaraObject* elems) -> AngaraObject ---
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

        // Allocate elements array: count * sizeof(AngaraObject)
        auto* elem_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(obj_ty));
        auto* total = b.CreateMul(count, elem_size);
        auto* elems_mem = b.CreateCall(malloc_fn, {total});
        // memcpy from source
        b.CreateCall(m_module.getFunction("memcpy"),
            {elems_mem, b.CreateBitCast(elems, i8_ptr), total});
        b.CreateStore(b.CreateBitCast(elems_mem, PointerType::get(m_ctx, 0)),
            b.CreateStructGEP(m_list_type, list_ptr, 3));

        // Incref all elements
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

    // --- list_push(AngaraObject list, AngaraObject val) ---
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

        // Extract list pointer
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

        // Grow: new_cap = max(count + 1, cap * 2)
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

        // Store the element
        IRBuilder<> bs(store_bb);
        // Re-read count and elems (may have changed in grow)
        auto* count2 = bs.CreateLoad(i64_ty, count_addr, "count2");
        auto* elems2 = bs.CreateLoad(PointerType::get(m_ctx, 0), elems_addr, "elems2");
        auto* elem_ptr = bs.CreateGEP(obj_ty, elems2, {count2});
        bs.CreateStore(val_arg, elem_ptr);
        // Incref the pushed value
        bs.CreateCall(m_module.getFunction("__ang_incref"), {val_arg});
        // count++
        bs.CreateStore(bs.CreateAdd(count2, ConstantInt::get(i64_ty, 1)), count_addr);
        bs.CreateRetVoid();
    }

    // --- list_get(AngaraObject list, AngaraObject index) -> AngaraObject ---
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

        // Extract index as i64
        auto* idx_payload = b.CreateExtractValue(idx_arg, {1});
        auto* idx = b.CreateBitCast(idx_payload, i64_ty, "idx");

        // Extract list pointer
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
        // nil for out of bounds
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = bd.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = bd.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        phi->addIncoming(nil_val, entry);
        bd.CreateRet(phi);
    }

    // --- list_set(AngaraObject list, AngaraObject index, AngaraObject val) ---
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

        // Decref old, store new, incref new
        auto* old = b.CreateLoad(obj_ty, elem_ptr, "old");
        b.CreateCall(m_module.getFunction("__ang_decref"), {old});
        b.CreateStore(val_arg, elem_ptr);
        b.CreateCall(m_module.getFunction("__ang_incref"), {val_arg});

        b.CreateRetVoid();
    }
}

// ============================================================================
// Record Operations
// ============================================================================

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

    // --- record_new() -> AngaraObject ---
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

    // --- record_get(AngaraObject record, i8* key) -> AngaraObject ---
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
        // Return nil
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = bnf.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = bnf.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        bnf.CreateRet(nil_val);
    }

    // --- record_set(AngaraObject record, i8* key, AngaraObject val) ---
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

        // Linear search for existing key
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

        // Found: update value (decref old, incref new)
        IRBuilder<> bf(found_bb);
        auto* found_entry = bf.CreateGEP(m_record_entry_type, entries, {i_phi});
        auto* val_addr = bf.CreateStructGEP(m_record_entry_type, found_entry, 1);
        auto* old_val = bf.CreateLoad(obj_ty, val_addr, "old");
        bf.CreateCall(m_module.getFunction("__ang_decref"), {old_val});
        bf.CreateStore(val_arg, val_addr);
        bf.CreateCall(m_module.getFunction("__ang_incref"), {val_arg});
        bf.CreateBr(done_bb);

        // Not found: insert new entry
        IRBuilder<> bnf(not_found_bb);
        auto* cap = bnf.CreateLoad(i64_ty, cap_addr, "cap");
        auto* need_grow = bnf.CreateICmpEQ(count, cap);
        bnf.CreateCondBr(need_grow, grow_bb, insert_bb);

        // Grow entries array
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

        // Insert the new entry
        IRBuilder<> bi(insert_bb);
        auto* cur_entries = bi.CreateLoad(PointerType::get(m_ctx, 0), entries_addr);
        auto* cur_count = bi.CreateLoad(i64_ty, count_addr);
        auto* new_entry = bi.CreateGEP(m_record_entry_type, cur_entries, {cur_count});

        // key = strdup(key_arg)
        auto* strdup_fn = m_module.getFunction("strdup");
        auto* copied_key = bi.CreateCall(strdup_fn, {key_arg});
        bi.CreateStore(copied_key, bi.CreateStructGEP(m_record_entry_type, new_entry, 0));

        // value = val_arg (with incref)
        bi.CreateStore(val_arg, bi.CreateStructGEP(m_record_entry_type, new_entry, 1));
        bi.CreateCall(m_module.getFunction("__ang_incref"), {val_arg});

        // count++
        bi.CreateStore(bi.CreateAdd(cur_count, ConstantInt::get(i64_ty, 1)), count_addr);
        bi.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }
}

// ============================================================================
// Type Conversions
// ============================================================================

void RuntimeBuilder::generateConversions() {
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* f64_ty = Type::getDoubleTy(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    // Helper: create nil
    auto make_nil = [&](IRBuilder<>& b) -> Value* {
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = b.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        return nil_val;
    };

    // --- to_i64(AngaraObject val) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_to_i64", fn_ty);
        m_fn_to_i64 = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0}, "tag");

        auto* is_i64_bb = BasicBlock::Create(m_ctx, "is_i64", fn);
        auto* is_f64_bb = BasicBlock::Create(m_ctx, "is_f64", fn);
        auto* is_bool_bb = BasicBlock::Create(m_ctx, "is_bool", fn);
        auto* default_bb = BasicBlock::Create(m_ctx, "default", fn);

        auto* sw = b.CreateSwitch(tag, default_bb, 3);
        sw->addCase(ConstantInt::get(i32_ty, TAG_I64), is_i64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_F64), is_f64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), is_bool_bb);

        // i64: return as-is
        IRBuilder<> bi(is_i64_bb);
        bi.CreateRet(val);

        // f64: truncate to i64 and pack
        IRBuilder<> bf(is_f64_bb);
        auto* fval = bf.CreateBitCast(bf.CreateExtractValue(val, {1}), f64_ty);
        auto* ival = bf.CreateFPToSI(fval, i64_ty);
        Value* result = UndefValue::get(obj_ty);
        result = bf.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_I64), {0});
        result = bf.CreateInsertValue(result, ival, {1});
        bf.CreateRet(result);

        // bool: zext to i64
        IRBuilder<> bb(is_bool_bb);
        auto* bval = bb.CreateTrunc(bb.CreateBitCast(bb.CreateExtractValue(val, {1}), i64_ty), Type::getInt1Ty(m_ctx));
        auto* extended = bb.CreateZExt(bval, i64_ty);
        Value* result2 = UndefValue::get(obj_ty);
        result2 = bb.CreateInsertValue(result2, ConstantInt::get(i32_ty, TAG_I64), {0});
        result2 = bb.CreateInsertValue(result2, extended, {1});
        bb.CreateRet(result2);

        IRBuilder<> bd(default_bb);
        bd.CreateRet(make_nil(bd));
    }

    // --- to_f64(AngaraObject val) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_to_f64", fn_ty);
        m_fn_to_f64 = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0});

        auto* is_f64_bb = BasicBlock::Create(m_ctx, "is_f64", fn);
        auto* is_i64_bb = BasicBlock::Create(m_ctx, "is_i64", fn);
        auto* default_bb = BasicBlock::Create(m_ctx, "default", fn);

        auto* sw = b.CreateSwitch(tag, default_bb, 2);
        sw->addCase(ConstantInt::get(i32_ty, TAG_F64), is_f64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_I64), is_i64_bb);

        IRBuilder<> bf(is_f64_bb);
        bf.CreateRet(val);

        IRBuilder<> bi(is_i64_bb);
        auto* ival = bi.CreateBitCast(bi.CreateExtractValue(val, {1}), i64_ty);
        auto* fval = bi.CreateSIToFP(ival, f64_ty);
        Value* result = UndefValue::get(obj_ty);
        result = bi.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_F64), {0});
        result = bi.CreateInsertValue(result, bi.CreateBitCast(fval, i64_ty), {1});
        bi.CreateRet(result);

        IRBuilder<> bd(default_bb);
        bd.CreateRet(make_nil(bd));
    }

    // --- to_bool(AngaraObject val) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_to_bool", fn_ty);
        m_fn_to_bool = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // For simplicity, delegate to truthiness check and pack as bool
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0}, "tag");
        auto* payload = b.CreateExtractValue(val, {1}, "payload");

        auto* nil_bb = BasicBlock::Create(m_ctx, "nil", fn);
        auto* bool_bb = BasicBlock::Create(m_ctx, "bool", fn);
        auto* i64_bb = BasicBlock::Create(m_ctx, "i64", fn);
        auto* f64_bb = BasicBlock::Create(m_ctx, "f64", fn);
        auto* obj_bb = BasicBlock::Create(m_ctx, "obj", fn);

        auto* sw = b.CreateSwitch(tag, nil_bb, 5);
        sw->addCase(ConstantInt::get(i32_ty, TAG_NIL), nil_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), bool_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_I64), i64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_F64), f64_bb);
        sw->addCase(ConstantInt::get(i32_ty, TAG_OBJ), obj_bb);

        auto pack_bool = [&](IRBuilder<>& b, Value* bool_val) -> Value* {
            auto* extended = b.CreateZExt(bool_val, i64_ty);
            Value* result = UndefValue::get(obj_ty);
            result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_BOOL), {0});
            result = b.CreateInsertValue(result, extended, {1});
            return result;
        };

        IRBuilder<> bn(nil_bb);
        bn.CreateRet(pack_bool(bn, ConstantInt::get(Type::getInt1Ty(m_ctx), 0)));

        IRBuilder<> bb(bool_bb);
        auto* bval = bb.CreateTrunc(bb.CreateBitCast(payload, i64_ty), Type::getInt1Ty(m_ctx));
        bb.CreateRet(pack_bool(bb, bval));

        IRBuilder<> bi(i64_bb);
        auto* ival = bi.CreateBitCast(payload, i64_ty);
        auto* nonzero = bi.CreateICmpNE(ival, ConstantInt::get(i64_ty, 0));
        bi.CreateRet(pack_bool(bi, nonzero));

        IRBuilder<> bf(f64_bb);
        auto* dval = bf.CreateBitCast(payload, f64_ty);
        auto* nonzero_f = bf.CreateFCmpUNE(dval, ConstantFP::get(f64_ty, 0.0));
        bf.CreateRet(pack_bool(bf, nonzero_f));

        IRBuilder<> bo(obj_bb);
        // Objects are truthy (simplified)
        bo.CreateRet(pack_bool(bo, ConstantInt::get(Type::getInt1Ty(m_ctx), 1)));
    }

    // --- typeof(AngaraObject val) -> AngaraObject (string) ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_typeof", fn_ty);
        m_fn_typeof = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0});

        auto* nil_bb = BasicBlock::Create(m_ctx, "nil", fn);
        auto* bool_bb = BasicBlock::Create(m_ctx, "bool", fn);
        auto* i64_bb = BasicBlock::Create(m_ctx, "i64", fn);
        auto* f64_bb = BasicBlock::Create(m_ctx, "f64", fn);
        auto* obj_bb = BasicBlock::Create(m_ctx, "obj", fn);

        {
            auto* sw = b.CreateSwitch(tag, nil_bb, 5);
            sw->addCase(ConstantInt::get(i32_ty, TAG_NIL), nil_bb);
            sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), bool_bb);
            sw->addCase(ConstantInt::get(i32_ty, TAG_I64), i64_bb);
            sw->addCase(ConstantInt::get(i32_ty, TAG_F64), f64_bb);
            sw->addCase(ConstantInt::get(i32_ty, TAG_OBJ), obj_bb);
        }

        auto* str_from_c = m_module.getFunction("__ang_string_from_c");

        IRBuilder<> bn(nil_bb);
        bn.CreateRet(bn.CreateCall(str_from_c, {bn.CreateGlobalString("nil")}));

        IRBuilder<> bb(bool_bb);
        bb.CreateRet(bb.CreateCall(str_from_c, {bb.CreateGlobalString("bool")}));

        IRBuilder<> bi(i64_bb);
        bi.CreateRet(bi.CreateCall(str_from_c, {bi.CreateGlobalString("i64")}));

        IRBuilder<> bf(f64_bb);
        bf.CreateRet(bf.CreateCall(str_from_c, {bf.CreateGlobalString("f64")}));

        IRBuilder<> bo(obj_bb);
        bo.CreateRet(bo.CreateCall(str_from_c, {bo.CreateGlobalString("object")}));
    }
}

// ============================================================================
// Equality
// ============================================================================

void RuntimeBuilder::generateEquality() {
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* f64_ty = Type::getDoubleTy(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto pack_bool = [&](IRBuilder<>& b, Value* bool_val) -> Value* {
        auto* extended = b.CreateZExt(bool_val, i64_ty);
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_BOOL), {0});
        result = b.CreateInsertValue(result, extended, {1});
        return result;
    };

    // --- equals(AngaraObject a, AngaraObject b) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_equals", fn_ty);
        m_fn_equals = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> bb(entry);
        auto* a = fn->arg_begin();
        auto* b_arg = fn->arg_begin() + 1;

        auto* tag_a = bb.CreateExtractValue(a, {0}, "tag_a");
        auto* tag_b = bb.CreateExtractValue(b_arg, {0}, "tag_b");

        // If tags differ, check for i64/f64 cross-comparison
        auto* tags_eq = bb.CreateICmpEQ(tag_a, tag_b, "tags_eq");
        auto* both_num = bb.CreateOr(
            bb.CreateAnd(
                bb.CreateICmpEQ(tag_a, ConstantInt::get(i32_ty, TAG_I64)),
                bb.CreateICmpEQ(tag_b, ConstantInt::get(i32_ty, TAG_F64))),
            bb.CreateAnd(
                bb.CreateICmpEQ(tag_a, ConstantInt::get(i32_ty, TAG_F64)),
                bb.CreateICmpEQ(tag_b, ConstantInt::get(i32_ty, TAG_I64)))
        );
        auto* can_compare = bb.CreateOr(tags_eq, both_num, "can_compare");
        auto* not_eq_bb = BasicBlock::Create(m_ctx, "not_eq", fn);
        auto* check_same_bb = BasicBlock::Create(m_ctx, "check_same", fn);
        bb.CreateCondBr(can_compare, check_same_bb, not_eq_bb);

        IRBuilder<> bns(not_eq_bb);
        bns.CreateRet(pack_bool(bns, ConstantInt::get(Type::getInt1Ty(m_ctx), 0)));

        IRBuilder<> bcs(check_same_bb);
        auto* payload_a = bcs.CreateExtractValue(a, {1});
        auto* payload_b = bcs.CreateExtractValue(b_arg, {1});

        // Same tag: compare payloads
        auto* same_tag_bb = BasicBlock::Create(m_ctx, "same_tag", fn);
        auto* cross_num_bb = BasicBlock::Create(m_ctx, "cross_num", fn);
        bcs.CreateCondBr(tags_eq, same_tag_bb, cross_num_bb);

        // Same tag comparison
        IRBuilder<> bst(same_tag_bb);
        auto* nil_eq_bb = BasicBlock::Create(m_ctx, "nil_eq", fn);
        auto* sw = bst.CreateSwitch(tag_a, nil_eq_bb, 5);

        // nil == nil → true
        IRBuilder<> bne(nil_eq_bb);
        bne.CreateRet(pack_bool(bne, ConstantInt::get(Type::getInt1Ty(m_ctx), 1)));

        // bool comparison
        auto* bool_eq_bb = BasicBlock::Create(m_ctx, "bool_eq", fn);
        sw->addCase(ConstantInt::get(i32_ty, TAG_BOOL), bool_eq_bb);
        {
            IRBuilder<> bbe(bool_eq_bb);
            auto* ba = bbe.CreateTrunc(bbe.CreateBitCast(payload_a, i64_ty), Type::getInt1Ty(m_ctx));
            auto* bb2 = bbe.CreateTrunc(bbe.CreateBitCast(payload_b, i64_ty), Type::getInt1Ty(m_ctx));
            bbe.CreateRet(pack_bool(bbe, bbe.CreateICmpEQ(ba, bb2)));
        }

        // i64 comparison
        auto* i64_eq_bb = BasicBlock::Create(m_ctx, "i64_eq", fn);
        sw->addCase(ConstantInt::get(i32_ty, TAG_I64), i64_eq_bb);
        {
            IRBuilder<> bie(i64_eq_bb);
            auto* ia = bie.CreateBitCast(payload_a, i64_ty);
            auto* ib = bie.CreateBitCast(payload_b, i64_ty);
            bie.CreateRet(pack_bool(bie, bie.CreateICmpEQ(ia, ib)));
        }

        // f64 comparison
        auto* f64_eq_bb = BasicBlock::Create(m_ctx, "f64_eq", fn);
        sw->addCase(ConstantInt::get(i32_ty, TAG_F64), f64_eq_bb);
        {
            IRBuilder<> bfe(f64_eq_bb);
            auto* da = bfe.CreateBitCast(payload_a, f64_ty);
            auto* db = bfe.CreateBitCast(payload_b, f64_ty);
            bfe.CreateRet(pack_bool(bfe, bfe.CreateFCmpOEQ(da, db)));
        }

        // obj comparison: pointer equality for now
        auto* obj_eq_bb = BasicBlock::Create(m_ctx, "obj_eq", fn);
        sw->addCase(ConstantInt::get(i32_ty, TAG_OBJ), obj_eq_bb);
        {
            IRBuilder<> boe(obj_eq_bb);
            auto* pa = boe.CreateBitCast(payload_a, i64_ty);
            auto* pb = boe.CreateBitCast(payload_b, i64_ty);
            // For strings, compare content
            auto* is_string_bb = BasicBlock::Create(m_ctx, "is_str_eq", fn);
            auto* ptr_eq_bb = BasicBlock::Create(m_ctx, "ptr_eq", fn);
            // Check if both are strings
            auto* ptr_a = boe.CreateIntToPtr(pa, PointerType::get(m_ctx, 0));
            auto* obj_type_a = boe.CreateLoad(i32_ty, boe.CreateStructGEP(m_obj_header_type, ptr_a, 0));
            auto* is_str_a = boe.CreateICmpEQ(obj_type_a, ConstantInt::get(i32_ty, OBJ_STRING));
            auto* ptr_b = boe.CreateIntToPtr(pb, PointerType::get(m_ctx, 0));
            auto* obj_type_b = boe.CreateLoad(i32_ty, boe.CreateStructGEP(m_obj_header_type, ptr_b, 0));
            auto* is_str_b = boe.CreateICmpEQ(obj_type_b, ConstantInt::get(i32_ty, OBJ_STRING));
            auto* both_str = boe.CreateAnd(is_str_a, is_str_b);
            boe.CreateCondBr(both_str, is_string_bb, ptr_eq_bb);

            // String comparison
            IRBuilder<> bse(is_string_bb);
            auto* str_a = bse.CreateIntToPtr(pa, PointerType::get(m_ctx, 0));
            auto* str_b = bse.CreateIntToPtr(pb, PointerType::get(m_ctx, 0));
            auto* chars_a = bse.CreateLoad(i8_ptr, bse.CreateStructGEP(m_string_type, str_a, 2));
            auto* chars_b = bse.CreateLoad(i8_ptr, bse.CreateStructGEP(m_string_type, str_b, 2));
            auto* strcmp_fn = m_module.getFunction("strcmp");
            auto* cmp = bse.CreateCall(strcmp_fn, {chars_a, chars_b});
            auto* eq = bse.CreateICmpEQ(cmp, ConstantInt::get(i32_ty, 0));
            bse.CreateRet(pack_bool(bse, eq));

            // Pointer equality for other objects
            IRBuilder<> bpe(ptr_eq_bb);
            bpe.CreateRet(pack_bool(bpe, bpe.CreateICmpEQ(pa, pb)));
        }

        // Cross numeric comparison (i64 vs f64)
        IRBuilder<> bcn(cross_num_bb);
        auto* ia = bcn.CreateBitCast(payload_a, i64_ty);
        auto* da = bcn.CreateSIToFP(ia, f64_ty);
        auto* db = bcn.CreateBitCast(payload_b, f64_ty);
        // Determine which is i64 and which is f64
        auto* a_is_i64 = bcn.CreateICmpEQ(tag_a, ConstantInt::get(i32_ty, TAG_I64));
        auto* a_as_f64 = bcn.CreateSelect(a_is_i64, da, bcn.CreateBitCast(payload_a, f64_ty));
        auto* b_as_f64 = bcn.CreateSelect(a_is_i64, db, bcn.CreateBitCast(payload_b, f64_ty));
        bcn.CreateRet(pack_bool(bcn, bcn.CreateFCmpOEQ(a_as_f64, b_as_f64)));
    }
}

// ============================================================================
// Deep Clone (stub — returns the value with incref for heap objects)
// ============================================================================

void RuntimeBuilder::generateDeepClone() {
    auto* obj_ty = m_angara_obj_type;
    auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
    auto* fn = createRuntimeFunc("__ang_deep_clone", fn_ty);
    m_fn_deep_clone = FunctionCallee(fn);

    auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
    IRBuilder<> b(entry);
    auto* val = fn->arg_begin();

    // For now: incref and return (shallow clone)
    auto* incref_fn = m_module.getFunction("__ang_incref");
    b.CreateCall(incref_fn, {val});
    b.CreateRet(val);
}

// ============================================================================
// Closure Operations
// ============================================================================

void RuntimeBuilder::generateClosureOps() {
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i1_ty = Type::getInt1Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto* malloc_fn = m_module.getFunction("malloc");

    auto pack_obj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* ptr_i8 = b.CreateBitCast(raw_ptr, i8_ptr);
        auto* ptr_i64 = b.CreatePtrToInt(ptr_i8, i64_ty);
        auto* payload = ptr_i64;
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        result = b.CreateInsertValue(result, payload, {1});
        return result;
    };

    // --- closure_new(void* fn, i32 arity, i1 is_native) -> AngaraObject ---
    {
        auto* fn_ptr_type = PointerType::get(m_ctx, 0);
        auto* fn_ty = FunctionType::get(obj_ty, {fn_ptr_type, i32_ty, i1_ty}, false);
        auto* fn = createRuntimeFunc("__ang_closure_new", fn_ty);
        m_fn_closure_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* fn_arg = fn->arg_begin();
        auto* arity_arg = fn->arg_begin() + 1;
        auto* native_arg = fn->arg_begin() + 2;

        auto* closure_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_closure_type));
        auto* mem = b.CreateCall(malloc_fn, {closure_size});
        auto* closure_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_closure_type, closure_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_CLOSURE),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i64_ty, 1),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(fn_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 1));
        b.CreateStore(arity_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 2));
        b.CreateStore(native_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 3));

        b.CreateRet(pack_obj(b, closure_ptr));
    }

    // --- call(AngaraObject callee, i32 argc, AngaraObject* args) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, i32_ty, PointerType::get(m_ctx, 0)}, false);
        auto* fn = createRuntimeFunc("__ang_call", fn_ty);
        m_fn_call = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* is_closure_bb = BasicBlock::Create(m_ctx, "is_closure", fn);
        auto* is_bound_bb = BasicBlock::Create(m_ctx, "is_bound", fn);
        auto* error_bb = BasicBlock::Create(m_ctx, "error", fn);

        IRBuilder<> b(entry);
        auto* callee = fn->arg_begin();
        auto* argc = fn->arg_begin() + 1;
        auto* args = fn->arg_begin() + 2;

        auto* payload = b.CreateExtractValue(callee, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* obj_ptr = b.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* obj_type = b.CreateLoad(i32_ty, b.CreateStructGEP(m_obj_header_type, obj_ptr, 0));

        auto* sw = b.CreateSwitch(obj_type, error_bb, 2);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_CLOSURE), is_closure_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD), is_bound_bb);

        // Closure call
        IRBuilder<> bc(is_closure_bb);
        auto* closure_ptr = bc.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* fn_field = bc.CreateLoad(
            PointerType::get(m_ctx, 0),
            bc.CreateStructGEP(m_closure_type, closure_ptr, 1), "fn");
        auto* closure_fn_ty = FunctionType::get(obj_ty, {i32_ty, PointerType::get(m_ctx, 0)}, false);
        auto* result = bc.CreateCall(closure_fn_ty, fn_field, {argc, args});
        bc.CreateRet(result);

        // Bound method call: prepend receiver to args
        IRBuilder<> bbm(is_bound_bb);
        auto* bm_ptr = bbm.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* receiver = bbm.CreateLoad(obj_ty, bbm.CreateStructGEP(m_bound_method_type, bm_ptr, 1));
        auto* method = bbm.CreateLoad(obj_ty, bbm.CreateStructGEP(m_bound_method_type, bm_ptr, 2));

        // Allocate new args array: argc + 1
        auto* new_argc = bbm.CreateAdd(argc, ConstantInt::get(i32_ty, 1));
        auto* elem_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(obj_ty));
        auto* new_args_size = bbm.CreateSExt(new_argc, i64_ty);
        auto* alloc_size = bbm.CreateMul(new_args_size, elem_size);
        auto* new_args = bbm.CreateCall(malloc_fn, {alloc_size});
        auto* new_args_typed = bbm.CreateBitCast(new_args, PointerType::get(m_ctx, 0));
        // Store receiver at [0]
        bbm.CreateStore(receiver, new_args_typed);
        // Copy original args starting at [1]
        bbm.CreateCall(m_module.getFunction("memcpy"),
            {bbm.CreateGEP(obj_ty, new_args_typed, {ConstantInt::get(i64_ty, 1)}),
             bbm.CreateBitCast(args, i8_ptr),
             bbm.CreateMul(bbm.CreateSExt(argc, i64_ty), elem_size)});

        // Get closure fn from method
        auto* m_payload = bbm.CreateExtractValue(method, {1});
        auto* m_ptr_i64 = bbm.CreateBitCast(m_payload, i64_ty);
        auto* m_closure_ptr = bbm.CreateIntToPtr(m_ptr_i64, PointerType::get(m_ctx, 0));
        auto* m_fn = bbm.CreateLoad(
            PointerType::get(m_ctx, 0),
            bbm.CreateStructGEP(m_closure_type, m_closure_ptr, 1));
        auto* method_fn_ty = FunctionType::get(obj_ty, {i32_ty, PointerType::get(m_ctx, 0)}, false);
        auto* call_result = bbm.CreateCall(method_fn_ty, m_fn, {new_argc, new_args_typed});
        bbm.CreateCall(m_module.getFunction("free"), {new_args});
        bbm.CreateRet(call_result);

        // Error: return nil
        IRBuilder<> be(error_bb);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = be.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = be.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        be.CreateRet(nil_val);
    }

    // --- bound_method_new(AngaraObject receiver, AngaraObject closure) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_bound_method_new", fn_ty);
        m_fn_bound_method_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* recv_arg = fn->arg_begin();
        auto* closure_arg = fn->arg_begin() + 1;

        auto* bm_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_bound_method_type));
        auto* mem = b.CreateCall(malloc_fn, {bm_size});
        auto* bm_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_bound_method_type, bm_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i64_ty, 1),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(recv_arg, b.CreateStructGEP(m_bound_method_type, bm_ptr, 1));
        b.CreateStore(closure_arg, b.CreateStructGEP(m_bound_method_type, bm_ptr, 2));

        // Incref receiver and closure
        b.CreateCall(m_module.getFunction("__ang_incref"), {recv_arg});
        b.CreateCall(m_module.getFunction("__ang_incref"), {closure_arg});

        b.CreateRet(pack_obj(b, bm_ptr));
    }
}

// ============================================================================
// Exception Operations
// ============================================================================

void RuntimeBuilder::generateExceptionOps() {
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto* malloc_fn = m_module.getFunction("malloc");

    auto pack_obj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* ptr_i8 = b.CreateBitCast(raw_ptr, i8_ptr);
        auto* ptr_i64 = b.CreatePtrToInt(ptr_i8, i64_ty);
        auto* payload = ptr_i64;
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        result = b.CreateInsertValue(result, payload, {1});
        return result;
    };

    // --- exception_new(AngaraObject message) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_exception_new", fn_ty);
        m_fn_exception_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* msg = fn->arg_begin();

        auto* exc_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_exception_type));
        auto* mem = b.CreateCall(malloc_fn, {exc_size});
        auto* exc_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_exception_type, exc_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_EXCEPTION),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i64_ty, 1),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(msg, b.CreateStructGEP(m_exception_type, exc_ptr, 1));
        b.CreateCall(m_module.getFunction("__ang_incref"), {msg});

        b.CreateRet(pack_obj(b, exc_ptr));
    }

    // --- throw(AngaraObject exception) — calls longjmp ---
    {
        auto* fn_ty = FunctionType::get(Type::getVoidTy(m_ctx), {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_throw", fn_ty);
        m_fn_throw = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* exc = fn->arg_begin();

        // Store exception in global
        b.CreateStore(exc, m_g_current_exception);

        // Load chain head, if null → abort
        auto* chain = b.CreateLoad(i8_ptr, m_g_exception_chain, "chain");
        auto* is_null = b.CreateICmpEQ(chain, ConstantPointerNull::get(i8_ptr));

        auto* abort_bb = BasicBlock::Create(m_ctx, "abort", fn);
        auto* unwind_bb = BasicBlock::Create(m_ctx, "unwind", fn);
        b.CreateCondBr(is_null, abort_bb, unwind_bb);

        IRBuilder<> ba(abort_bb);
        // Print error and exit
        auto* msg = ba.CreateGlobalString("Unhandled exception\n");
        auto* fprintf_fn = m_module.getFunction("fprintf");
        // stderr is typically at a fixed address, but we can't easily get it.
        // Use printf instead
        ba.CreateCall(m_module.getFunction("printf"), {msg});
        ba.CreateCall(m_module.getFunction("exit"), {ConstantInt::get(i32_ty, 1)});
        ba.CreateUnreachable();

        IRBuilder<> bu(unwind_bb);
        // Pop frame from chain
        // chain points to ExceptionFrame which is { jmp_buf, prev* }
        // We need to read prev and update chain, then longjmp
        // ExceptionFrame layout: first field is jmp_buf buffer, second is prev pointer
        // jmp_buf is opaque — we treat it as [200 x i8] (typical size)
        auto* frame_type = StructType::create(m_ctx, {
            ArrayType::get(i8_ty, 200),   // jmp_buf
            i8_ptr                        // prev
        }, "ExceptionFrame");

        auto* frame = bu.CreateBitCast(chain, PointerType::get(m_ctx, 0));
        auto* prev_ptr = bu.CreateStructGEP(frame_type, frame, 1);
        auto* prev = bu.CreateLoad(i8_ptr, prev_ptr, "prev");
        bu.CreateStore(prev, m_g_exception_chain);

        auto* jmp_buf_ptr = bu.CreateStructGEP(frame_type, frame, 0);
        bu.CreateCall(m_module.getFunction("longjmp"), {
            bu.CreateBitCast(jmp_buf_ptr, i8_ptr),
            ConstantInt::get(i32_ty, 1)
        });
        bu.CreateUnreachable();
    }

    // --- try_begin(i8* frame) -> i32 (returns setjmp result) ---
    {
        auto* fn_ty = FunctionType::get(i32_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_try_begin", fn_ty);
        m_fn_try_begin = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* frame_arg = fn->arg_begin();

        // Push frame onto chain: frame->prev = chain_head, chain_head = frame
        auto* frame_type = StructType::create(m_ctx, {
            ArrayType::get(i8_ty, 200),
            i8_ptr
        }, "ExceptionFrame");

        auto* frame = b.CreateBitCast(frame_arg, PointerType::get(m_ctx, 0));
        auto* prev_addr = b.CreateStructGEP(frame_type, frame, 1);
        auto* old_chain = b.CreateLoad(i8_ptr, m_g_exception_chain, "old_chain");
        b.CreateStore(old_chain, prev_addr);
        b.CreateStore(frame_arg, m_g_exception_chain);

        // Call setjmp
        auto* jmp_buf_ptr = b.CreateStructGEP(frame_type, frame, 0);
        auto* result = b.CreateCall(m_module.getFunction("setjmp"),
            {b.CreateBitCast(jmp_buf_ptr, i8_ptr)}, "setjmp_result");
        b.CreateRet(result);
    }

    // --- try_end() — pops the exception frame ---
    {
        auto* fn_ty = FunctionType::get(Type::getVoidTy(m_ctx), {}, false);
        auto* fn = createRuntimeFunc("__ang_try_end", fn_ty);
        m_fn_try_end = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        auto* frame_type = StructType::create(m_ctx, {
            ArrayType::get(i8_ty, 200),
            i8_ptr
        }, "ExceptionFrame");

        auto* chain = b.CreateLoad(i8_ptr, m_g_exception_chain, "chain");
        auto* frame = b.CreateBitCast(chain, PointerType::get(m_ctx, 0));
        auto* prev = b.CreateLoad(i8_ptr, b.CreateStructGEP(frame_type, frame, 1), "prev");
        b.CreateStore(prev, m_g_exception_chain);
        b.CreateRetVoid();
    }
}

// ============================================================================
// Thread Operations (stubs)
// ============================================================================

void RuntimeBuilder::generateThreadOps() {
    auto* obj_ty = m_angara_obj_type;
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);

    // Stubs that return nil
    auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, i32_ty, PointerType::get(m_ctx, 0)}, false);
    auto* fn = createRuntimeFunc("__ang_spawn_thread", fn_ty);
    m_fn_thread_spawn = FunctionCallee(fn);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = b.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        b.CreateRet(nil_val);
    }

    auto* fn_ty2 = FunctionType::get(obj_ty, {obj_ty}, false);
    auto* fn2 = createRuntimeFunc("__ang_thread_join", fn_ty2);
    m_fn_thread_join = FunctionCallee(fn2);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn2);
        IRBuilder<> b(entry);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = b.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        b.CreateRet(nil_val);
    }

    auto* fn_ty3 = FunctionType::get(obj_ty, {}, false);
    auto* fn3 = createRuntimeFunc("__ang_mutex_new", fn_ty3);
    m_fn_mutex_new = FunctionCallee(fn3);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn3);
        IRBuilder<> b(entry);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = b.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        b.CreateRet(nil_val);
    }

    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* fn_ty4 = FunctionType::get(void_ty, {obj_ty}, false);
    auto* fn4 = createRuntimeFunc("__ang_mutex_lock", fn_ty4);
    m_fn_mutex_lock = FunctionCallee(fn4);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn4);
        IRBuilder<> b(entry);
        b.CreateRetVoid();
    }

    auto* fn5 = createRuntimeFunc("__ang_mutex_unlock", fn_ty4);
    m_fn_mutex_unlock = FunctionCallee(fn5);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn5);
        IRBuilder<> b(entry);
        b.CreateRetVoid();
    }
}

// ============================================================================
// Miscellaneous Operations
// ============================================================================

void RuntimeBuilder::generateMiscOps() {
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto pack_obj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* ptr_i8 = b.CreateBitCast(raw_ptr, i8_ptr);
        auto* ptr_i64 = b.CreatePtrToInt(ptr_i8, i64_ty);
        auto* payload = ptr_i64;
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        result = b.CreateInsertValue(result, payload, {1});
        return result;
    };

    // --- len(AngaraObject collection) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_len", fn_ty);
        m_fn_len = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0}, "tag");

        auto* is_obj_bb = BasicBlock::Create(m_ctx, "is_obj", fn);
        auto* not_obj_bb = BasicBlock::Create(m_ctx, "not_obj", fn);
        auto* obj_is_string_bb = BasicBlock::Create(m_ctx, "obj_is_string", fn);
        auto* obj_is_list_bb = BasicBlock::Create(m_ctx, "obj_is_list", fn);
        auto* default_bb = BasicBlock::Create(m_ctx, "default", fn);

        auto* is_obj = b.CreateICmpEQ(tag, ConstantInt::get(i32_ty, TAG_OBJ));
        b.CreateCondBr(is_obj, is_obj_bb, not_obj_bb);

        // String or list
        IRBuilder<> bo(is_obj_bb);
        auto* payload = bo.CreateExtractValue(val, {1});
        auto* ptr_i64 = bo.CreateBitCast(payload, i64_ty);
        auto* obj_ptr = bo.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* obj_type = bo.CreateLoad(i32_ty, bo.CreateStructGEP(m_obj_header_type, obj_ptr, 0));
        auto* sw = bo.CreateSwitch(obj_type, default_bb, 2);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_STRING), obj_is_string_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_LIST), obj_is_list_bb);

        // String length
        IRBuilder<> bs(obj_is_string_bb);
        auto* str_ptr = bs.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* len = bs.CreateLoad(i64_ty, bs.CreateStructGEP(m_string_type, str_ptr, 1), "len");
        Value* result = UndefValue::get(obj_ty);
        result = bs.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_I64), {0});
        result = bs.CreateInsertValue(result, len, {1});
        bs.CreateRet(result);

        // List count
        IRBuilder<> bl(obj_is_list_bb);
        auto* list_ptr = bl.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* count = bl.CreateLoad(i64_ty, bl.CreateStructGEP(m_list_type, list_ptr, 1), "count");
        Value* result2 = UndefValue::get(obj_ty);
        result2 = bl.CreateInsertValue(result2, ConstantInt::get(i32_ty, TAG_I64), {0});
        result2 = bl.CreateInsertValue(result2, count, {1});
        bl.CreateRet(result2);

        // Not an object → return 0
        IRBuilder<> bn(not_obj_bb);
        Value* zero_val = UndefValue::get(obj_ty);
        zero_val = bn.CreateInsertValue(zero_val, ConstantInt::get(i32_ty, TAG_I64), {0});
        zero_val = bn.CreateInsertValue(zero_val,
            ConstantInt::get(i64_ty, 0), {1});
        bn.CreateRet(zero_val);

        IRBuilder<> bd(default_bb);
        bd.CreateRet(bn.CreateInsertValue(UndefValue::get(obj_ty),
            ConstantInt::get(i32_ty, TAG_I64), {0}));
    }
}

// ============================================================================
// IO Operations — intrinsic for the LLVM backend
// ============================================================================

void RuntimeBuilder::generateIOOps() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto* malloc_fn = m_module.getFunction("malloc");
    auto* realloc_fn = m_module.getFunction("realloc");
    auto* strlen_fn = m_module.getFunction("strlen");
    auto* str_from_c = m_module.getFunction("__ang_string_from_c");

    // Helper: extract the C string pointer from an AngaraObject string
    auto get_cstr = [&](IRBuilder<>& b, Value* str_obj) -> Value* {
        auto* payload = b.CreateExtractValue(str_obj, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* str_ptr = b.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* chars_ptr = b.CreateStructGEP(m_string_type, str_ptr, 2);
        return b.CreateLoad(i8_ptr, chars_ptr, "cstr");
    };

    // Helper: get FILE* from stream_id (1=stdout, 2=stderr)
    // We use a simple approach: call fprintf with the right FILE*
    // For simplicity, we inline the stdout/stderr selection

    // --- io_print(AngaraObject stream_id, AngaraObject value) ---
    // Converts to string and prints without newline to the given stream
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_io_print", fn_ty);
        m_fn_io_print = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* stream_arg = fn->arg_begin();
        auto* val = fn->arg_begin() + 1;

        // to_string(val)
        auto* to_str_fn = m_module.getFunction("__ang_to_string");
        auto* str_obj = b.CreateCall(to_str_fn, {val}, "str");
        // Get C string
        auto* cstr = get_cstr(b, str_obj);

        // Select stdout or stderr based on stream_id
        auto* stream_payload = b.CreateExtractValue(stream_arg, {1});
        auto* stream_id = b.CreateBitCast(stream_payload, i64_ty, "stream_id");
        auto* is_stderr = b.CreateICmpEQ(stream_id, ConstantInt::get(i64_ty, 2));

        auto* stdout_var = m_module.getOrInsertGlobal("__stdoutp",
            PointerType::get(m_ctx, 0));
        auto* stderr_var = m_module.getOrInsertGlobal("__stderrp",
            PointerType::get(m_ctx, 0));
        auto* file_ptr = b.CreateSelect(is_stderr,
            b.CreateLoad(PointerType::get(m_ctx, 0), stderr_var, "stderr"),
            b.CreateLoad(PointerType::get(m_ctx, 0), stdout_var, "stdout"));

        auto* fprintf_fn = m_module.getFunction("fprintf");
        auto* fmt = b.CreateGlobalString("%s");
        b.CreateCall(fprintf_fn, {file_ptr, fmt, cstr});
        // decref the temporary string
        b.CreateCall(m_module.getFunction("__ang_decref"), {str_obj});
        b.CreateRetVoid();
    }

    // --- io_println(AngaraObject stream_id, AngaraObject value) ---
    // Converts to string and prints with newline to the given stream
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_io_println", fn_ty);
        m_fn_io_println = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* stream_arg = fn->arg_begin();
        auto* val = fn->arg_begin() + 1;

        auto* to_str_fn = m_module.getFunction("__ang_to_string");
        auto* str_obj = b.CreateCall(to_str_fn, {val}, "str");
        auto* cstr = get_cstr(b, str_obj);

        // Select stdout or stderr based on stream_id
        auto* stream_payload = b.CreateExtractValue(stream_arg, {1});
        auto* stream_id = b.CreateBitCast(stream_payload, i64_ty, "stream_id");
        auto* is_stderr = b.CreateICmpEQ(stream_id, ConstantInt::get(i64_ty, 2));

        auto* stdout_var = m_module.getOrInsertGlobal("__stdoutp",
            PointerType::get(m_ctx, 0));
        auto* stderr_var = m_module.getOrInsertGlobal("__stderrp",
            PointerType::get(m_ctx, 0));
        auto* file_ptr = b.CreateSelect(is_stderr,
            b.CreateLoad(PointerType::get(m_ctx, 0), stderr_var, "stderr"),
            b.CreateLoad(PointerType::get(m_ctx, 0), stdout_var, "stdout"));

        auto* fprintf_fn = m_module.getFunction("fprintf");
        auto* fmt = b.CreateGlobalString("%s\n");
        b.CreateCall(fprintf_fn, {file_ptr, fmt, cstr});
        b.CreateCall(m_module.getFunction("__ang_decref"), {str_obj});
        b.CreateRetVoid();
    }

    // --- io_write(i64 stream_id, AngaraObject content) ---
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_io_write", fn_ty);
        m_fn_io_write = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* stream_arg = fn->arg_begin();
        auto* content_arg = fn->arg_begin() + 1;

        // Extract stream_id
        auto* stream_payload = b.CreateExtractValue(stream_arg, {1});
        auto* stream_id = b.CreateBitCast(stream_payload, i64_ty, "stream_id");

        // to_string(content)
        auto* to_str_fn = m_module.getFunction("__ang_to_string");
        auto* str_obj = b.CreateCall(to_str_fn, {content_arg}, "str");
        auto* cstr = get_cstr(b, str_obj);

        // fprintf(stream, "%s", cstr)
        // We use stdout/stderr based on stream_id
        auto* is_stderr = b.CreateICmpEQ(stream_id, ConstantInt::get(i64_ty, 2));

        // Declare stderr and stdout as external globals
        auto* stdout_var = m_module.getOrInsertGlobal("__stdoutp",
            PointerType::get(m_ctx, 0));
        auto* stderr_var = m_module.getOrInsertGlobal("__stderrp",
            PointerType::get(m_ctx, 0));

        // macOS uses __stdoutp/__stderrp. We'll also declare the standard ones.
        auto* file_ptr = b.CreateSelect(is_stderr,
            b.CreateLoad(PointerType::get(m_ctx, 0), stderr_var, "stderr"),
            b.CreateLoad(PointerType::get(m_ctx, 0), stdout_var, "stdout"));

        auto* fprintf_fn = m_module.getFunction("fprintf");
        auto* fmt = b.CreateGlobalString("%s");
        b.CreateCall(fprintf_fn, {file_ptr, fmt, cstr});
        b.CreateCall(m_module.getFunction("__ang_decref"), {str_obj});
        b.CreateRetVoid();
    }

    // --- io_flush(i64 stream_id) ---
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_io_flush", fn_ty);
        m_fn_io_flush = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* stream_arg = fn->arg_begin();

        // Declare fflush
        FunctionType* fflush_ty = FunctionType::get(i32_ty, {PointerType::get(m_ctx, 0)}, false);
        auto fflush_fn = m_module.getOrInsertFunction("fflush", fflush_ty);

        auto* stream_payload = b.CreateExtractValue(stream_arg, {1});
        auto* stream_id = b.CreateBitCast(stream_payload, i64_ty, "stream_id");
        auto* is_stderr = b.CreateICmpEQ(stream_id, ConstantInt::get(i64_ty, 2));

        auto* stdout_var = m_module.getOrInsertGlobal("__stdoutp",
            PointerType::get(m_ctx, 0));
        auto* stderr_var = m_module.getOrInsertGlobal("__stderrp",
            PointerType::get(m_ctx, 0));

        auto* file_ptr = b.CreateSelect(is_stderr,
            b.CreateLoad(PointerType::get(m_ctx, 0), stderr_var),
            b.CreateLoad(PointerType::get(m_ctx, 0), stdout_var));

        b.CreateCall(fflush_fn, {file_ptr});
        b.CreateRetVoid();
    }

    // --- io_read_line() -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_io_read_line", fn_ty);
        m_fn_io_read_line = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* ok_bb = BasicBlock::Create(m_ctx, "ok", fn);
        auto* eof_bb = BasicBlock::Create(m_ctx, "eof", fn);

        IRBuilder<> b(entry);

        // Declare getline: ssize_t getline(char **lineptr, size_t *n, FILE *stream)
        auto* ssize_ty = i64_ty; // ssize_t is i64 on macOS
        auto* size_ty = i64_ty;
        FunctionType* getline_ty = FunctionType::get(ssize_ty,
            {PointerType::get(m_ctx, 0), PointerType::get(m_ctx, 0),
             PointerType::get(m_ctx, 0)}, false);
        auto getline_fn = m_module.getOrInsertFunction("getline", getline_ty);

        // char* line_buf = NULL; size_t buf_size = 0;
        auto* line_buf = b.CreateAlloca(i8_ptr);
        b.CreateStore(ConstantPointerNull::get(i8_ptr), line_buf);
        auto* buf_size = b.CreateAlloca(i64_ty);
        b.CreateStore(ConstantInt::get(i64_ty, 0), buf_size);

        // Get stdin — on macOS, use __stdinp
        auto* stdin_var = m_module.getOrInsertGlobal("__stdinp",
            PointerType::get(m_ctx, 0));
        auto* stdin_ptr = b.CreateLoad(PointerType::get(m_ctx, 0), stdin_var, "stdin");

        // ssize_t line_size = getline(&line_buf, &buf_size, stdin)
        auto* line_size = b.CreateCall(getline_fn,
            {line_buf, buf_size, stdin_ptr}, "line_size");

        auto* is_eof = b.CreateICmpSLT(line_size, ConstantInt::get(i64_ty, 0));
        b.CreateCondBr(is_eof, eof_bb, ok_bb);

        // EOF: free buffer, return nil
        IRBuilder<> be(eof_bb);
        auto* buf_to_free = be.CreateLoad(i8_ptr, line_buf, "buf");
        auto* is_null = be.CreateICmpEQ(buf_to_free, ConstantPointerNull::get(i8_ptr));
        auto* skip_free = BasicBlock::Create(m_ctx, "skip_free", fn);
        auto* do_free = BasicBlock::Create(m_ctx, "do_free", fn);
        be.CreateCondBr(is_null, skip_free, do_free);

        IRBuilder<> bf(do_free);
        bf.CreateCall(m_module.getFunction("free"), {buf_to_free});
        bf.CreateBr(skip_free);

        IRBuilder<> bs(skip_free);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = bs.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = bs.CreateInsertValue(nil_val, ConstantInt::get(i64_ty, 0), {1});
        bs.CreateRet(nil_val);

        // OK: strip trailing newline, create string
        IRBuilder<> bo(ok_bb);
        auto* chars = bo.CreateLoad(i8_ptr, line_buf, "chars");
        // Check if last char is '\n'
        auto* last_idx = bo.CreateSub(line_size, ConstantInt::get(i64_ty, 1));
        auto* last_char_ptr = bo.CreateGEP(i8_ty, chars, {last_idx});
        auto* last_char = bo.CreateLoad(i8_ty, last_char_ptr, "last");
        auto* is_newline = bo.CreateICmpEQ(last_char, ConstantInt::get(i8_ty, '\n'));

        // If newline, null-terminate at that position
        auto* strip_bb = BasicBlock::Create(m_ctx, "strip", fn);
        auto* keep_bb = BasicBlock::Create(m_ctx, "keep", fn);
        bo.CreateCondBr(is_newline, strip_bb, keep_bb);

        IRBuilder<> bst(strip_bb);
        bst.CreateStore(ConstantInt::get(i8_ty, 0), last_char_ptr);
        bst.CreateBr(keep_bb);

        // Create string from the buffer (takes ownership via string_from_c which strdup's)
        IRBuilder<> bk(keep_bb);
        auto* result = bk.CreateCall(str_from_c, {chars});
        // Free the getline buffer (string_from_c makes its own copy)
        bk.CreateCall(m_module.getFunction("free"), {chars});
        bk.CreateRet(result);
    }

    // --- io_read_all() -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_io_read_all", fn_ty);
        m_fn_io_read_all = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
        auto* grow_bb = BasicBlock::Create(m_ctx, "grow", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);

        // size_t capacity = 4096, total_read = 0
        auto* cap_alloca = b.CreateAlloca(i64_ty);
        b.CreateStore(ConstantInt::get(i64_ty, 4096), cap_alloca);
        auto* total_alloca = b.CreateAlloca(i64_ty);
        b.CreateStore(ConstantInt::get(i64_ty, 0), total_alloca);

        // char* buffer = malloc(4096)
        auto* buf_alloca = b.CreateAlloca(i8_ptr);
        auto* init_buf = b.CreateCall(malloc_fn, {ConstantInt::get(i64_ty, 4096)});
        b.CreateStore(init_buf, buf_alloca);

        // Declare fread: size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
        FunctionType* fread_ty = FunctionType::get(i64_ty,
            {i8_ptr, i64_ty, i64_ty, PointerType::get(m_ctx, 0)}, false);
        auto fread_fn = m_module.getOrInsertFunction("fread", fread_ty);

        auto* stdin_var = m_module.getOrInsertGlobal("__stdinp",
            PointerType::get(m_ctx, 0));

        b.CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* cap = bl.CreateLoad(i64_ty, cap_alloca, "cap");
        auto* total = bl.CreateLoad(i64_ty, total_alloca, "total");
        auto* buf = bl.CreateLoad(i8_ptr, buf_alloca, "buf");

        // remaining = cap - total
        auto* remaining = bl.CreateSub(cap, total);
        // bytes_read = fread(buf + total, 1, remaining, stdin)
        auto* write_ptr = bl.CreateGEP(i8_ty, buf, {total});
        auto* stdin_ptr = bl.CreateLoad(PointerType::get(m_ctx, 0), stdin_var, "stdin");
        auto* bytes_read = bl.CreateCall(fread_fn,
            {write_ptr, ConstantInt::get(i64_ty, 1), remaining, stdin_ptr}, "bytes_read");

        // total += bytes_read
        auto* new_total = bl.CreateAdd(total, bytes_read);
        bl.CreateStore(new_total, total_alloca);

        // if bytes_read == 0, done
        auto* is_done = bl.CreateICmpEQ(bytes_read, ConstantInt::get(i64_ty, 0));
        // Also check if we need to grow
        auto* is_full = bl.CreateICmpEQ(new_total, cap);
        auto* need_action = bl.CreateOr(is_done, bl.CreateNot(is_full));
        // If done → done_bb, if full → grow_bb, else → loop_bb
        bl.CreateCondBr(is_done, done_bb, is_full ? grow_bb : loop_bb);

        // Grow: double capacity
        IRBuilder<> bg(grow_bb);
        auto* cur_cap = bg.CreateLoad(i64_ty, cap_alloca);
        auto* cur_buf = bg.CreateLoad(i8_ptr, buf_alloca);
        auto* new_cap = bg.CreateShl(cur_cap, 1); // double
        bg.CreateStore(new_cap, cap_alloca);
        auto* new_buf = bg.CreateCall(realloc_fn, {cur_buf, new_cap});
        bg.CreateStore(new_buf, buf_alloca);
        bg.CreateBr(loop_bb);

        // Done: null-terminate, create string
        IRBuilder<> bd(done_bb);
        auto* final_buf = bd.CreateLoad(i8_ptr, buf_alloca, "final_buf");
        auto* final_total = bd.CreateLoad(i64_ty, total_alloca, "final_total");
        // Null terminate
        auto* null_pos = bd.CreateGEP(i8_ty, final_buf, {final_total});
        bd.CreateStore(ConstantInt::get(i8_ty, 0), null_pos);
        // Create string (string_from_c will strdup, so we can free our buffer)
        auto* result = bd.CreateCall(str_from_c, {final_buf});
        bd.CreateCall(m_module.getFunction("free"), {final_buf});
        bd.CreateRet(result);
    }
}

} // namespace angara
