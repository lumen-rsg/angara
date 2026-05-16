// Angara LLVM Backend — Runtime: IO + Miscellaneous
#include "RuntimeBuilder.h"

#ifdef __linux__
#include <cstdio>  // for stdout/stderr/stdin macros
#endif

using namespace llvm;

namespace angara {
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

    // --- Platform-specific stream access ---
    // macOS: __stdoutp/__stderrp/__stdinp are FILE** — load to get FILE*
    // Linux: _IO_2_1_stdout_/_IO_2_1_stderr_/_IO_2_1_stdin_ are FILE structs — their address IS FILE*
#ifdef __APPLE__
    auto* stdout_g = m_module.getOrInsertGlobal("__stdoutp", PointerType::get(m_ctx, 0));
    auto* stderr_g = m_module.getOrInsertGlobal("__stderrp", PointerType::get(m_ctx, 0));
    auto* stdin_g = m_module.getOrInsertGlobal("__stdinp", PointerType::get(m_ctx, 0));
#else
    auto* stdout_g = m_module.getOrInsertGlobal("_IO_2_1_stdout_", i8_ty);
    auto* stderr_g = m_module.getOrInsertGlobal("_IO_2_1_stderr_", i8_ty);
    auto* stdin_g = m_module.getOrInsertGlobal("_IO_2_1_stdin_", i8_ty);
#endif

    // Resolve a stream global to FILE* (Value*)
    auto resolve_stream = [&](IRBuilder<>& b, Constant* gvar, const char* name) -> Value* {
#ifdef __APPLE__
        return b.CreateLoad(PointerType::get(m_ctx, 0), gvar, name);
#else
        (void)b; (void)name;
        return gvar;
#endif
    };

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

        auto* file_ptr = b.CreateSelect(is_stderr,
            resolve_stream(b, stderr_g, "stderr"),
            resolve_stream(b, stdout_g, "stdout"));

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

        auto* file_ptr = b.CreateSelect(is_stderr,
            resolve_stream(b, stderr_g, "stderr"),
            resolve_stream(b, stdout_g, "stdout"));

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

        // Select stdout or stderr based on stream_id
        auto* is_stderr = b.CreateICmpEQ(stream_id, ConstantInt::get(i64_ty, 2));

        auto* file_ptr = b.CreateSelect(is_stderr,
            resolve_stream(b, stderr_g, "stderr"),
            resolve_stream(b, stdout_g, "stdout"));

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

        auto* file_ptr = b.CreateSelect(is_stderr,
            resolve_stream(b, stderr_g, "stderr"),
            resolve_stream(b, stdout_g, "stdout"));

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

        // Get stdin
        auto* stdin_ptr = resolve_stream(b, stdin_g, "stdin");

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

        auto* stdin_ptr = resolve_stream(b, stdin_g, "stdin");

        b.CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* cap = bl.CreateLoad(i64_ty, cap_alloca, "cap");
        auto* total = bl.CreateLoad(i64_ty, total_alloca, "total");
        auto* buf = bl.CreateLoad(i8_ptr, buf_alloca, "buf");

        // remaining = cap - total
        auto* remaining = bl.CreateSub(cap, total);
        // bytes_read = fread(buf + total, 1, remaining, stdin)
        auto* write_ptr = bl.CreateGEP(i8_ty, buf, {total});
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
