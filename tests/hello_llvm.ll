; ModuleID = 'hello_llvm'
source_filename = "angara_module"
target triple = "arm64-apple-darwin25.4.0"

%AngaraObject = type { i32, i64 }

@g_hello_llvm_add = private global %AngaraObject zeroinitializer
@g_hello_llvm_factorial = private global %AngaraObject zeroinitializer
@0 = private unnamed_addr constant [32 x i8] c"Hello from Angara LLVM backend!\00", align 1
@1 = private unnamed_addr constant [10 x i8] c"42 + 8 = \00", align 1
@2 = private unnamed_addr constant [17 x i8] c"factorial(10) = \00", align 1

declare %AngaraObject @angara_create_nil()

declare %AngaraObject @angara_create_bool(i1)

declare %AngaraObject @angara_create_i64(i64)

declare %AngaraObject @angara_create_f64(double)

declare %AngaraObject @angara_string_from_c(ptr)

declare %AngaraObject @angara_equals(%AngaraObject, %AngaraObject)

declare i1 @angara_is_truthy(%AngaraObject)

declare %AngaraObject @angara_string_concat(%AngaraObject, %AngaraObject)

declare %AngaraObject @angara_len(%AngaraObject)

declare %AngaraObject @angara_deep_clone(%AngaraObject)

declare %AngaraObject @angara_to_string(%AngaraObject)

declare %AngaraObject @angara_to_i64(%AngaraObject)

declare %AngaraObject @angara_to_f64(%AngaraObject)

declare %AngaraObject @angara_to_bool(%AngaraObject)

declare %AngaraObject @angara_typeof(%AngaraObject)

declare %AngaraObject @angara_call(%AngaraObject, i32, ptr)

declare void @angara_runtime_init()

declare void @angara_runtime_shutdown()

declare void @angara_throw_error(ptr)

declare void @angara_incref(%AngaraObject)

declare void @angara_decref(%AngaraObject)

declare void @angara_throw(%AngaraObject)

define %AngaraObject @Angara_hello_llvm_add(%AngaraObject %a, %AngaraObject %b) {
entry:
  %b2 = alloca %AngaraObject, align 8
  %a1 = alloca %AngaraObject, align 8
  store %AngaraObject %a, ptr %a1, align 4
  store %AngaraObject %b, ptr %b2, align 4
  %a_val = load %AngaraObject, ptr %a1, align 4
  %b_val = load %AngaraObject, ptr %b2, align 4
  %i64_val = extractvalue %AngaraObject %a_val, 1
  %i64_val3 = extractvalue %AngaraObject %b_val, 1
  %iadd = add i64 %i64_val, %i64_val3
  %rt_angara_create_i64 = call %AngaraObject @angara_create_i64(i64 %iadd)
  ret %AngaraObject %rt_angara_create_i64
}

define private %AngaraObject @angara_w_Angara_hello_llvm_add(i32 %0, ptr %1) {
entry:
  %arg0 = getelementptr %AngaraObject, ptr %1, i32 0
  %a0 = load %AngaraObject, ptr %arg0, align 4
  %arg1 = getelementptr %AngaraObject, ptr %1, i32 1
  %a1 = load %AngaraObject, ptr %arg1, align 4
  %result = call %AngaraObject @Angara_hello_llvm_add(%AngaraObject %a0, %AngaraObject %a1)
  ret %AngaraObject %result
}

define %AngaraObject @Angara_hello_llvm_factorial(%AngaraObject %n) {
entry:
  %n1 = alloca %AngaraObject, align 8
  store %AngaraObject %n, ptr %n1, align 4
  %n_val = load %AngaraObject, ptr %n1, align 4
  %rt_angara_create_i64 = call %AngaraObject @angara_create_i64(i64 1)
  %i64_val = extractvalue %AngaraObject %n_val, 1
  %f64_val = bitcast i64 %i64_val to double
  %i64_val2 = extractvalue %AngaraObject %rt_angara_create_i64, 1
  %f64_val3 = bitcast i64 %i64_val2 to double
  %0 = fcmp ole double %f64_val, %f64_val3
  %rt_angara_create_bool = call %AngaraObject @angara_create_bool(i1 %0)
  %is_truthy = call i1 @angara_is_truthy(%AngaraObject %rt_angara_create_bool)
  br i1 %is_truthy, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %rt_angara_create_i644 = call %AngaraObject @angara_create_i64(i64 1)
  ret %AngaraObject %rt_angara_create_i644

if.merge:                                         ; preds = %entry
  %n_val5 = load %AngaraObject, ptr %n1, align 4
  %n_val6 = load %AngaraObject, ptr %n1, align 4
  %rt_angara_create_i647 = call %AngaraObject @angara_create_i64(i64 1)
  %i64_val8 = extractvalue %AngaraObject %n_val6, 1
  %i64_val9 = extractvalue %AngaraObject %rt_angara_create_i647, 1
  %isub = sub i64 %i64_val8, %i64_val9
  %rt_angara_create_i6410 = call %AngaraObject @angara_create_i64(i64 %isub)
  %g_factorial_val = load %AngaraObject, ptr @g_hello_llvm_factorial, align 4
  %1 = alloca [1 x %AngaraObject], align 8
  %2 = getelementptr [1 x %AngaraObject], ptr %1, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6410, ptr %2, align 4
  %rt_angara_call = call %AngaraObject @angara_call(%AngaraObject %g_factorial_val, i32 1, ptr %1)
  %i64_val11 = extractvalue %AngaraObject %n_val5, 1
  %i64_val12 = extractvalue %AngaraObject %rt_angara_call, 1
  %imul = mul i64 %i64_val11, %i64_val12
  %rt_angara_create_i6413 = call %AngaraObject @angara_create_i64(i64 %imul)
  ret %AngaraObject %rt_angara_create_i6413
}

define private %AngaraObject @angara_w_Angara_hello_llvm_factorial(i32 %0, ptr %1) {
entry:
  %arg0 = getelementptr %AngaraObject, ptr %1, i32 0
  %a0 = load %AngaraObject, ptr %arg0, align 4
  %result = call %AngaraObject @Angara_hello_llvm_factorial(%AngaraObject %a0)
  ret %AngaraObject %result
}

define i32 @main() {
entry:
  call void @angara_runtime_init()
  %rt_angara_closure_new = call %AngaraObject @angara_closure_new(ptr @angara_w_Angara_hello_llvm_add, i32 2, i1 false)
  store %AngaraObject %rt_angara_closure_new, ptr @g_hello_llvm_add, align 4
  %rt_angara_closure_new1 = call %AngaraObject @angara_closure_new(ptr @angara_w_Angara_hello_llvm_factorial, i32 1, i1 false)
  store %AngaraObject %rt_angara_closure_new1, ptr @g_hello_llvm_factorial, align 4
  %rt_angara_create_i64 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c = call %AngaraObject @angara_string_from_c(ptr @0)
  %rt_angara_create_nil = call %AngaraObject @angara_create_nil()
  %0 = alloca [2 x %AngaraObject], align 8
  %1 = getelementptr [2 x %AngaraObject], ptr %0, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i64, ptr %1, align 4
  %2 = getelementptr [2 x %AngaraObject], ptr %0, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c, ptr %2, align 4
  %rt_Angara_io_println = call %AngaraObject @Angara_io_println(i32 2, ptr %0)
  %rt_angara_create_i642 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c3 = call %AngaraObject @angara_string_from_c(ptr @1)
  %rt_angara_create_i644 = call %AngaraObject @angara_create_i64(i64 42)
  %rt_angara_create_i645 = call %AngaraObject @angara_create_i64(i64 8)
  %g_add_val = load %AngaraObject, ptr @g_hello_llvm_add, align 4
  %3 = alloca [2 x %AngaraObject], align 8
  %4 = getelementptr [2 x %AngaraObject], ptr %3, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i644, ptr %4, align 4
  %5 = getelementptr [2 x %AngaraObject], ptr %3, i64 0, i64 1
  store %AngaraObject %rt_angara_create_i645, ptr %5, align 4
  %rt_angara_call = call %AngaraObject @angara_call(%AngaraObject %g_add_val, i32 2, ptr %3)
  %rt_angara_to_string = call %AngaraObject @angara_to_string(%AngaraObject %rt_angara_call)
  %rt_angara_string_concat = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c3, %AngaraObject %rt_angara_to_string)
  %rt_angara_create_nil6 = call %AngaraObject @angara_create_nil()
  %6 = alloca [2 x %AngaraObject], align 8
  %7 = getelementptr [2 x %AngaraObject], ptr %6, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i642, ptr %7, align 4
  %8 = getelementptr [2 x %AngaraObject], ptr %6, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat, ptr %8, align 4
  %rt_Angara_io_println7 = call %AngaraObject @Angara_io_println(i32 2, ptr %6)
  %rt_angara_create_i648 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c9 = call %AngaraObject @angara_string_from_c(ptr @2)
  %rt_angara_create_i6410 = call %AngaraObject @angara_create_i64(i64 10)
  %g_factorial_val = load %AngaraObject, ptr @g_hello_llvm_factorial, align 4
  %9 = alloca [1 x %AngaraObject], align 8
  %10 = getelementptr [1 x %AngaraObject], ptr %9, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6410, ptr %10, align 4
  %rt_angara_call11 = call %AngaraObject @angara_call(%AngaraObject %g_factorial_val, i32 1, ptr %9)
  %rt_angara_to_string12 = call %AngaraObject @angara_to_string(%AngaraObject %rt_angara_call11)
  %rt_angara_string_concat13 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c9, %AngaraObject %rt_angara_to_string12)
  %rt_angara_create_nil14 = call %AngaraObject @angara_create_nil()
  %11 = alloca [2 x %AngaraObject], align 8
  %12 = getelementptr [2 x %AngaraObject], ptr %11, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i648, ptr %12, align 4
  %13 = getelementptr [2 x %AngaraObject], ptr %11, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat13, ptr %13, align 4
  %rt_Angara_io_println15 = call %AngaraObject @Angara_io_println(i32 2, ptr %11)
  call void @angara_runtime_shutdown()
  ret i32 0
}

declare %AngaraObject @angara_closure_new(ptr, i32, i1)

declare %AngaraObject @Angara_io_println(i32, ptr)
