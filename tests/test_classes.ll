; ModuleID = 'test_classes'
source_filename = "angara_module"
target triple = "arm64-apple-darwin25.4.0"

%AngaraObject = type { i32, i64 }

@0 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@1 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@2 = private unnamed_addr constant [8 x i8] c"Hello, \00", align 1
@3 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@4 = private unnamed_addr constant [2 x i8] c"!\00", align 1
@g_test_classes_main = private global %AngaraObject zeroinitializer
@5 = private unnamed_addr constant [20 x i8] c"=== Class Tests ===\00", align 1
@6 = private unnamed_addr constant [6 x i8] c"World\00", align 1
@7 = private unnamed_addr constant [7 x i8] c"Name: \00", align 1
@8 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@9 = private unnamed_addr constant [27 x i8] c"=== Class Tests Passed ===\00", align 1

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

define %AngaraObject @Angara_Greeter_new(%AngaraObject %0) {
entry:
  %name = alloca %AngaraObject, align 8
  %rt_angara_record_new = call %AngaraObject @angara_record_new()
  store %AngaraObject %0, ptr %name, align 4
  call void @angara_record_set(%AngaraObject %rt_angara_record_new, ptr @0, %AngaraObject %0)
  ret %AngaraObject %rt_angara_record_new
}

declare %AngaraObject @angara_record_new()

declare void @angara_record_set(%AngaraObject, ptr, %AngaraObject)

define %AngaraObject @Angara_Greeter_init(%AngaraObject %0, %AngaraObject %1) {
entry:
  %name = alloca %AngaraObject, align 8
  %this = alloca %AngaraObject, align 8
  store %AngaraObject %0, ptr %this, align 4
  store %AngaraObject %1, ptr %name, align 4
  %name_val = load %AngaraObject, ptr %name, align 4
  %this_val = load %AngaraObject, ptr %this, align 4
  call void @angara_record_set(%AngaraObject %this_val, ptr @1, %AngaraObject %name_val)
  %rt_angara_create_nil = call %AngaraObject @angara_create_nil()
  ret %AngaraObject %rt_angara_create_nil
}

define %AngaraObject @Angara_Greeter_greet(%AngaraObject %0) {
entry:
  %this = alloca %AngaraObject, align 8
  store %AngaraObject %0, ptr %this, align 4
  %rt_angara_string_from_c = call %AngaraObject @angara_string_from_c(ptr @2)
  %this_val = load %AngaraObject, ptr %this, align 4
  %rt_angara_record_get = call %AngaraObject @angara_record_get(%AngaraObject %this_val, ptr @3)
  %rt_angara_string_concat = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c, %AngaraObject %rt_angara_record_get)
  %rt_angara_string_from_c1 = call %AngaraObject @angara_string_from_c(ptr @4)
  %rt_angara_string_concat2 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_concat, %AngaraObject %rt_angara_string_from_c1)
  ret %AngaraObject %rt_angara_string_concat2
}

declare %AngaraObject @angara_record_get(%AngaraObject, ptr)

define %AngaraObject @Angara_test_classes_main() {
entry:
  %g = alloca %AngaraObject, align 8
  %rt_angara_create_i64 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c = call %AngaraObject @angara_string_from_c(ptr @5)
  %rt_angara_create_nil = call %AngaraObject @angara_create_nil()
  %0 = alloca [2 x %AngaraObject], align 8
  %1 = getelementptr [2 x %AngaraObject], ptr %0, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i64, ptr %1, align 4
  %2 = getelementptr [2 x %AngaraObject], ptr %0, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c, ptr %2, align 4
  %rt_Angara_io_println = call %AngaraObject @Angara_io_println(i32 2, ptr %0)
  %rt_angara_string_from_c1 = call %AngaraObject @angara_string_from_c(ptr @6)
  %rt_Angara_Greeter_new = call %AngaraObject @Angara_Greeter_new(%AngaraObject %rt_angara_string_from_c1)
  store %AngaraObject %rt_Angara_Greeter_new, ptr %g, align 4
  call void @angara_incref(%AngaraObject %rt_Angara_Greeter_new)
  %rt_angara_create_i642 = call %AngaraObject @angara_create_i64(i64 1)
  %g_val = load %AngaraObject, ptr %g, align 4
  %rt_Angara_Greeter_greet = call %AngaraObject @Angara_Greeter_greet(%AngaraObject %g_val)
  %rt_angara_create_nil3 = call %AngaraObject @angara_create_nil()
  %3 = alloca [2 x %AngaraObject], align 8
  %4 = getelementptr [2 x %AngaraObject], ptr %3, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i642, ptr %4, align 4
  %5 = getelementptr [2 x %AngaraObject], ptr %3, i64 0, i64 1
  store %AngaraObject %rt_Angara_Greeter_greet, ptr %5, align 4
  %rt_Angara_io_println4 = call %AngaraObject @Angara_io_println(i32 2, ptr %3)
  %rt_angara_create_i645 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c6 = call %AngaraObject @angara_string_from_c(ptr @7)
  %g_val7 = load %AngaraObject, ptr %g, align 4
  %rt_angara_record_get = call %AngaraObject @angara_record_get(%AngaraObject %g_val7, ptr @8)
  %rt_angara_string_concat = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c6, %AngaraObject %rt_angara_record_get)
  %rt_angara_create_nil8 = call %AngaraObject @angara_create_nil()
  %6 = alloca [2 x %AngaraObject], align 8
  %7 = getelementptr [2 x %AngaraObject], ptr %6, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i645, ptr %7, align 4
  %8 = getelementptr [2 x %AngaraObject], ptr %6, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat, ptr %8, align 4
  %rt_Angara_io_println9 = call %AngaraObject @Angara_io_println(i32 2, ptr %6)
  %rt_angara_create_i6410 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c11 = call %AngaraObject @angara_string_from_c(ptr @9)
  %rt_angara_create_nil12 = call %AngaraObject @angara_create_nil()
  %9 = alloca [2 x %AngaraObject], align 8
  %10 = getelementptr [2 x %AngaraObject], ptr %9, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6410, ptr %10, align 4
  %11 = getelementptr [2 x %AngaraObject], ptr %9, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c11, ptr %11, align 4
  %rt_Angara_io_println13 = call %AngaraObject @Angara_io_println(i32 2, ptr %9)
  %rt_angara_create_i6414 = call %AngaraObject @angara_create_i64(i64 0)
  ret %AngaraObject %rt_angara_create_i6414
}

declare %AngaraObject @Angara_io_println(i32, ptr)

define private %AngaraObject @angara_w_Angara_test_classes_main(i32 %0, ptr %1) {
entry:
  %result = call %AngaraObject @Angara_test_classes_main()
  ret %AngaraObject %result
}

define i32 @main() {
entry:
  call void @angara_runtime_init()
  %rt_angara_closure_new = call %AngaraObject @angara_closure_new(ptr @angara_w_Angara_test_classes_main, i32 0, i1 false)
  store %AngaraObject %rt_angara_closure_new, ptr @g_test_classes_main, align 4
  %main_closure = load %AngaraObject, ptr @g_test_classes_main, align 4
  %0 = alloca [0 x %AngaraObject], align 8
  %rt_angara_call = call %AngaraObject @angara_call(%AngaraObject %main_closure, i32 0, ptr %0)
  call void @angara_runtime_shutdown()
  ret i32 0
}

declare %AngaraObject @angara_closure_new(ptr, i32, i1)
