; ModuleID = 'test_basics'
source_filename = "angara_module"
target triple = "arm64-apple-darwin25.4.0"

%AngaraObject = type { i32, i64 }

@g_test_basics_fibonacci = private global %AngaraObject zeroinitializer
@g_test_basics_main = private global %AngaraObject zeroinitializer
@0 = private unnamed_addr constant [20 x i8] c"=== Basic Tests ===\00", align 1
@1 = private unnamed_addr constant [13 x i8] c"x > 40: true\00", align 1
@2 = private unnamed_addr constant [14 x i8] c"x > 40: false\00", align 1
@3 = private unnamed_addr constant [12 x i8] c"Sum 0..4 = \00", align 1
@4 = private unnamed_addr constant [6 x i8] c"5! = \00", align 1
@5 = private unnamed_addr constant [15 x i8] c"List length = \00", align 1
@6 = private unnamed_addr constant [12 x i8] c"items[0] = \00", align 1
@7 = private unnamed_addr constant [12 x i8] c"items[2] = \00", align 1
@8 = private unnamed_addr constant [16 x i8] c"Sum of items = \00", align 1
@9 = private unnamed_addr constant [11 x i8] c"fib(10) = \00", align 1
@10 = private unnamed_addr constant [6 x i8] c"Hello\00", align 1
@11 = private unnamed_addr constant [2 x i8] c" \00", align 1
@12 = private unnamed_addr constant [6 x i8] c"LLVM!\00", align 1
@13 = private unnamed_addr constant [25 x i8] c"=== All Tests Passed ===\00", align 1

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

define %AngaraObject @Angara_test_basics_fibonacci(%AngaraObject %n) {
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
  %n_val4 = load %AngaraObject, ptr %n1, align 4
  ret %AngaraObject %n_val4

if.merge:                                         ; preds = %entry
  %n_val5 = load %AngaraObject, ptr %n1, align 4
  %rt_angara_create_i646 = call %AngaraObject @angara_create_i64(i64 1)
  %i64_val7 = extractvalue %AngaraObject %n_val5, 1
  %i64_val8 = extractvalue %AngaraObject %rt_angara_create_i646, 1
  %isub = sub i64 %i64_val7, %i64_val8
  %rt_angara_create_i649 = call %AngaraObject @angara_create_i64(i64 %isub)
  %g_fibonacci_val = load %AngaraObject, ptr @g_test_basics_fibonacci, align 4
  %1 = alloca [1 x %AngaraObject], align 8
  %2 = getelementptr [1 x %AngaraObject], ptr %1, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i649, ptr %2, align 4
  %rt_angara_call = call %AngaraObject @angara_call(%AngaraObject %g_fibonacci_val, i32 1, ptr %1)
  %n_val10 = load %AngaraObject, ptr %n1, align 4
  %rt_angara_create_i6411 = call %AngaraObject @angara_create_i64(i64 2)
  %i64_val12 = extractvalue %AngaraObject %n_val10, 1
  %i64_val13 = extractvalue %AngaraObject %rt_angara_create_i6411, 1
  %isub14 = sub i64 %i64_val12, %i64_val13
  %rt_angara_create_i6415 = call %AngaraObject @angara_create_i64(i64 %isub14)
  %g_fibonacci_val16 = load %AngaraObject, ptr @g_test_basics_fibonacci, align 4
  %3 = alloca [1 x %AngaraObject], align 8
  %4 = getelementptr [1 x %AngaraObject], ptr %3, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6415, ptr %4, align 4
  %rt_angara_call17 = call %AngaraObject @angara_call(%AngaraObject %g_fibonacci_val16, i32 1, ptr %3)
  %i64_val18 = extractvalue %AngaraObject %rt_angara_call, 1
  %i64_val19 = extractvalue %AngaraObject %rt_angara_call17, 1
  %iadd = add i64 %i64_val18, %i64_val19
  %rt_angara_create_i6420 = call %AngaraObject @angara_create_i64(i64 %iadd)
  ret %AngaraObject %rt_angara_create_i6420
}

define private %AngaraObject @angara_w_Angara_test_basics_fibonacci(i32 %0, ptr %1) {
entry:
  %arg0 = getelementptr %AngaraObject, ptr %1, i32 0
  %a0 = load %AngaraObject, ptr %arg0, align 4
  %result = call %AngaraObject @Angara_test_basics_fibonacci(%AngaraObject %a0)
  ret %AngaraObject %result
}

define %AngaraObject @Angara_test_basics_main() {
entry:
  %greeting = alloca %AngaraObject, align 8
  %fib10 = alloca %AngaraObject, align 8
  %item = alloca %AngaraObject, align 8
  %__for_idx_item = alloca %AngaraObject, align 8
  %total = alloca %AngaraObject, align 8
  %items = alloca %AngaraObject, align 8
  %j = alloca %AngaraObject, align 8
  %product = alloca %AngaraObject, align 8
  %i = alloca %AngaraObject, align 8
  %sum = alloca %AngaraObject, align 8
  %x = alloca %AngaraObject, align 8
  %rt_angara_create_i64 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c = call %AngaraObject @angara_string_from_c(ptr @0)
  %rt_angara_create_nil = call %AngaraObject @angara_create_nil()
  %0 = alloca [2 x %AngaraObject], align 8
  %1 = getelementptr [2 x %AngaraObject], ptr %0, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i64, ptr %1, align 4
  %2 = getelementptr [2 x %AngaraObject], ptr %0, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c, ptr %2, align 4
  %rt_Angara_io_println = call %AngaraObject @Angara_io_println(i32 2, ptr %0)
  %rt_angara_create_i641 = call %AngaraObject @angara_create_i64(i64 42)
  store %AngaraObject %rt_angara_create_i641, ptr %x, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i641)
  %x_val = load %AngaraObject, ptr %x, align 4
  %rt_angara_create_i642 = call %AngaraObject @angara_create_i64(i64 40)
  %i64_val = extractvalue %AngaraObject %x_val, 1
  %f64_val = bitcast i64 %i64_val to double
  %i64_val3 = extractvalue %AngaraObject %rt_angara_create_i642, 1
  %f64_val4 = bitcast i64 %i64_val3 to double
  %3 = fcmp ogt double %f64_val, %f64_val4
  %rt_angara_create_bool = call %AngaraObject @angara_create_bool(i1 %3)
  %is_truthy = call i1 @angara_is_truthy(%AngaraObject %rt_angara_create_bool)
  br i1 %is_truthy, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %rt_angara_create_i645 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c6 = call %AngaraObject @angara_string_from_c(ptr @1)
  %rt_angara_create_nil7 = call %AngaraObject @angara_create_nil()
  %4 = alloca [2 x %AngaraObject], align 8
  %5 = getelementptr [2 x %AngaraObject], ptr %4, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i645, ptr %5, align 4
  %6 = getelementptr [2 x %AngaraObject], ptr %4, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c6, ptr %6, align 4
  %rt_Angara_io_println8 = call %AngaraObject @Angara_io_println(i32 2, ptr %4)
  br label %if.merge

if.else:                                          ; preds = %entry
  %rt_angara_create_i649 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c10 = call %AngaraObject @angara_string_from_c(ptr @2)
  %rt_angara_create_nil11 = call %AngaraObject @angara_create_nil()
  %7 = alloca [2 x %AngaraObject], align 8
  %8 = getelementptr [2 x %AngaraObject], ptr %7, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i649, ptr %8, align 4
  %9 = getelementptr [2 x %AngaraObject], ptr %7, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c10, ptr %9, align 4
  %rt_Angara_io_println12 = call %AngaraObject @Angara_io_println(i32 2, ptr %7)
  br label %if.merge

if.merge:                                         ; preds = %if.else, %if.then
  %rt_angara_create_i6413 = call %AngaraObject @angara_create_i64(i64 0)
  store %AngaraObject %rt_angara_create_i6413, ptr %sum, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i6413)
  %rt_angara_create_i6414 = call %AngaraObject @angara_create_i64(i64 0)
  store %AngaraObject %rt_angara_create_i6414, ptr %i, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i6414)
  br label %while.cond

while.cond:                                       ; preds = %while.body, %if.merge
  %i_val = load %AngaraObject, ptr %i, align 4
  %rt_angara_create_i6415 = call %AngaraObject @angara_create_i64(i64 5)
  %i64_val16 = extractvalue %AngaraObject %i_val, 1
  %f64_val17 = bitcast i64 %i64_val16 to double
  %i64_val18 = extractvalue %AngaraObject %rt_angara_create_i6415, 1
  %f64_val19 = bitcast i64 %i64_val18 to double
  %10 = fcmp olt double %f64_val17, %f64_val19
  %rt_angara_create_bool20 = call %AngaraObject @angara_create_bool(i1 %10)
  %is_truthy21 = call i1 @angara_is_truthy(%AngaraObject %rt_angara_create_bool20)
  br i1 %is_truthy21, label %while.body, label %while.exit

while.body:                                       ; preds = %while.cond
  %sum_val = load %AngaraObject, ptr %sum, align 4
  %i_val22 = load %AngaraObject, ptr %i, align 4
  %i64_val23 = extractvalue %AngaraObject %sum_val, 1
  %i64_val24 = extractvalue %AngaraObject %i_val22, 1
  %iadd = add i64 %i64_val23, %i64_val24
  %rt_angara_create_i6425 = call %AngaraObject @angara_create_i64(i64 %iadd)
  %sum_val26 = load %AngaraObject, ptr %sum, align 4
  call void @angara_decref(%AngaraObject %sum_val26)
  store %AngaraObject %rt_angara_create_i6425, ptr %sum, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i6425)
  %i_val27 = load %AngaraObject, ptr %i, align 4
  %rt_angara_create_i6428 = call %AngaraObject @angara_create_i64(i64 1)
  %i64_val29 = extractvalue %AngaraObject %i_val27, 1
  %i64_val30 = extractvalue %AngaraObject %rt_angara_create_i6428, 1
  %iadd31 = add i64 %i64_val29, %i64_val30
  %rt_angara_create_i6432 = call %AngaraObject @angara_create_i64(i64 %iadd31)
  %i_val33 = load %AngaraObject, ptr %i, align 4
  call void @angara_decref(%AngaraObject %i_val33)
  store %AngaraObject %rt_angara_create_i6432, ptr %i, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i6432)
  br label %while.cond

while.exit:                                       ; preds = %while.cond
  %rt_angara_create_i6434 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c35 = call %AngaraObject @angara_string_from_c(ptr @3)
  %sum_val36 = load %AngaraObject, ptr %sum, align 4
  %rt_angara_to_string = call %AngaraObject @angara_to_string(%AngaraObject %sum_val36)
  %rt_angara_string_concat = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c35, %AngaraObject %rt_angara_to_string)
  %rt_angara_create_nil37 = call %AngaraObject @angara_create_nil()
  %11 = alloca [2 x %AngaraObject], align 8
  %12 = getelementptr [2 x %AngaraObject], ptr %11, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6434, ptr %12, align 4
  %13 = getelementptr [2 x %AngaraObject], ptr %11, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat, ptr %13, align 4
  %rt_Angara_io_println38 = call %AngaraObject @Angara_io_println(i32 2, ptr %11)
  %rt_angara_create_i6439 = call %AngaraObject @angara_create_i64(i64 1)
  store %AngaraObject %rt_angara_create_i6439, ptr %product, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i6439)
  %rt_angara_create_i6440 = call %AngaraObject @angara_create_i64(i64 1)
  store %AngaraObject %rt_angara_create_i6440, ptr %j, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i6440)
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %while.exit
  %j_val = load %AngaraObject, ptr %j, align 4
  %rt_angara_create_i6441 = call %AngaraObject @angara_create_i64(i64 6)
  %i64_val42 = extractvalue %AngaraObject %j_val, 1
  %f64_val43 = bitcast i64 %i64_val42 to double
  %i64_val44 = extractvalue %AngaraObject %rt_angara_create_i6441, 1
  %f64_val45 = bitcast i64 %i64_val44 to double
  %14 = fcmp olt double %f64_val43, %f64_val45
  %rt_angara_create_bool46 = call %AngaraObject @angara_create_bool(i1 %14)
  %is_truthy47 = call i1 @angara_is_truthy(%AngaraObject %rt_angara_create_bool46)
  br i1 %is_truthy47, label %for.body, label %for.exit

for.body:                                         ; preds = %for.cond
  %product_val = load %AngaraObject, ptr %product, align 4
  %j_val48 = load %AngaraObject, ptr %j, align 4
  %i64_val49 = extractvalue %AngaraObject %product_val, 1
  %i64_val50 = extractvalue %AngaraObject %j_val48, 1
  %imul = mul i64 %i64_val49, %i64_val50
  %rt_angara_create_i6451 = call %AngaraObject @angara_create_i64(i64 %imul)
  %product_val52 = load %AngaraObject, ptr %product, align 4
  call void @angara_decref(%AngaraObject %product_val52)
  store %AngaraObject %rt_angara_create_i6451, ptr %product, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i6451)
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %j_val53 = load %AngaraObject, ptr %j, align 4
  %rt_angara_create_i6454 = call %AngaraObject @angara_create_i64(i64 1)
  %i64_val55 = extractvalue %AngaraObject %j_val53, 1
  %i64_val56 = extractvalue %AngaraObject %rt_angara_create_i6454, 1
  %iadd57 = add i64 %i64_val55, %i64_val56
  %rt_angara_create_i6458 = call %AngaraObject @angara_create_i64(i64 %iadd57)
  %j_val59 = load %AngaraObject, ptr %j, align 4
  call void @angara_decref(%AngaraObject %j_val59)
  store %AngaraObject %rt_angara_create_i6458, ptr %j, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i6458)
  br label %for.cond

for.exit:                                         ; preds = %for.cond
  %rt_angara_create_i6460 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c61 = call %AngaraObject @angara_string_from_c(ptr @4)
  %product_val62 = load %AngaraObject, ptr %product, align 4
  %rt_angara_to_string63 = call %AngaraObject @angara_to_string(%AngaraObject %product_val62)
  %rt_angara_string_concat64 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c61, %AngaraObject %rt_angara_to_string63)
  %rt_angara_create_nil65 = call %AngaraObject @angara_create_nil()
  %15 = alloca [2 x %AngaraObject], align 8
  %16 = getelementptr [2 x %AngaraObject], ptr %15, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6460, ptr %16, align 4
  %17 = getelementptr [2 x %AngaraObject], ptr %15, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat64, ptr %17, align 4
  %rt_Angara_io_println66 = call %AngaraObject @Angara_io_println(i32 2, ptr %15)
  %rt_angara_create_i6467 = call %AngaraObject @angara_create_i64(i64 10)
  %rt_angara_create_i6468 = call %AngaraObject @angara_create_i64(i64 20)
  %rt_angara_create_i6469 = call %AngaraObject @angara_create_i64(i64 30)
  %18 = alloca [3 x %AngaraObject], align 8
  %19 = getelementptr [3 x %AngaraObject], ptr %18, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6467, ptr %19, align 4
  %20 = getelementptr [3 x %AngaraObject], ptr %18, i64 0, i64 1
  store %AngaraObject %rt_angara_create_i6468, ptr %20, align 4
  %21 = getelementptr [3 x %AngaraObject], ptr %18, i64 0, i64 2
  store %AngaraObject %rt_angara_create_i6469, ptr %21, align 4
  %rt_angara_list_new_with_elements = call %AngaraObject @angara_list_new_with_elements(i64 3, ptr %18)
  store %AngaraObject %rt_angara_list_new_with_elements, ptr %items, align 4
  call void @angara_incref(%AngaraObject %rt_angara_list_new_with_elements)
  %rt_angara_create_i6470 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c71 = call %AngaraObject @angara_string_from_c(ptr @5)
  %items_val = load %AngaraObject, ptr %items, align 4
  %rt_angara_len = call %AngaraObject @angara_len(%AngaraObject %items_val)
  %rt_angara_to_string72 = call %AngaraObject @angara_to_string(%AngaraObject %rt_angara_len)
  %rt_angara_string_concat73 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c71, %AngaraObject %rt_angara_to_string72)
  %rt_angara_create_nil74 = call %AngaraObject @angara_create_nil()
  %22 = alloca [2 x %AngaraObject], align 8
  %23 = getelementptr [2 x %AngaraObject], ptr %22, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6470, ptr %23, align 4
  %24 = getelementptr [2 x %AngaraObject], ptr %22, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat73, ptr %24, align 4
  %rt_Angara_io_println75 = call %AngaraObject @Angara_io_println(i32 2, ptr %22)
  %rt_angara_create_i6476 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c77 = call %AngaraObject @angara_string_from_c(ptr @6)
  %items_val78 = load %AngaraObject, ptr %items, align 4
  %rt_angara_create_i6479 = call %AngaraObject @angara_create_i64(i64 0)
  %rt_angara_list_get = call %AngaraObject @angara_list_get(%AngaraObject %items_val78, %AngaraObject %rt_angara_create_i6479)
  %rt_angara_to_string80 = call %AngaraObject @angara_to_string(%AngaraObject %rt_angara_list_get)
  %rt_angara_string_concat81 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c77, %AngaraObject %rt_angara_to_string80)
  %rt_angara_create_nil82 = call %AngaraObject @angara_create_nil()
  %25 = alloca [2 x %AngaraObject], align 8
  %26 = getelementptr [2 x %AngaraObject], ptr %25, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6476, ptr %26, align 4
  %27 = getelementptr [2 x %AngaraObject], ptr %25, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat81, ptr %27, align 4
  %rt_Angara_io_println83 = call %AngaraObject @Angara_io_println(i32 2, ptr %25)
  %rt_angara_create_i6484 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c85 = call %AngaraObject @angara_string_from_c(ptr @7)
  %items_val86 = load %AngaraObject, ptr %items, align 4
  %rt_angara_create_i6487 = call %AngaraObject @angara_create_i64(i64 2)
  %rt_angara_list_get88 = call %AngaraObject @angara_list_get(%AngaraObject %items_val86, %AngaraObject %rt_angara_create_i6487)
  %rt_angara_to_string89 = call %AngaraObject @angara_to_string(%AngaraObject %rt_angara_list_get88)
  %rt_angara_string_concat90 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c85, %AngaraObject %rt_angara_to_string89)
  %rt_angara_create_nil91 = call %AngaraObject @angara_create_nil()
  %28 = alloca [2 x %AngaraObject], align 8
  %29 = getelementptr [2 x %AngaraObject], ptr %28, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i6484, ptr %29, align 4
  %30 = getelementptr [2 x %AngaraObject], ptr %28, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat90, ptr %30, align 4
  %rt_Angara_io_println92 = call %AngaraObject @Angara_io_println(i32 2, ptr %28)
  %rt_angara_create_i6493 = call %AngaraObject @angara_create_i64(i64 0)
  store %AngaraObject %rt_angara_create_i6493, ptr %total, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i6493)
  %items_val94 = load %AngaraObject, ptr %items, align 4
  %rt_angara_len95 = call %AngaraObject @angara_len(%AngaraObject %items_val94)
  %i64_val96 = extractvalue %AngaraObject %rt_angara_len95, 1
  %rt_angara_create_i6497 = call %AngaraObject @angara_create_i64(i64 0)
  store %AngaraObject %rt_angara_create_i6497, ptr %__for_idx_item, align 4
  %rt_angara_create_nil98 = call %AngaraObject @angara_create_nil()
  store %AngaraObject %rt_angara_create_nil98, ptr %item, align 4
  br label %forin.cond

forin.cond:                                       ; preds = %forin.body, %for.exit
  %__for_idx_item_val = load %AngaraObject, ptr %__for_idx_item, align 4
  %i64_val99 = extractvalue %AngaraObject %__for_idx_item_val, 1
  %forin.cmp = icmp slt i64 %i64_val99, %i64_val96
  br i1 %forin.cmp, label %forin.body, label %forin.exit

forin.body:                                       ; preds = %forin.cond
  %rt_angara_list_get100 = call %AngaraObject @angara_list_get(%AngaraObject %items_val94, %AngaraObject %__for_idx_item_val)
  store %AngaraObject %rt_angara_list_get100, ptr %item, align 4
  call void @angara_incref(%AngaraObject %rt_angara_list_get100)
  %total_val = load %AngaraObject, ptr %total, align 4
  %item_val = load %AngaraObject, ptr %item, align 4
  %i64_val101 = extractvalue %AngaraObject %total_val, 1
  %i64_val102 = extractvalue %AngaraObject %item_val, 1
  %iadd103 = add i64 %i64_val101, %i64_val102
  %rt_angara_create_i64104 = call %AngaraObject @angara_create_i64(i64 %iadd103)
  %total_val105 = load %AngaraObject, ptr %total, align 4
  call void @angara_decref(%AngaraObject %total_val105)
  store %AngaraObject %rt_angara_create_i64104, ptr %total, align 4
  call void @angara_incref(%AngaraObject %rt_angara_create_i64104)
  %old_loop_val = load %AngaraObject, ptr %item, align 4
  call void @angara_decref(%AngaraObject %old_loop_val)
  %__for_idx_item_val106 = load %AngaraObject, ptr %__for_idx_item, align 4
  %i64_val107 = extractvalue %AngaraObject %__for_idx_item_val106, 1
  %idx.inc = add i64 %i64_val107, 1
  %rt_angara_create_i64108 = call %AngaraObject @angara_create_i64(i64 %idx.inc)
  store %AngaraObject %rt_angara_create_i64108, ptr %__for_idx_item, align 4
  br label %forin.cond

forin.exit:                                       ; preds = %forin.cond
  call void @angara_decref(%AngaraObject %items_val94)
  %final_idx = load %AngaraObject, ptr %__for_idx_item, align 4
  call void @angara_decref(%AngaraObject %final_idx)
  %rt_angara_create_i64109 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c110 = call %AngaraObject @angara_string_from_c(ptr @8)
  %total_val111 = load %AngaraObject, ptr %total, align 4
  %rt_angara_to_string112 = call %AngaraObject @angara_to_string(%AngaraObject %total_val111)
  %rt_angara_string_concat113 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c110, %AngaraObject %rt_angara_to_string112)
  %rt_angara_create_nil114 = call %AngaraObject @angara_create_nil()
  %31 = alloca [2 x %AngaraObject], align 8
  %32 = getelementptr [2 x %AngaraObject], ptr %31, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i64109, ptr %32, align 4
  %33 = getelementptr [2 x %AngaraObject], ptr %31, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat113, ptr %33, align 4
  %rt_Angara_io_println115 = call %AngaraObject @Angara_io_println(i32 2, ptr %31)
  %rt_angara_create_i64116 = call %AngaraObject @angara_create_i64(i64 10)
  %g_fibonacci_val = load %AngaraObject, ptr @g_test_basics_fibonacci, align 4
  %34 = alloca [1 x %AngaraObject], align 8
  %35 = getelementptr [1 x %AngaraObject], ptr %34, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i64116, ptr %35, align 4
  %rt_angara_call = call %AngaraObject @angara_call(%AngaraObject %g_fibonacci_val, i32 1, ptr %34)
  store %AngaraObject %rt_angara_call, ptr %fib10, align 4
  call void @angara_incref(%AngaraObject %rt_angara_call)
  %rt_angara_create_i64117 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c118 = call %AngaraObject @angara_string_from_c(ptr @9)
  %fib10_val = load %AngaraObject, ptr %fib10, align 4
  %rt_angara_to_string119 = call %AngaraObject @angara_to_string(%AngaraObject %fib10_val)
  %rt_angara_string_concat120 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c118, %AngaraObject %rt_angara_to_string119)
  %rt_angara_create_nil121 = call %AngaraObject @angara_create_nil()
  %36 = alloca [2 x %AngaraObject], align 8
  %37 = getelementptr [2 x %AngaraObject], ptr %36, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i64117, ptr %37, align 4
  %38 = getelementptr [2 x %AngaraObject], ptr %36, i64 0, i64 1
  store %AngaraObject %rt_angara_string_concat120, ptr %38, align 4
  %rt_Angara_io_println122 = call %AngaraObject @Angara_io_println(i32 2, ptr %36)
  %rt_angara_string_from_c123 = call %AngaraObject @angara_string_from_c(ptr @10)
  %rt_angara_string_from_c124 = call %AngaraObject @angara_string_from_c(ptr @11)
  %rt_angara_string_concat125 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_from_c123, %AngaraObject %rt_angara_string_from_c124)
  %rt_angara_string_from_c126 = call %AngaraObject @angara_string_from_c(ptr @12)
  %rt_angara_string_concat127 = call %AngaraObject @angara_string_concat(%AngaraObject %rt_angara_string_concat125, %AngaraObject %rt_angara_string_from_c126)
  store %AngaraObject %rt_angara_string_concat127, ptr %greeting, align 4
  call void @angara_incref(%AngaraObject %rt_angara_string_concat127)
  %rt_angara_create_i64128 = call %AngaraObject @angara_create_i64(i64 1)
  %greeting_val = load %AngaraObject, ptr %greeting, align 4
  %rt_angara_create_nil129 = call %AngaraObject @angara_create_nil()
  %39 = alloca [2 x %AngaraObject], align 8
  %40 = getelementptr [2 x %AngaraObject], ptr %39, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i64128, ptr %40, align 4
  %41 = getelementptr [2 x %AngaraObject], ptr %39, i64 0, i64 1
  store %AngaraObject %greeting_val, ptr %41, align 4
  %rt_Angara_io_println130 = call %AngaraObject @Angara_io_println(i32 2, ptr %39)
  %rt_angara_create_i64131 = call %AngaraObject @angara_create_i64(i64 1)
  %rt_angara_string_from_c132 = call %AngaraObject @angara_string_from_c(ptr @13)
  %rt_angara_create_nil133 = call %AngaraObject @angara_create_nil()
  %42 = alloca [2 x %AngaraObject], align 8
  %43 = getelementptr [2 x %AngaraObject], ptr %42, i64 0, i64 0
  store %AngaraObject %rt_angara_create_i64131, ptr %43, align 4
  %44 = getelementptr [2 x %AngaraObject], ptr %42, i64 0, i64 1
  store %AngaraObject %rt_angara_string_from_c132, ptr %44, align 4
  %rt_Angara_io_println134 = call %AngaraObject @Angara_io_println(i32 2, ptr %42)
  %rt_angara_create_i64135 = call %AngaraObject @angara_create_i64(i64 0)
  ret %AngaraObject %rt_angara_create_i64135
}

declare %AngaraObject @Angara_io_println(i32, ptr)

declare %AngaraObject @angara_list_new_with_elements(i64, ptr)

declare %AngaraObject @angara_list_get(%AngaraObject, %AngaraObject)

define private %AngaraObject @angara_w_Angara_test_basics_main(i32 %0, ptr %1) {
entry:
  %result = call %AngaraObject @Angara_test_basics_main()
  ret %AngaraObject %result
}

define i32 @main() {
entry:
  call void @angara_runtime_init()
  %rt_angara_closure_new = call %AngaraObject @angara_closure_new(ptr @angara_w_Angara_test_basics_fibonacci, i32 1, i1 false)
  store %AngaraObject %rt_angara_closure_new, ptr @g_test_basics_fibonacci, align 4
  %rt_angara_closure_new1 = call %AngaraObject @angara_closure_new(ptr @angara_w_Angara_test_basics_main, i32 0, i1 false)
  store %AngaraObject %rt_angara_closure_new1, ptr @g_test_basics_main, align 4
  %main_closure = load %AngaraObject, ptr @g_test_basics_main, align 4
  %0 = alloca [0 x %AngaraObject], align 8
  %rt_angara_call = call %AngaraObject @angara_call(%AngaraObject %main_closure, i32 0, ptr %0)
  call void @angara_runtime_shutdown()
  ret i32 0
}

declare %AngaraObject @angara_closure_new(ptr, i32, i1)
