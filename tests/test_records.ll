; ModuleID = 'test_records'
source_filename = "angara_module"
target triple = "arm64-apple-darwin25.4.0"

%AngaraObject = type { i32, i64 }

@g_test_records_main = private global %AngaraObject zeroinitializer
@0 = private unnamed_addr constant [21 x i8] c"=== Record Tests ===\00", align 1
@1 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@2 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"age\00", align 1
@4 = private unnamed_addr constant [15 x i8] c"Created record\00", align 1
@5 = private unnamed_addr constant [7 x i8] c"Name: \00", align 1
@6 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@7 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@8 = private unnamed_addr constant [6 x i8] c"Age: \00", align 1
@9 = private unnamed_addr constant [4 x i8] c"age\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"age\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"Bob\00", align 1
@12 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"age\00", align 1
@14 = private unnamed_addr constant [10 x i8] c"p2 Name: \00", align 1
@15 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@16 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"val\00", align 1
@18 = private unnamed_addr constant [6 x i8] c"inner\00", align 1
@19 = private unnamed_addr constant [6 x i8] c"inner\00", align 1
@20 = private unnamed_addr constant [6 x i8] c"inner\00", align 1
@21 = private unnamed_addr constant [13 x i8] c"Nested val: \00", align 1
@22 = private unnamed_addr constant [4 x i8] c"val\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"val\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"age\00", align 1
@25 = private unnamed_addr constant [14 x i8] c"Updated age: \00", align 1
@26 = private unnamed_addr constant [4 x i8] c"age\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"age\00", align 1
@28 = private unnamed_addr constant [28 x i8] c"=== Record Tests Passed ===\00", align 1

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

define %AngaraObject @Angara_test_records_main() {
entry:
  %inner = alloca %AngaraObject, align 8
  %nested = alloca %AngaraObject, align 8
  %p2 = alloca %AngaraObject, align 8
  %p = alloca %AngaraObject, align 8
  %rt_angara_create_i64 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c = call %AngaraObject @angara_string_from_c(ptr @0)
  %rt_angara_create_nil = call %AngaraObject @angara_create_nil()
  %0 = alloca [2 x %AngaraObject], align 8
  %1 = getelementptr [2 x %AngaraObject], ptr %0, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i64, ptr %1, align 4
  %2 = getelementptr [2 x %AngaraObject], ptr %0, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c, ptr %2, align 4
  %rt_Angara_io_println = call %AngaraObject @Angara_io_println(i32 2, ptr %0)
  %rt_angara_record_new = call %AngaraObject @angara_record_new()
  %rt_angara_string_from_c1 = call %AngaraObject @angara_string_from_c(ptr @1)
  call void @angara_record_set(%AngaraObject %rt_angara_record_new, ptr @2, %AngaraObject %rt_angara_string_from_c1)
  %rt_angara_create_i642 = call %AngaraObject @angara_create_i64(i64 30)
  call void @angara_record_set(%AngaraObject %rt_angara_record_new, ptr @3, %AngaraObject %rt_angara_create_i642)
  store %AngaraObject %rt_angara_record_new, ptr %p, align 4
  call void @angara_incref(%AngaraObject %rt_angara_record_new)
  %rt_angara_create_i643 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c4 = call %AngaraObject @angara_string_from_c(ptr @4)
  %rt_angara_create_nil5 = call %AngaraObject @angara_create_nil()
  %3 = alloca [2 x %AngaraObject], align 8
  %4 = getelementptr [2 x %AngaraObject], ptr %3, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i643, ptr %4, align 4
  %5 = getelementptr [2 x %AngaraObject], ptr %3, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c4, ptr %5, align 4
  %rt_Angara_io_println6 = call %AngaraObject @Angara_io_println(i32 2, ptr %3)
  %rt_angara_create_i647 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c8 = call %AngaraObject @angara_string_from_c(ptr @5)
  %p_val = load %AngaraObject, ptr %p, align 4
  %rt_angara_string_from_c9 = call %AngaraObject @angara_string_from_c(ptr @6)
  %rt_angara_record_get = call %AngaraObject @angara_record_get(%AngaraObject %p_val, ptr @7)
  %rt_angara_string_concat = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c8, %AngaraObject %rt_angara_record_get)
  %rt_angara_create_nil10 = call %AngaraObject @angara_create_nil()
  %6 = alloca [2 x %AngaraObject], align 8
  %7 = getelementptr [2 x %AngaraObject], ptr %6, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i647, ptr %7, align 4
  %8 = getelementptr [2 x %AngaraObject], ptr %6, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat, ptr %8, align 4
  %rt_Angara_io_println11 = call %AngaraObject @Angara_io_println(i32 2, ptr %6)
  %rt_angara_create_i6412 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c13 = call %AngaraObject @angara_string_from_c(ptr @8)
  %p_val14 = load %AngaraObject, ptr %p, align 4
  %rt_angara_string_from_c15 = call %AngaraObject @angara_string_from_c(ptr @9)
  %rt_angara_record_get16 = call %AngaraObject @angara_record_get(%AngaraObject %p_val14, ptr @10)
  %rt_angara_to_string = call %AngaraObject @angara_to_string(%AngaraObject %rt_angara_record_get16)
  %rt_angara_string_concat17 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c13, %AngaraObject %rt_angara_to_string)
  %rt_angara_create_nil18 = call %AngaraObject @angara_create_nil()
  %9 = alloca [2 x %AngaraObject], align 8
  %10 = getelementptr [2 x %AngaraObject], ptr %9, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6412, ptr %10, align 4
  %11 = getelementptr [2 x %AngaraObject], ptr %9, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat17, ptr %11, align 4
  %rt_Angara_io_println19 = call %AngaraObject @Angara_io_println(i32 2, ptr %9)
  %rt_angara_record_new20 = call %AngaraObject @angara_record_new()
  %rt_angara_string_from_c21 = call %AngaraObject @angara_string_from_c(ptr @11)
  call void @angara_record_set(%AngaraObject %rt_angara_record_new20, ptr @12, %AngaraObject %rt_angara_string_from_c21)
  %rt_angara_create_i6422 = call %AngaraObject @angara_create_i64(i64 25)
  call void @angara_record_set(%AngaraObject %rt_angara_record_new20, ptr @13, %AngaraObject %rt_angara_create_i6422)
  store %AngaraObject %rt_angara_record_new20, ptr %p2, align 4
  call void @angara_incref(%AngaraObject %rt_angara_record_new20)
  %rt_angara_create_i6423 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c24 = call %AngaraObject @angara_string_from_c(ptr @14)
  %p2_val = load %AngaraObject, ptr %p2, align 4
  %rt_angara_string_from_c25 = call %AngaraObject @angara_string_from_c(ptr @15)
  %rt_angara_record_get26 = call %AngaraObject @angara_record_get(%AngaraObject %p2_val, ptr @16)
  %rt_angara_string_concat27 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c24, %AngaraObject %rt_angara_record_get26)
  %rt_angara_create_nil28 = call %AngaraObject @angara_create_nil()
  %12 = alloca [2 x %AngaraObject], align 8
  %13 = getelementptr [2 x %AngaraObject], ptr %12, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6423, ptr %13, align 4
  %14 = getelementptr [2 x %AngaraObject], ptr %12, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat27, ptr %14, align 4
  %rt_Angara_io_println29 = call %AngaraObject @Angara_io_println(i32 2, ptr %12)
  %rt_angara_record_new30 = call %AngaraObject @angara_record_new()
  %rt_angara_record_new31 = call %AngaraObject @angara_record_new()
  %rt_angara_create_i6432 = call %AngaraObject @angara_create_i64(i64 42)
  call void @angara_record_set(%AngaraObject %rt_angara_record_new31, ptr @17, %AngaraObject %rt_angara_create_i6432)
  call void @angara_record_set(%AngaraObject %rt_angara_record_new30, ptr @18, %AngaraObject %rt_angara_record_new31)
  store %AngaraObject %rt_angara_record_new30, ptr %nested, align 4
  call void @angara_incref(%AngaraObject %rt_angara_record_new30)
  %nested_val = load %AngaraObject, ptr %nested, align 4
  %rt_angara_string_from_c33 = call %AngaraObject @angara_string_from_c(ptr @19)
  %rt_angara_record_get34 = call %AngaraObject @angara_record_get(%AngaraObject %nested_val, ptr @20)
  store %AngaraObject %rt_angara_record_get34, ptr %inner, align 4
  call void @angara_incref(%AngaraObject %rt_angara_record_get34)
  %rt_angara_create_i6435 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c36 = call %AngaraObject @angara_string_from_c(ptr @21)
  %inner_val = load %AngaraObject, ptr %inner, align 4
  %rt_angara_string_from_c37 = call %AngaraObject @angara_string_from_c(ptr @22)
  %rt_angara_record_get38 = call %AngaraObject @angara_record_get(%AngaraObject %inner_val, ptr @23)
  %rt_angara_to_string39 = call %AngaraObject @angara_to_string(%AngaraObject %rt_angara_record_get38)
  %rt_angara_string_concat40 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c36, %AngaraObject %rt_angara_to_string39)
  %rt_angara_create_nil41 = call %AngaraObject @angara_create_nil()
  %15 = alloca [2 x %AngaraObject], align 8
  %16 = getelementptr [2 x %AngaraObject], ptr %15, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6435, ptr %16, align 4
  %17 = getelementptr [2 x %AngaraObject], ptr %15, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat40, ptr %17, align 4
  %rt_Angara_io_println42 = call %AngaraObject @Angara_io_println(i32 2, ptr %15)
  %rt_angara_create_i6443 = call %AngaraObject @angara_create_i64(i64 31)
  %p_val44 = load %AngaraObject, ptr %p, align 4
  call void @angara_record_set(%AngaraObject %p_val44, ptr @24, %AngaraObject %rt_angara_create_i6443)
  %rt_angara_create_i6445 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c46 = call %AngaraObject @angara_string_from_c(ptr @25)
  %p_val47 = load %AngaraObject, ptr %p, align 4
  %rt_angara_string_from_c48 = call %AngaraObject @angara_string_from_c(ptr @26)
  %rt_angara_record_get49 = call %AngaraObject @angara_record_get(%AngaraObject %p_val47, ptr @27)
  %rt_angara_to_string50 = call %AngaraObject @angara_to_string(%AngaraObject %rt_angara_record_get49)
  %rt_angara_string_concat51 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c46, %AngaraObject %rt_angara_to_string50)
  %rt_angara_create_nil52 = call %AngaraObject @angara_create_nil()
  %18 = alloca [2 x %AngaraObject], align 8
  %19 = getelementptr [2 x %AngaraObject], ptr %18, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6445, ptr %19, align 4
  %20 = getelementptr [2 x %AngaraObject], ptr %18, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat51, ptr %20, align 4
  %rt_Angara_io_println53 = call %AngaraObject @Angara_io_println(i32 2, ptr %18)
  %rt_angara_create_i6454 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c55 = call %AngaraObject @angara_string_from_c(ptr @28)
  %rt_angara_create_nil56 = call %AngaraObject @angara_create_nil()
  %21 = alloca [2 x %AngaraObject], align 8
  %22 = getelementptr [2 x %AngaraObject], ptr %21, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6454, ptr %22, align 4
  %23 = getelementptr [2 x %AngaraObject], ptr %21, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c55, ptr %23, align 4
  %rt_Angara_io_println57 = call %AngaraObject @Angara_io_println(i32 2, ptr %21)
  %rt_angara_create_i6458 = call %AngaraObject @angara_create_i64(i64 0)
  ret %AngaraObject %rt_angara_create_i6458
}

declare %AngaraObject @Angara_io_println(i32, ptr)

declare %AngaraObject @angara_record_new()

declare void @angara_record_set(%AngaraObject, ptr, %AngaraObject)

declare %AngaraObject @angara_record_get(%AngaraObject, ptr)

define private %AngaraObject @angara_w_Angara_test_records_main(i32 %0, ptr %1) {
entry:
  %result = call %AngaraObject @Angara_test_records_main()
  ret %AngaraObject %result
}

define i32 @main() {
entry:
  call void @angara_runtime_init()
  %rt_angara_closure_new = call %AngaraObject @angara_closure_new(ptr @angara_w_Angara_test_records_main, i32 0, i1 false)
  store %AngaraObject %rt_angara_closure_new, ptr @g_test_records_main, align 4
  %main_closure = load %AngaraObject, ptr @g_test_records_main, align 4
  %0 = alloca [0 x %AngaraObject], align 8
  %rt_angara_call = call %AngaraObject @angara_call(%AngaraObject %main_closure, i32 0, ptr %0)
  call void @angara_runtime_shutdown()
  ret i32 0
}

declare %AngaraObject @angara_closure_new(ptr, i32, i1)
