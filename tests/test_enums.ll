; ModuleID = 'test_enums'
source_filename = "angara_module"
target triple = "arm64-apple-darwin25.4.0"

%AngaraObject = type { i32, i64 }

@Angara_enum_Color_Red = private constant %AngaraObject { i32 2, i64 0 }
@Angara_enum_Color_Green = private constant %AngaraObject { i32 2, i64 1 }
@Angara_enum_Color_Blue = private constant %AngaraObject { i32 2, i64 2 }
@g_test_enums_main = private global %AngaraObject zeroinitializer
@0 = private unnamed_addr constant [19 x i8] c"=== Enum Tests ===\00", align 1
@1 = private unnamed_addr constant [15 x i8] c"Got enum value\00", align 1
@2 = private unnamed_addr constant [11 x i8] c"forty-two!\00", align 1
@3 = private unnamed_addr constant [26 x i8] c"=== Enum Tests Passed ===\00", align 1

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

define %AngaraObject @Angara_test_enums_main() {
entry:
  %val = alloca %AngaraObject, align 8
  %c = alloca %AngaraObject, align 8
  %rt_angara_create_i64 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c = call %AngaraObject @angara_string_from_c(ptr @0)
  %rt_angara_create_nil = call %AngaraObject @angara_create_nil()
  %0 = alloca [2 x %AngaraObject], align 8
  %1 = getelementptr [2 x %AngaraObject], ptr %0, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i64, ptr %1, align 4
  %2 = getelementptr [2 x %AngaraObject], ptr %0, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c, ptr %2, align 4
  %rt_Angara_io_println = call %AngaraObject @Angara_io_println(i32 2, ptr %0)
  %rt_angara_create_nil1 = call %AngaraObject @angara_create_nil()
  store %AngaraObject %rt_angara_create_nil1, ptr %c, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_nil1)
  %rt_angara_create_i642 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c3 = call %AngaraObject @angara_string_from_c(ptr @1)
  %rt_angara_create_nil4 = call %AngaraObject @angara_create_nil()
  %3 = alloca [2 x %AngaraObject], align 8
  %4 = getelementptr [2 x %AngaraObject], ptr %3, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i642, ptr %4, align 4
  %5 = getelementptr [2 x %AngaraObject], ptr %3, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c3, ptr %5, align 4
  %rt_Angara_io_println5 = call %AngaraObject @Angara_io_println(i32 2, ptr %3)
  %rt_angara_create_i646 = call %AngaraObject @angara_create_i64(i64 42)
  store %AngaraObject %rt_angara_create_i646, ptr %val, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i646)
  %val_val = load %AngaraObject, ptr %val, align 4
  %rt_angara_create_i647 = call %AngaraObject @angara_create_i64(i64 42)
  %rt_angara_equals = call %AngaraObject @angara_equals(%AngaraObject %val_val, %AngaraObject %rt_angara_create_i647)
  %is_truthy = call i1 @angara_is_truthy(%AngaraObject %rt_angara_equals)
  br i1 %is_truthy, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %rt_angara_create_i648 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c9 = call %AngaraObject @angara_string_from_c(ptr @2)
  %rt_angara_create_nil10 = call %AngaraObject @angara_create_nil()
  %6 = alloca [2 x %AngaraObject], align 8
  %7 = getelementptr [2 x %AngaraObject], ptr %6, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i648, ptr %7, align 4
  %8 = getelementptr [2 x %AngaraObject], ptr %6, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c9, ptr %8, align 4
  %rt_Angara_io_println11 = call %AngaraObject @Angara_io_println(i32 2, ptr %6)
  br label %if.merge

if.merge:                                         ; preds = %if.then, %entry
  %rt_angara_create_i6412 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c13 = call %AngaraObject @angara_string_from_c(ptr @3)
  %rt_angara_create_nil14 = call %AngaraObject @angara_create_nil()
  %9 = alloca [2 x %AngaraObject], align 8
  %10 = getelementptr [2 x %AngaraObject], ptr %9, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6412, ptr %10, align 4
  %11 = getelementptr [2 x %AngaraObject], ptr %9, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c13, ptr %11, align 4
  %rt_Angara_io_println15 = call %AngaraObject @Angara_io_println(i32 2, ptr %9)
  %rt_angara_create_i6416 = call %AngaraObject @angara_create_i64(i64 0)
  ret %AngaraObject %rt_angara_create_i6416
}

declare %AngaraObject @Angara_io_println(i32, ptr)

define private %AngaraObject @angara_w_Angara_test_enums_main(i32 %0, ptr %1) {
entry:
  %result = call %AngaraObject @Angara_test_enums_main()
  ret %AngaraObject %result
}

define i32 @main() {
entry:
  call void @angara_runtime_init()
  %rt_angara_closure_new = call %AngaraObject @angara_closure_new(ptr @angara_w_Angara_test_enums_main, i32 0, i1 false)
  store %AngaraObject %rt_angara_closure_new, ptr @g_test_enums_main, align 4
  %main_closure = load %AngaraObject, ptr @g_test_enums_main, align 4
  %0 = alloca [0 x %AngaraObject], align 8
  %rt_angara_call = call %AngaraObject @angara_call(%AngaraObject %main_closure, i32 0, ptr %0)
  call void @angara_runtime_shutdown()
  ret i32 0
}

declare %AngaraObject @angara_closure_new(ptr, i32, i1)
