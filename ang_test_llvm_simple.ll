; ModuleID = 'angara_module'
source_filename = "angara_module"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "aarch64-apple-darwin"

%AngaraObject = type { i32, i64 }
%AngaraString = type { %ObjHeader, i64, ptr }
%ObjHeader = type { i32, i64 }
%AngaraList = type { %ObjHeader, i64, i64, ptr }
%AngaraRecord = type { %ObjHeader, i64, i64, ptr }
%AngaraException = type { %ObjHeader, %AngaraObject }
%AngaraBoundMethod = type { %ObjHeader, %AngaraObject, %AngaraObject }
%RecordEntry = type { ptr, %AngaraObject }
%AngaraClosure = type { %ObjHeader, ptr, i32, i1 }
%ExceptionFrame = type { [200 x i8], ptr }
%ExceptionFrame.0 = type { [200 x i8], ptr }
%ExceptionFrame.1 = type { [200 x i8], ptr }

@__ang_exception_chain = common global ptr null
@__ang_current_exception = internal global %AngaraObject zeroinitializer
@0 = private unnamed_addr constant [4 x i8] c"nil\00", align 1
@1 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@2 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@4 = private unnamed_addr constant [6 x i8] c"%.15g\00", align 1
@5 = private unnamed_addr constant [9 x i8] c"<object>\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"nil\00", align 1
@7 = private unnamed_addr constant [5 x i8] c"bool\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"i64\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"f64\00", align 1
@10 = private unnamed_addr constant [7 x i8] c"object\00", align 1
@11 = private unnamed_addr constant [21 x i8] c"Unhandled exception\0A\00", align 1
@__stdoutp = external global ptr
@__stderrp = external global ptr
@12 = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@__stdinp = external global ptr

declare ptr @malloc(i64)

declare void @free(ptr)

declare ptr @realloc(ptr, i64)

declare ptr @memcpy(ptr, ptr, i64)

declare i64 @strlen(ptr)

declare i32 @strcmp(ptr, ptr)

declare ptr @strdup(ptr)

declare i32 @printf(ptr, ...)

declare i32 @fprintf(ptr, ptr, ...)

declare i32 @snprintf(ptr, i64, ptr, ...)

declare i64 @strtoll(ptr, ptr, i32)

declare double @strtod(ptr, ptr)

declare i32 @setjmp(ptr)

declare void @longjmp(ptr, i32)

declare void @exit(i32)

declare i32 @pthread_create(ptr, ptr, ptr, ptr)

declare i32 @pthread_join(ptr, ptr)

declare ptr @memset(ptr, i32, i64)

define internal void @__ang_free_object(ptr %0) {
entry:
  %obj_type = load i32, ptr %0, align 4
  switch i32 %obj_type, label %default_free [
    i32 0, label %is_string
    i32 1, label %is_list
    i32 2, label %is_record
    i32 3, label %is_exception
    i32 6, label %is_closure
    i32 12, label %is_bound
  ]

is_string:                                        ; preds = %entry
  %1 = getelementptr inbounds nuw %AngaraString, ptr %0, i32 0, i32 2
  %chars = load ptr, ptr %1, align 8
  call void @free(ptr %chars)
  call void @free(ptr %0)
  ret void

is_list:                                          ; preds = %entry
  %2 = getelementptr inbounds nuw %AngaraList, ptr %0, i32 0, i32 1
  %count = load i64, ptr %2, align 8
  %3 = getelementptr inbounds nuw %AngaraList, ptr %0, i32 0, i32 3
  %elems = load ptr, ptr %3, align 8
  br label %loop

is_record:                                        ; preds = %entry
  %4 = getelementptr inbounds nuw %AngaraRecord, ptr %0, i32 0, i32 1
  %count1 = load i64, ptr %4, align 8
  %5 = getelementptr inbounds nuw %AngaraRecord, ptr %0, i32 0, i32 3
  %entries = load ptr, ptr %5, align 8
  br label %rloop

is_exception:                                     ; preds = %entry
  %6 = getelementptr inbounds nuw %AngaraException, ptr %0, i32 0, i32 1
  %msg = load %AngaraObject, ptr %6, align 8
  call void @__ang_decref(%AngaraObject %msg)
  call void @free(ptr %0)
  ret void

is_closure:                                       ; preds = %entry
  call void @free(ptr %0)
  ret void

is_bound:                                         ; preds = %entry
  %7 = getelementptr inbounds nuw %AngaraBoundMethod, ptr %0, i32 0, i32 1
  %8 = load %AngaraObject, ptr %7, align 8
  %9 = getelementptr inbounds nuw %AngaraBoundMethod, ptr %0, i32 0, i32 2
  %10 = load %AngaraObject, ptr %9, align 8
  call void @__ang_decref(%AngaraObject %8)
  call void @__ang_decref(%AngaraObject %10)
  call void @free(ptr %0)
  ret void

default_free:                                     ; preds = %entry
  call void @free(ptr %0)
  ret void

loop:                                             ; preds = %body, %is_list
  %i = phi i64 [ 0, %is_list ], [ %next, %body ]
  %cmp = icmp slt i64 %i, %count
  br i1 %cmp, label %body, label %done_elems

body:                                             ; preds = %loop
  %11 = getelementptr %AngaraObject, ptr %elems, i64 %i
  %elem = load %AngaraObject, ptr %11, align 8
  call void @__ang_decref(%AngaraObject %elem)
  %next = add i64 %i, 1
  br label %loop

done_elems:                                       ; preds = %loop
  call void @free(ptr %elems)
  call void @free(ptr %0)
  ret void

rloop:                                            ; preds = %rbody, %is_record
  %i2 = phi i64 [ 0, %is_record ], [ %next4, %rbody ]
  %cmp3 = icmp slt i64 %i2, %count1
  br i1 %cmp3, label %rbody, label %rdone

rbody:                                            ; preds = %rloop
  %12 = getelementptr %RecordEntry, ptr %entries, i64 %i2
  %13 = getelementptr inbounds nuw %RecordEntry, ptr %12, i32 0, i32 0
  %key = load ptr, ptr %13, align 8
  %14 = getelementptr inbounds nuw %RecordEntry, ptr %12, i32 0, i32 1
  %val = load %AngaraObject, ptr %14, align 8
  call void @free(ptr %key)
  call void @__ang_decref(%AngaraObject %val)
  %next4 = add i64 %i2, 1
  br label %rloop

rdone:                                            ; preds = %rloop
  call void @free(ptr %entries)
  call void @free(ptr %0)
  ret void
}

define internal void @__ang_decref(%AngaraObject %0) {
entry:
  %tag = extractvalue %AngaraObject %0, 0
  %1 = icmp eq i32 %tag, 4
  br i1 %1, label %is_obj, label %done

is_obj:                                           ; preds = %entry
  %payload = extractvalue %AngaraObject %0, 1
  %obj_ptr = inttoptr i64 %payload to ptr
  %2 = getelementptr inbounds nuw %ObjHeader, ptr %obj_ptr, i32 0, i32 1
  %rc = load i64, ptr %2, align 8
  %new_rc = sub i64 %rc, 1
  store i64 %new_rc, ptr %2, align 8
  br label %check_zero

check_zero:                                       ; preds = %is_obj
  %3 = icmp eq i64 %new_rc, 0
  br i1 %3, label %free, label %done

free:                                             ; preds = %check_zero
  call void @__ang_free_object(ptr %obj_ptr)
  br label %done

done:                                             ; preds = %free, %check_zero, %entry
  ret void
}

define internal void @__ang_incref(%AngaraObject %0) {
entry:
  %tag = extractvalue %AngaraObject %0, 0
  %1 = icmp eq i32 %tag, 4
  br i1 %1, label %is_obj, label %done

is_obj:                                           ; preds = %entry
  %payload = extractvalue %AngaraObject %0, 1
  %obj_ptr = inttoptr i64 %payload to ptr
  %2 = getelementptr inbounds nuw %ObjHeader, ptr %obj_ptr, i32 0, i32 1
  %rc = load i64, ptr %2, align 8
  %new_rc = add i64 %rc, 1
  store i64 %new_rc, ptr %2, align 8
  br label %done

done:                                             ; preds = %is_obj, %entry
  ret void
}

define internal %AngaraObject @__ang_string_from_c(ptr %0) {
entry:
  %len = call i64 @strlen(ptr %0)
  %mem = call ptr @malloc(i64 32)
  %1 = getelementptr inbounds nuw %AngaraString, ptr %mem, i32 0, i32 0
  %2 = getelementptr inbounds nuw %ObjHeader, ptr %1, i32 0, i32 0
  store i32 0, ptr %2, align 4
  %3 = getelementptr inbounds nuw %ObjHeader, ptr %1, i32 0, i32 1
  store i64 1, ptr %3, align 8
  %4 = getelementptr inbounds nuw %AngaraString, ptr %mem, i32 0, i32 1
  store i64 %len, ptr %4, align 8
  %copied = call ptr @strdup(ptr %0)
  %5 = getelementptr inbounds nuw %AngaraString, ptr %mem, i32 0, i32 2
  store ptr %copied, ptr %5, align 8
  %6 = ptrtoint ptr %mem to i64
  %7 = insertvalue %AngaraObject { i32 4, i64 undef }, i64 %6, 1
  ret %AngaraObject %7
}

define internal %AngaraObject @__ang_string_concat(%AngaraObject %0, %AngaraObject %1) {
entry:
  %2 = extractvalue %AngaraObject %0, 1
  %3 = inttoptr i64 %2 to ptr
  %4 = getelementptr inbounds nuw %AngaraString, ptr %3, i32 0, i32 2
  %5 = load ptr, ptr %4, align 8
  %6 = getelementptr inbounds nuw %AngaraString, ptr %3, i32 0, i32 1
  %7 = load i64, ptr %6, align 8
  %8 = extractvalue %AngaraObject %1, 1
  %9 = inttoptr i64 %8 to ptr
  %10 = getelementptr inbounds nuw %AngaraString, ptr %9, i32 0, i32 2
  %11 = load ptr, ptr %10, align 8
  %12 = getelementptr inbounds nuw %AngaraString, ptr %9, i32 0, i32 1
  %13 = load i64, ptr %12, align 8
  %new_len = add i64 %7, %13
  %14 = add i64 %new_len, 1
  %buf = call ptr @malloc(i64 %14)
  %15 = call ptr @memcpy(ptr %buf, ptr %5, i64 %7)
  %16 = getelementptr i8, ptr %buf, i64 %7
  %17 = call ptr @memcpy(ptr %16, ptr %11, i64 %13)
  %18 = getelementptr i8, ptr %buf, i64 %new_len
  store i8 0, ptr %18, align 1
  %mem = call ptr @malloc(i64 32)
  %19 = getelementptr inbounds nuw %AngaraString, ptr %mem, i32 0, i32 0
  %20 = getelementptr inbounds nuw %ObjHeader, ptr %19, i32 0, i32 0
  store i32 0, ptr %20, align 4
  %21 = getelementptr inbounds nuw %ObjHeader, ptr %19, i32 0, i32 1
  store i64 1, ptr %21, align 8
  %22 = getelementptr inbounds nuw %AngaraString, ptr %mem, i32 0, i32 1
  store i64 %new_len, ptr %22, align 8
  %23 = getelementptr inbounds nuw %AngaraString, ptr %mem, i32 0, i32 2
  store ptr %buf, ptr %23, align 8
  %24 = ptrtoint ptr %mem to i64
  %25 = insertvalue %AngaraObject { i32 4, i64 undef }, i64 %24, 1
  ret %AngaraObject %25
}

define internal %AngaraObject @__ang_to_string(%AngaraObject %0) {
entry:
  %tag = extractvalue %AngaraObject %0, 0
  switch i32 %tag, label %nil [
    i32 0, label %nil
    i32 1, label %bool
    i32 2, label %i64
    i32 3, label %f64
    i32 4, label %obj
  ]

nil:                                              ; preds = %entry, %entry
  %1 = call %AngaraObject @__ang_string_from_c(ptr @0)
  ret %AngaraObject %1

bool:                                             ; preds = %entry
  %2 = extractvalue %AngaraObject %0, 1
  %b = trunc i64 %2 to i1
  br i1 %b, label %true, label %false

i64:                                              ; preds = %entry
  %3 = extractvalue %AngaraObject %0, 1
  %4 = alloca [32 x i8], align 1
  %5 = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %4, i64 32, ptr @3, i64 %3)
  %6 = call %AngaraObject @__ang_string_from_c(ptr %4)
  ret %AngaraObject %6

f64:                                              ; preds = %entry
  %7 = extractvalue %AngaraObject %0, 1
  %dval = bitcast i64 %7 to double
  %8 = alloca [64 x i8], align 1
  %9 = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %8, i64 64, ptr @4, double %dval)
  %10 = call %AngaraObject @__ang_string_from_c(ptr %8)
  ret %AngaraObject %10

obj:                                              ; preds = %entry
  %11 = extractvalue %AngaraObject %0, 1
  %12 = inttoptr i64 %11 to ptr
  %13 = getelementptr inbounds nuw %ObjHeader, ptr %12, i32 0, i32 0
  %obj_type = load i32, ptr %13, align 4
  %14 = icmp eq i32 %obj_type, 0
  br i1 %14, label %is_string, label %not_str

is_string:                                        ; preds = %obj
  call void @__ang_incref(%AngaraObject %0)
  ret %AngaraObject %0

merge:                                            ; No predecessors!
  ret %AngaraObject undef

true:                                             ; preds = %bool
  %15 = call %AngaraObject @__ang_string_from_c(ptr @1)
  ret %AngaraObject %15

false:                                            ; preds = %bool
  %16 = call %AngaraObject @__ang_string_from_c(ptr @2)
  ret %AngaraObject %16

not_str:                                          ; preds = %obj
  %17 = call %AngaraObject @__ang_string_from_c(ptr @5)
  ret %AngaraObject %17
}

define internal %AngaraObject @__ang_list_new() {
entry:
  %mem = call ptr @malloc(i64 40)
  %0 = getelementptr inbounds nuw %AngaraList, ptr %mem, i32 0, i32 0
  %1 = getelementptr inbounds nuw %ObjHeader, ptr %0, i32 0, i32 0
  store i32 1, ptr %1, align 4
  %2 = getelementptr inbounds nuw %ObjHeader, ptr %0, i32 0, i32 1
  store i64 1, ptr %2, align 8
  %3 = getelementptr inbounds nuw %AngaraList, ptr %mem, i32 0, i32 1
  store i64 0, ptr %3, align 8
  %4 = getelementptr inbounds nuw %AngaraList, ptr %mem, i32 0, i32 2
  store i64 0, ptr %4, align 8
  %5 = getelementptr inbounds nuw %AngaraList, ptr %mem, i32 0, i32 3
  store ptr null, ptr %5, align 8
  %6 = ptrtoint ptr %mem to i64
  %7 = insertvalue %AngaraObject { i32 4, i64 undef }, i64 %6, 1
  ret %AngaraObject %7
}

define internal %AngaraObject @__ang_list_new_with_elements(i64 %0, ptr %1) {
entry:
  %2 = call ptr @malloc(i64 40)
  %3 = getelementptr inbounds nuw %AngaraList, ptr %2, i32 0, i32 0
  %4 = getelementptr inbounds nuw %ObjHeader, ptr %3, i32 0, i32 0
  store i32 1, ptr %4, align 4
  %5 = getelementptr inbounds nuw %ObjHeader, ptr %3, i32 0, i32 1
  store i64 1, ptr %5, align 8
  %6 = getelementptr inbounds nuw %AngaraList, ptr %2, i32 0, i32 1
  store i64 %0, ptr %6, align 8
  %7 = getelementptr inbounds nuw %AngaraList, ptr %2, i32 0, i32 2
  store i64 %0, ptr %7, align 8
  %8 = mul i64 %0, 16
  %9 = call ptr @malloc(i64 %8)
  %10 = call ptr @memcpy(ptr %9, ptr %1, i64 %8)
  %11 = getelementptr inbounds nuw %AngaraList, ptr %2, i32 0, i32 3
  store ptr %9, ptr %11, align 8
  br label %loop

loop:                                             ; preds = %body, %entry
  %i = phi i64 [ 0, %entry ], [ %15, %body ]
  %12 = icmp slt i64 %i, %0
  br i1 %12, label %body, label %done

body:                                             ; preds = %loop
  %13 = getelementptr %AngaraObject, ptr %9, i64 %i
  %14 = load %AngaraObject, ptr %13, align 8
  call void @__ang_incref(%AngaraObject %14)
  %15 = add i64 %i, 1
  br label %loop

done:                                             ; preds = %loop
  %16 = ptrtoint ptr %2 to i64
  %17 = insertvalue %AngaraObject { i32 4, i64 undef }, i64 %16, 1
  ret %AngaraObject %17
}

define internal void @__ang_list_push(%AngaraObject %0, %AngaraObject %1) {
entry:
  %2 = extractvalue %AngaraObject %0, 1
  %3 = inttoptr i64 %2 to ptr
  %4 = getelementptr inbounds nuw %AngaraList, ptr %3, i32 0, i32 1
  %5 = getelementptr inbounds nuw %AngaraList, ptr %3, i32 0, i32 2
  %6 = getelementptr inbounds nuw %AngaraList, ptr %3, i32 0, i32 3
  %count = load i64, ptr %4, align 8
  %cap = load i64, ptr %5, align 8
  %need_grow = icmp eq i64 %count, %cap
  br i1 %need_grow, label %grow, label %store

grow:                                             ; preds = %entry
  %7 = add i64 %count, 1
  %8 = shl i64 %cap, 1
  %9 = icmp slt i64 %8, %7
  %new_cap = select i1 %9, i64 %7, i64 %8
  %10 = mul i64 %new_cap, 16
  %11 = load ptr, ptr %6, align 8
  %new_raw = call ptr @realloc(ptr %11, i64 %10)
  store ptr %new_raw, ptr %6, align 8
  store i64 %new_cap, ptr %5, align 8
  br label %store

store:                                            ; preds = %grow, %entry
  %count2 = load i64, ptr %4, align 8
  %elems2 = load ptr, ptr %6, align 8
  %12 = getelementptr %AngaraObject, ptr %elems2, i64 %count2
  store %AngaraObject %1, ptr %12, align 8
  call void @__ang_incref(%AngaraObject %1)
  %13 = add i64 %count2, 1
  store i64 %13, ptr %4, align 8
  ret void
}

define internal %AngaraObject @__ang_list_get(%AngaraObject %0, %AngaraObject %1) {
entry:
  %2 = extractvalue %AngaraObject %1, 1
  %3 = extractvalue %AngaraObject %0, 1
  %4 = inttoptr i64 %3 to ptr
  %5 = getelementptr inbounds nuw %AngaraList, ptr %4, i32 0, i32 1
  %count = load i64, ptr %5, align 8
  %6 = icmp sge i64 %2, 0
  %7 = icmp slt i64 %2, %count
  %in_bounds1 = and i1 %6, %7
  br i1 %in_bounds1, label %in_bounds, label %done

in_bounds:                                        ; preds = %entry
  %8 = getelementptr inbounds nuw %AngaraList, ptr %4, i32 0, i32 3
  %elems = load ptr, ptr %8, align 8
  %9 = getelementptr %AngaraObject, ptr %elems, i64 %2
  %result = load %AngaraObject, ptr %9, align 8
  br label %done

done:                                             ; preds = %in_bounds, %entry
  %val = phi %AngaraObject [ %result, %in_bounds ], [ zeroinitializer, %entry ]
  ret %AngaraObject %val
}

define internal void @__ang_list_set(%AngaraObject %0, %AngaraObject %1, %AngaraObject %2) {
entry:
  %3 = extractvalue %AngaraObject %1, 1
  %4 = extractvalue %AngaraObject %0, 1
  %5 = inttoptr i64 %4 to ptr
  %6 = getelementptr inbounds nuw %AngaraList, ptr %5, i32 0, i32 3
  %elems = load ptr, ptr %6, align 8
  %7 = getelementptr %AngaraObject, ptr %elems, i64 %3
  %old = load %AngaraObject, ptr %7, align 8
  call void @__ang_decref(%AngaraObject %old)
  store %AngaraObject %2, ptr %7, align 8
  call void @__ang_incref(%AngaraObject %2)
  ret void
}

define internal %AngaraObject @__ang_record_new() {
entry:
  %0 = call ptr @malloc(i64 40)
  %1 = getelementptr inbounds nuw %AngaraRecord, ptr %0, i32 0, i32 0
  %2 = getelementptr inbounds nuw %ObjHeader, ptr %1, i32 0, i32 0
  store i32 2, ptr %2, align 4
  %3 = getelementptr inbounds nuw %ObjHeader, ptr %1, i32 0, i32 1
  store i64 1, ptr %3, align 8
  %4 = getelementptr inbounds nuw %AngaraRecord, ptr %0, i32 0, i32 1
  store i64 0, ptr %4, align 8
  %5 = getelementptr inbounds nuw %AngaraRecord, ptr %0, i32 0, i32 2
  store i64 0, ptr %5, align 8
  %6 = getelementptr inbounds nuw %AngaraRecord, ptr %0, i32 0, i32 3
  store ptr null, ptr %6, align 8
  %7 = ptrtoint ptr %0 to i64
  %8 = insertvalue %AngaraObject { i32 4, i64 undef }, i64 %7, 1
  ret %AngaraObject %8
}

define internal %AngaraObject @__ang_record_get(%AngaraObject %0, ptr %1) {
entry:
  %2 = extractvalue %AngaraObject %0, 1
  %3 = inttoptr i64 %2 to ptr
  %4 = getelementptr inbounds nuw %AngaraRecord, ptr %3, i32 0, i32 1
  %count = load i64, ptr %4, align 8
  %5 = getelementptr inbounds nuw %AngaraRecord, ptr %3, i32 0, i32 3
  %entries = load ptr, ptr %5, align 8
  br label %loop

loop:                                             ; preds = %next, %entry
  %i = phi i64 [ 0, %entry ], [ %14, %next ]
  %6 = icmp slt i64 %i, %count
  br i1 %6, label %body, label %not_found

body:                                             ; preds = %loop
  %7 = getelementptr %RecordEntry, ptr %entries, i64 %i
  %8 = getelementptr inbounds nuw %RecordEntry, ptr %7, i32 0, i32 0
  %9 = load ptr, ptr %8, align 8
  %cmp = call i32 @strcmp(ptr %9, ptr %1)
  %10 = icmp eq i32 %cmp, 0
  br i1 %10, label %found, label %next

found:                                            ; preds = %body
  %11 = getelementptr %RecordEntry, ptr %entries, i64 %i
  %12 = getelementptr inbounds nuw %RecordEntry, ptr %11, i32 0, i32 1
  %13 = load %AngaraObject, ptr %12, align 8
  ret %AngaraObject %13

not_found:                                        ; preds = %loop
  ret %AngaraObject zeroinitializer

next:                                             ; preds = %body
  %14 = add i64 %i, 1
  br label %loop
}

define internal void @__ang_record_set(%AngaraObject %0, ptr %1, %AngaraObject %2) {
entry:
  %3 = extractvalue %AngaraObject %0, 1
  %4 = inttoptr i64 %3 to ptr
  %5 = getelementptr inbounds nuw %AngaraRecord, ptr %4, i32 0, i32 1
  %6 = getelementptr inbounds nuw %AngaraRecord, ptr %4, i32 0, i32 2
  %7 = getelementptr inbounds nuw %AngaraRecord, ptr %4, i32 0, i32 3
  %count = load i64, ptr %5, align 8
  %entries = load ptr, ptr %7, align 8
  br label %loop

loop:                                             ; preds = %next, %entry
  %i = phi i64 [ 0, %entry ], [ %30, %next ]
  %8 = icmp slt i64 %i, %count
  br i1 %8, label %body, label %not_found

body:                                             ; preds = %loop
  %9 = getelementptr %RecordEntry, ptr %entries, i64 %i
  %10 = getelementptr inbounds nuw %RecordEntry, ptr %9, i32 0, i32 0
  %11 = load ptr, ptr %10, align 8
  %12 = call i32 @strcmp(ptr %11, ptr %1)
  %13 = icmp eq i32 %12, 0
  br i1 %13, label %found, label %next

found:                                            ; preds = %body
  %14 = getelementptr %RecordEntry, ptr %entries, i64 %i
  %15 = getelementptr inbounds nuw %RecordEntry, ptr %14, i32 0, i32 1
  %old = load %AngaraObject, ptr %15, align 8
  call void @__ang_decref(%AngaraObject %old)
  store %AngaraObject %2, ptr %15, align 8
  call void @__ang_incref(%AngaraObject %2)
  br label %done

not_found:                                        ; preds = %loop
  %cap = load i64, ptr %6, align 8
  %16 = icmp eq i64 %count, %cap
  br i1 %16, label %grow, label %insert

grow:                                             ; preds = %not_found
  %17 = add i64 %count, 1
  %18 = shl i64 %cap, 1
  %19 = icmp slt i64 %18, %17
  %20 = select i1 %19, i64 %17, i64 %18
  %21 = mul i64 %20, 24
  %22 = call ptr @realloc(ptr %entries, i64 %21)
  store ptr %22, ptr %7, align 8
  store i64 %20, ptr %6, align 8
  br label %insert

insert:                                           ; preds = %grow, %not_found
  %23 = load ptr, ptr %7, align 8
  %24 = load i64, ptr %5, align 8
  %25 = getelementptr %RecordEntry, ptr %23, i64 %24
  %26 = call ptr @strdup(ptr %1)
  %27 = getelementptr inbounds nuw %RecordEntry, ptr %25, i32 0, i32 0
  store ptr %26, ptr %27, align 8
  %28 = getelementptr inbounds nuw %RecordEntry, ptr %25, i32 0, i32 1
  store %AngaraObject %2, ptr %28, align 8
  call void @__ang_incref(%AngaraObject %2)
  %29 = add i64 %24, 1
  store i64 %29, ptr %5, align 8
  br label %done

done:                                             ; preds = %insert, %found
  ret void

next:                                             ; preds = %body
  %30 = add i64 %i, 1
  br label %loop
}

define internal %AngaraObject @__ang_to_i64(%AngaraObject %0) {
entry:
  %tag = extractvalue %AngaraObject %0, 0
  switch i32 %tag, label %default [
    i32 2, label %is_i64
    i32 3, label %is_f64
    i32 1, label %is_bool
  ]

is_i64:                                           ; preds = %entry
  ret %AngaraObject %0

is_f64:                                           ; preds = %entry
  %1 = extractvalue %AngaraObject %0, 1
  %2 = bitcast i64 %1 to double
  %3 = fptosi double %2 to i64
  %4 = insertvalue %AngaraObject { i32 2, i64 undef }, i64 %3, 1
  ret %AngaraObject %4

is_bool:                                          ; preds = %entry
  %5 = extractvalue %AngaraObject %0, 1
  %6 = trunc i64 %5 to i1
  %7 = zext i1 %6 to i64
  %8 = insertvalue %AngaraObject { i32 2, i64 undef }, i64 %7, 1
  ret %AngaraObject %8

default:                                          ; preds = %entry
  ret %AngaraObject zeroinitializer
}

define internal %AngaraObject @__ang_to_f64(%AngaraObject %0) {
entry:
  %1 = extractvalue %AngaraObject %0, 0
  switch i32 %1, label %default [
    i32 3, label %is_f64
    i32 2, label %is_i64
  ]

is_f64:                                           ; preds = %entry
  ret %AngaraObject %0

is_i64:                                           ; preds = %entry
  %2 = extractvalue %AngaraObject %0, 1
  %3 = sitofp i64 %2 to double
  %4 = bitcast double %3 to i64
  %5 = insertvalue %AngaraObject { i32 3, i64 undef }, i64 %4, 1
  ret %AngaraObject %5

default:                                          ; preds = %entry
  ret %AngaraObject zeroinitializer
}

define internal %AngaraObject @__ang_to_bool(%AngaraObject %0) {
entry:
  %tag = extractvalue %AngaraObject %0, 0
  %payload = extractvalue %AngaraObject %0, 1
  switch i32 %tag, label %nil [
    i32 0, label %nil
    i32 1, label %bool
    i32 2, label %i64
    i32 3, label %f64
    i32 4, label %obj
  ]

nil:                                              ; preds = %entry, %entry
  ret %AngaraObject { i32 1, i64 0 }

bool:                                             ; preds = %entry
  %1 = trunc i64 %payload to i1
  %2 = zext i1 %1 to i64
  %3 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %2, 1
  ret %AngaraObject %3

i64:                                              ; preds = %entry
  %4 = icmp ne i64 %payload, 0
  %5 = zext i1 %4 to i64
  %6 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %5, 1
  ret %AngaraObject %6

f64:                                              ; preds = %entry
  %7 = bitcast i64 %payload to double
  %8 = fcmp une double %7, 0.000000e+00
  %9 = zext i1 %8 to i64
  %10 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %9, 1
  ret %AngaraObject %10

obj:                                              ; preds = %entry
  ret %AngaraObject { i32 1, i64 1 }
}

define internal %AngaraObject @__ang_typeof(%AngaraObject %0) {
entry:
  %1 = extractvalue %AngaraObject %0, 0
  switch i32 %1, label %nil [
    i32 0, label %nil
    i32 1, label %bool
    i32 2, label %i64
    i32 3, label %f64
    i32 4, label %obj
  ]

nil:                                              ; preds = %entry, %entry
  %2 = call %AngaraObject @__ang_string_from_c(ptr @6)
  ret %AngaraObject %2

bool:                                             ; preds = %entry
  %3 = call %AngaraObject @__ang_string_from_c(ptr @7)
  ret %AngaraObject %3

i64:                                              ; preds = %entry
  %4 = call %AngaraObject @__ang_string_from_c(ptr @8)
  ret %AngaraObject %4

f64:                                              ; preds = %entry
  %5 = call %AngaraObject @__ang_string_from_c(ptr @9)
  ret %AngaraObject %5

obj:                                              ; preds = %entry
  %6 = call %AngaraObject @__ang_string_from_c(ptr @10)
  ret %AngaraObject %6
}

define internal %AngaraObject @__ang_equals(%AngaraObject %0, %AngaraObject %1) {
entry:
  %tag_a = extractvalue %AngaraObject %0, 0
  %tag_b = extractvalue %AngaraObject %1, 0
  %tags_eq = icmp eq i32 %tag_a, %tag_b
  %2 = icmp eq i32 %tag_a, 2
  %3 = icmp eq i32 %tag_b, 3
  %4 = and i1 %2, %3
  %5 = icmp eq i32 %tag_a, 3
  %6 = icmp eq i32 %tag_b, 2
  %7 = and i1 %5, %6
  %8 = or i1 %4, %7
  %can_compare = or i1 %tags_eq, %8
  br i1 %can_compare, label %check_same, label %not_eq

not_eq:                                           ; preds = %entry
  ret %AngaraObject { i32 1, i64 0 }

check_same:                                       ; preds = %entry
  %9 = extractvalue %AngaraObject %0, 1
  %10 = extractvalue %AngaraObject %1, 1
  br i1 %tags_eq, label %same_tag, label %cross_num

same_tag:                                         ; preds = %check_same
  switch i32 %tag_a, label %nil_eq [
    i32 1, label %bool_eq
    i32 2, label %i64_eq
    i32 3, label %f64_eq
    i32 4, label %obj_eq
  ]

cross_num:                                        ; preds = %check_same
  %11 = sitofp i64 %9 to double
  %12 = bitcast i64 %10 to double
  %13 = icmp eq i32 %tag_a, 2
  %14 = bitcast i64 %9 to double
  %15 = select i1 %13, double %11, double %14
  %16 = bitcast i64 %10 to double
  %17 = select i1 %13, double %12, double %16
  %18 = fcmp oeq double %15, %17
  %19 = zext i1 %18 to i64
  %20 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %19, 1
  ret %AngaraObject %20

nil_eq:                                           ; preds = %same_tag
  ret %AngaraObject { i32 1, i64 1 }

bool_eq:                                          ; preds = %same_tag
  %21 = trunc i64 %9 to i1
  %22 = trunc i64 %10 to i1
  %23 = icmp eq i1 %21, %22
  %24 = zext i1 %23 to i64
  %25 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %24, 1
  ret %AngaraObject %25

i64_eq:                                           ; preds = %same_tag
  %26 = icmp eq i64 %9, %10
  %27 = zext i1 %26 to i64
  %28 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %27, 1
  ret %AngaraObject %28

f64_eq:                                           ; preds = %same_tag
  %29 = bitcast i64 %9 to double
  %30 = bitcast i64 %10 to double
  %31 = fcmp oeq double %29, %30
  %32 = zext i1 %31 to i64
  %33 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %32, 1
  ret %AngaraObject %33

obj_eq:                                           ; preds = %same_tag
  %34 = inttoptr i64 %9 to ptr
  %35 = getelementptr inbounds nuw %ObjHeader, ptr %34, i32 0, i32 0
  %36 = load i32, ptr %35, align 4
  %37 = icmp eq i32 %36, 0
  %38 = inttoptr i64 %10 to ptr
  %39 = getelementptr inbounds nuw %ObjHeader, ptr %38, i32 0, i32 0
  %40 = load i32, ptr %39, align 4
  %41 = icmp eq i32 %40, 0
  %42 = and i1 %37, %41
  br i1 %42, label %is_str_eq, label %ptr_eq

is_str_eq:                                        ; preds = %obj_eq
  %43 = inttoptr i64 %9 to ptr
  %44 = inttoptr i64 %10 to ptr
  %45 = getelementptr inbounds nuw %AngaraString, ptr %43, i32 0, i32 2
  %46 = load ptr, ptr %45, align 8
  %47 = getelementptr inbounds nuw %AngaraString, ptr %44, i32 0, i32 2
  %48 = load ptr, ptr %47, align 8
  %49 = call i32 @strcmp(ptr %46, ptr %48)
  %50 = icmp eq i32 %49, 0
  %51 = zext i1 %50 to i64
  %52 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %51, 1
  ret %AngaraObject %52

ptr_eq:                                           ; preds = %obj_eq
  %53 = icmp eq i64 %9, %10
  %54 = zext i1 %53 to i64
  %55 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %54, 1
  ret %AngaraObject %55
}

define internal %AngaraObject @__ang_closure_new(ptr %0, i32 %1, i1 %2) {
entry:
  %3 = call ptr @malloc(i64 32)
  %4 = getelementptr inbounds nuw %AngaraClosure, ptr %3, i32 0, i32 0
  %5 = getelementptr inbounds nuw %ObjHeader, ptr %4, i32 0, i32 0
  store i32 6, ptr %5, align 4
  %6 = getelementptr inbounds nuw %ObjHeader, ptr %4, i32 0, i32 1
  store i64 1, ptr %6, align 8
  %7 = getelementptr inbounds nuw %AngaraClosure, ptr %3, i32 0, i32 1
  store ptr %0, ptr %7, align 8
  %8 = getelementptr inbounds nuw %AngaraClosure, ptr %3, i32 0, i32 2
  store i32 %1, ptr %8, align 4
  %9 = getelementptr inbounds nuw %AngaraClosure, ptr %3, i32 0, i32 3
  store i1 %2, ptr %9, align 1
  %10 = ptrtoint ptr %3 to i64
  %11 = insertvalue %AngaraObject { i32 4, i64 undef }, i64 %10, 1
  ret %AngaraObject %11
}

define internal %AngaraObject @__ang_call(%AngaraObject %0, i32 %1, ptr %2) {
entry:
  %3 = extractvalue %AngaraObject %0, 1
  %4 = inttoptr i64 %3 to ptr
  %5 = getelementptr inbounds nuw %ObjHeader, ptr %4, i32 0, i32 0
  %6 = load i32, ptr %5, align 4
  switch i32 %6, label %error [
    i32 6, label %is_closure
    i32 12, label %is_bound
  ]

is_closure:                                       ; preds = %entry
  %7 = inttoptr i64 %3 to ptr
  %8 = getelementptr inbounds nuw %AngaraClosure, ptr %7, i32 0, i32 1
  %fn = load ptr, ptr %8, align 8
  %9 = call %AngaraObject %fn(i32 %1, ptr %2)
  ret %AngaraObject %9

is_bound:                                         ; preds = %entry
  %10 = inttoptr i64 %3 to ptr
  %11 = getelementptr inbounds nuw %AngaraBoundMethod, ptr %10, i32 0, i32 1
  %12 = load %AngaraObject, ptr %11, align 8
  %13 = getelementptr inbounds nuw %AngaraBoundMethod, ptr %10, i32 0, i32 2
  %14 = load %AngaraObject, ptr %13, align 8
  %15 = add i32 %1, 1
  %16 = sext i32 %15 to i64
  %17 = mul i64 %16, 16
  %18 = call ptr @malloc(i64 %17)
  store %AngaraObject %12, ptr %18, align 8
  %19 = getelementptr %AngaraObject, ptr %18, i64 1
  %20 = sext i32 %1 to i64
  %21 = mul i64 %20, 16
  %22 = call ptr @memcpy(ptr %19, ptr %2, i64 %21)
  %23 = extractvalue %AngaraObject %14, 1
  %24 = inttoptr i64 %23 to ptr
  %25 = getelementptr inbounds nuw %AngaraClosure, ptr %24, i32 0, i32 1
  %26 = load ptr, ptr %25, align 8
  %27 = call %AngaraObject %26(i32 %15, ptr %18)
  call void @free(ptr %18)
  ret %AngaraObject %27

error:                                            ; preds = %entry
  ret %AngaraObject zeroinitializer
}

define internal %AngaraObject @__ang_bound_method_new(%AngaraObject %0, %AngaraObject %1) {
entry:
  %2 = call ptr @malloc(i64 48)
  %3 = getelementptr inbounds nuw %AngaraBoundMethod, ptr %2, i32 0, i32 0
  %4 = getelementptr inbounds nuw %ObjHeader, ptr %3, i32 0, i32 0
  store i32 12, ptr %4, align 4
  %5 = getelementptr inbounds nuw %ObjHeader, ptr %3, i32 0, i32 1
  store i64 1, ptr %5, align 8
  %6 = getelementptr inbounds nuw %AngaraBoundMethod, ptr %2, i32 0, i32 1
  store %AngaraObject %0, ptr %6, align 8
  %7 = getelementptr inbounds nuw %AngaraBoundMethod, ptr %2, i32 0, i32 2
  store %AngaraObject %1, ptr %7, align 8
  call void @__ang_incref(%AngaraObject %0)
  call void @__ang_incref(%AngaraObject %1)
  %8 = ptrtoint ptr %2 to i64
  %9 = insertvalue %AngaraObject { i32 4, i64 undef }, i64 %8, 1
  ret %AngaraObject %9
}

define internal %AngaraObject @__ang_exception_new(%AngaraObject %0) {
entry:
  %1 = call ptr @malloc(i64 32)
  %2 = getelementptr inbounds nuw %AngaraException, ptr %1, i32 0, i32 0
  %3 = getelementptr inbounds nuw %ObjHeader, ptr %2, i32 0, i32 0
  store i32 3, ptr %3, align 4
  %4 = getelementptr inbounds nuw %ObjHeader, ptr %2, i32 0, i32 1
  store i64 1, ptr %4, align 8
  %5 = getelementptr inbounds nuw %AngaraException, ptr %1, i32 0, i32 1
  store %AngaraObject %0, ptr %5, align 8
  call void @__ang_incref(%AngaraObject %0)
  %6 = ptrtoint ptr %1 to i64
  %7 = insertvalue %AngaraObject { i32 4, i64 undef }, i64 %6, 1
  ret %AngaraObject %7
}

define internal void @__ang_throw(%AngaraObject %0) {
entry:
  store %AngaraObject %0, ptr @__ang_current_exception, align 8
  %chain = load ptr, ptr @__ang_exception_chain, align 8
  %1 = icmp eq ptr %chain, null
  br i1 %1, label %abort, label %unwind

abort:                                            ; preds = %entry
  %2 = call i32 (ptr, ...) @printf(ptr @11)
  call void @exit(i32 1)
  unreachable

unwind:                                           ; preds = %entry
  %3 = getelementptr inbounds nuw %ExceptionFrame, ptr %chain, i32 0, i32 1
  %prev = load ptr, ptr %3, align 8
  store ptr %prev, ptr @__ang_exception_chain, align 8
  %4 = getelementptr inbounds nuw %ExceptionFrame, ptr %chain, i32 0, i32 0
  call void @longjmp(ptr %4, i32 1)
  unreachable
}

define internal i32 @__ang_try_begin(ptr %0) {
entry:
  %1 = getelementptr inbounds nuw %ExceptionFrame.0, ptr %0, i32 0, i32 1
  %old_chain = load ptr, ptr @__ang_exception_chain, align 8
  store ptr %old_chain, ptr %1, align 8
  store ptr %0, ptr @__ang_exception_chain, align 8
  %2 = getelementptr inbounds nuw %ExceptionFrame.0, ptr %0, i32 0, i32 0
  %setjmp_result = call i32 @setjmp(ptr %2)
  ret i32 %setjmp_result
}

define internal void @__ang_try_end() {
entry:
  %chain = load ptr, ptr @__ang_exception_chain, align 8
  %0 = getelementptr inbounds nuw %ExceptionFrame.1, ptr %chain, i32 0, i32 1
  %prev = load ptr, ptr %0, align 8
  store ptr %prev, ptr @__ang_exception_chain, align 8
  ret void
}

define internal %AngaraObject @__ang_spawn_thread(%AngaraObject %0, i32 %1, ptr %2) {
entry:
  ret %AngaraObject zeroinitializer
}

define internal %AngaraObject @__ang_thread_join(%AngaraObject %0) {
entry:
  ret %AngaraObject zeroinitializer
}

define internal %AngaraObject @__ang_mutex_new() {
entry:
  ret %AngaraObject zeroinitializer
}

define internal void @__ang_mutex_lock(%AngaraObject %0) {
entry:
  ret void
}

define internal void @__ang_mutex_unlock(%AngaraObject %0) {
entry:
  ret void
}

define internal %AngaraObject @__ang_len(%AngaraObject %0) {
entry:
  %tag = extractvalue %AngaraObject %0, 0
  %1 = icmp eq i32 %tag, 4
  br i1 %1, label %is_obj, label %not_obj

is_obj:                                           ; preds = %entry
  %2 = extractvalue %AngaraObject %0, 1
  %3 = inttoptr i64 %2 to ptr
  %4 = getelementptr inbounds nuw %ObjHeader, ptr %3, i32 0, i32 0
  %5 = load i32, ptr %4, align 4
  switch i32 %5, label %default [
    i32 0, label %obj_is_string
    i32 1, label %obj_is_list
  ]

not_obj:                                          ; preds = %entry
  ret %AngaraObject { i32 2, i64 0 }

obj_is_string:                                    ; preds = %is_obj
  %6 = inttoptr i64 %2 to ptr
  %7 = getelementptr inbounds nuw %AngaraString, ptr %6, i32 0, i32 1
  %len = load i64, ptr %7, align 8
  %8 = insertvalue %AngaraObject { i32 2, i64 undef }, i64 %len, 1
  ret %AngaraObject %8

obj_is_list:                                      ; preds = %is_obj
  %9 = inttoptr i64 %2 to ptr
  %10 = getelementptr inbounds nuw %AngaraList, ptr %9, i32 0, i32 1
  %count = load i64, ptr %10, align 8
  %11 = insertvalue %AngaraObject { i32 2, i64 undef }, i64 %count, 1
  ret %AngaraObject %11

default:                                          ; preds = %is_obj
  ret %AngaraObject { i32 2, i64 undef }
}

define internal void @__ang_io_print(%AngaraObject %0, %AngaraObject %1) {
entry:
  %str = call %AngaraObject @__ang_to_string(%AngaraObject %1)
  %2 = extractvalue %AngaraObject %str, 1
  %3 = inttoptr i64 %2 to ptr
  %4 = getelementptr inbounds nuw %AngaraString, ptr %3, i32 0, i32 2
  %cstr = load ptr, ptr %4, align 8
  %5 = extractvalue %AngaraObject %0, 1
  %6 = icmp eq i64 %5, 2
  %stderr = load ptr, ptr @__stderrp, align 8
  %stdout = load ptr, ptr @__stdoutp, align 8
  %7 = select i1 %6, ptr %stderr, ptr %stdout
  %8 = call i32 (ptr, ptr, ...) @fprintf(ptr %7, ptr @12, ptr %cstr)
  call void @__ang_decref(%AngaraObject %str)
  ret void
}

define internal void @__ang_io_println(%AngaraObject %0, %AngaraObject %1) {
entry:
  %str = call %AngaraObject @__ang_to_string(%AngaraObject %1)
  %2 = extractvalue %AngaraObject %str, 1
  %3 = inttoptr i64 %2 to ptr
  %4 = getelementptr inbounds nuw %AngaraString, ptr %3, i32 0, i32 2
  %cstr = load ptr, ptr %4, align 8
  %5 = extractvalue %AngaraObject %0, 1
  %6 = icmp eq i64 %5, 2
  %stderr = load ptr, ptr @__stderrp, align 8
  %stdout = load ptr, ptr @__stdoutp, align 8
  %7 = select i1 %6, ptr %stderr, ptr %stdout
  %8 = call i32 (ptr, ptr, ...) @fprintf(ptr %7, ptr @13, ptr %cstr)
  call void @__ang_decref(%AngaraObject %str)
  ret void
}

define internal void @__ang_io_write(%AngaraObject %0, %AngaraObject %1) {
entry:
  %2 = extractvalue %AngaraObject %0, 1
  %str = call %AngaraObject @__ang_to_string(%AngaraObject %1)
  %3 = extractvalue %AngaraObject %str, 1
  %4 = inttoptr i64 %3 to ptr
  %5 = getelementptr inbounds nuw %AngaraString, ptr %4, i32 0, i32 2
  %cstr = load ptr, ptr %5, align 8
  %6 = icmp eq i64 %2, 2
  %stderr = load ptr, ptr @__stderrp, align 8
  %stdout = load ptr, ptr @__stdoutp, align 8
  %7 = select i1 %6, ptr %stderr, ptr %stdout
  %8 = call i32 (ptr, ptr, ...) @fprintf(ptr %7, ptr @14, ptr %cstr)
  call void @__ang_decref(%AngaraObject %str)
  ret void
}

define internal void @__ang_io_flush(%AngaraObject %0) {
entry:
  %1 = extractvalue %AngaraObject %0, 1
  %2 = icmp eq i64 %1, 2
  %3 = load ptr, ptr @__stderrp, align 8
  %4 = load ptr, ptr @__stdoutp, align 8
  %5 = select i1 %2, ptr %3, ptr %4
  %6 = call i32 @fflush(ptr %5)
  ret void
}

declare i32 @fflush(ptr)

define internal %AngaraObject @__ang_io_read_line() {
entry:
  %0 = alloca ptr, align 8
  store ptr null, ptr %0, align 8
  %1 = alloca i64, align 8
  store i64 0, ptr %1, align 8
  %stdin = load ptr, ptr @__stdinp, align 8
  %line_size = call i64 @getline(ptr %0, ptr %1, ptr %stdin)
  %2 = icmp slt i64 %line_size, 0
  br i1 %2, label %eof, label %ok

ok:                                               ; preds = %entry
  %chars = load ptr, ptr %0, align 8
  %3 = sub i64 %line_size, 1
  %4 = getelementptr i8, ptr %chars, i64 %3
  %last = load i8, ptr %4, align 1
  %5 = icmp eq i8 %last, 10
  br i1 %5, label %strip, label %keep

eof:                                              ; preds = %entry
  %buf = load ptr, ptr %0, align 8
  %6 = icmp eq ptr %buf, null
  br i1 %6, label %skip_free, label %do_free

skip_free:                                        ; preds = %do_free, %eof
  ret %AngaraObject zeroinitializer

do_free:                                          ; preds = %eof
  call void @free(ptr %buf)
  br label %skip_free

strip:                                            ; preds = %ok
  store i8 0, ptr %4, align 1
  br label %keep

keep:                                             ; preds = %strip, %ok
  %7 = call %AngaraObject @__ang_string_from_c(ptr %chars)
  call void @free(ptr %chars)
  ret %AngaraObject %7
}

declare i64 @getline(ptr, ptr, ptr)

define internal %AngaraObject @__ang_io_read_all() {
entry:
  %0 = alloca i64, align 8
  store i64 4096, ptr %0, align 8
  %1 = alloca i64, align 8
  store i64 0, ptr %1, align 8
  %2 = alloca ptr, align 8
  %3 = call ptr @malloc(i64 4096)
  store ptr %3, ptr %2, align 8
  br label %loop

loop:                                             ; preds = %grow, %entry
  %cap = load i64, ptr %0, align 8
  %total = load i64, ptr %1, align 8
  %buf = load ptr, ptr %2, align 8
  %4 = sub i64 %cap, %total
  %5 = getelementptr i8, ptr %buf, i64 %total
  %stdin = load ptr, ptr @__stdinp, align 8
  %bytes_read = call i64 @fread(ptr %5, i64 1, i64 %4, ptr %stdin)
  %6 = add i64 %total, %bytes_read
  store i64 %6, ptr %1, align 8
  %7 = icmp eq i64 %bytes_read, 0
  %8 = icmp eq i64 %6, %cap
  %9 = xor i1 %8, true
  %10 = or i1 %7, %9
  br i1 %7, label %done, label %grow

grow:                                             ; preds = %loop
  %11 = load i64, ptr %0, align 8
  %12 = load ptr, ptr %2, align 8
  %13 = shl i64 %11, 1
  store i64 %13, ptr %0, align 8
  %14 = call ptr @realloc(ptr %12, i64 %13)
  store ptr %14, ptr %2, align 8
  br label %loop

done:                                             ; preds = %loop
  %final_buf = load ptr, ptr %2, align 8
  %final_total = load i64, ptr %1, align 8
  %15 = getelementptr i8, ptr %final_buf, i64 %final_total
  store i8 0, ptr %15, align 1
  %16 = call %AngaraObject @__ang_string_from_c(ptr %final_buf)
  call void @free(ptr %final_buf)
  ret %AngaraObject %16
}

declare i64 @fread(ptr, i64, i64, ptr)

define %AngaraObject @__ang_test_llvm_simple_add(%AngaraObject %a, %AngaraObject %b) {
entry:
  %b2 = alloca %AngaraObject, align 8
  %a1 = alloca %AngaraObject, align 8
  store %AngaraObject %a, ptr %a1, align 8
  store %AngaraObject %b, ptr %b2, align 8
  %a3 = load %AngaraObject, ptr %a1, align 8
  %b4 = load %AngaraObject, ptr %b2, align 8
  %0 = extractvalue %AngaraObject %a3, 0
  %1 = extractvalue %AngaraObject %b4, 0
  %2 = icmp eq i32 %0, 2
  %3 = icmp eq i32 %1, 2
  %4 = and i1 %2, %3
  br i1 %4, label %iadd, label %sadd

iadd:                                             ; preds = %entry
  %5 = extractvalue %AngaraObject %a3, 1
  %6 = extractvalue %AngaraObject %b4, 1
  %7 = add i64 %5, %6
  %8 = insertvalue %AngaraObject { i32 2, i64 undef }, i64 %7, 1
  br label %madd

sadd:                                             ; preds = %entry
  %9 = call %AngaraObject @__ang_string_concat(%AngaraObject %a3, %AngaraObject %b4)
  br label %madd

madd:                                             ; preds = %sadd, %iadd
  %10 = phi %AngaraObject [ %8, %iadd ], [ %9, %sadd ]
  ret %AngaraObject %10
}

define %AngaraObject @__ang_test_llvm_simple_factorial(%AngaraObject %n) {
entry:
  %n1 = alloca %AngaraObject, align 8
  store %AngaraObject %n, ptr %n1, align 8
  %n2 = load %AngaraObject, ptr %n1, align 8
  %0 = extractvalue %AngaraObject %n2, 1
  %1 = icmp sle i64 %0, 1
  %2 = zext i1 %1 to i64
  %3 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %2, 1
  %4 = extractvalue %AngaraObject %3, 0
  %5 = icmp eq i32 %4, 4
  %6 = extractvalue %AngaraObject %3, 1
  %7 = icmp ne i64 %6, 0
  %8 = select i1 %5, i1 true, i1 %7
  br i1 %8, label %then, label %else

then:                                             ; preds = %entry
  ret %AngaraObject { i32 2, i64 1 }

else:                                             ; preds = %entry
  br label %ifm

ifm:                                              ; preds = %else
  %n3 = load %AngaraObject, ptr %n1, align 8
  %n4 = load %AngaraObject, ptr %n1, align 8
  %9 = extractvalue %AngaraObject %n4, 1
  %10 = sub i64 %9, 1
  %11 = insertvalue %AngaraObject { i32 2, i64 undef }, i64 %10, 1
  %12 = call %AngaraObject @__ang_test_llvm_simple_factorial(%AngaraObject %11)
  %13 = extractvalue %AngaraObject %n3, 1
  %14 = extractvalue %AngaraObject %12, 1
  %15 = mul i64 %13, %14
  %16 = insertvalue %AngaraObject { i32 2, i64 undef }, i64 %15, 1
  ret %AngaraObject %16
}

define %AngaraObject @__ang_test_llvm_simple_fibonacci(%AngaraObject %n) {
entry:
  %n1 = alloca %AngaraObject, align 8
  store %AngaraObject %n, ptr %n1, align 8
  %n2 = load %AngaraObject, ptr %n1, align 8
  %0 = extractvalue %AngaraObject %n2, 1
  %1 = icmp sle i64 %0, 1
  %2 = zext i1 %1 to i64
  %3 = insertvalue %AngaraObject { i32 1, i64 undef }, i64 %2, 1
  %4 = extractvalue %AngaraObject %3, 0
  %5 = icmp eq i32 %4, 4
  %6 = extractvalue %AngaraObject %3, 1
  %7 = icmp ne i64 %6, 0
  %8 = select i1 %5, i1 true, i1 %7
  br i1 %8, label %then, label %else

then:                                             ; preds = %entry
  %n3 = load %AngaraObject, ptr %n1, align 8
  ret %AngaraObject %n3

else:                                             ; preds = %entry
  br label %ifm

ifm:                                              ; preds = %else
  %n4 = load %AngaraObject, ptr %n1, align 8
  %9 = extractvalue %AngaraObject %n4, 1
  %10 = sub i64 %9, 1
  %11 = insertvalue %AngaraObject { i32 2, i64 undef }, i64 %10, 1
  %12 = call %AngaraObject @__ang_test_llvm_simple_fibonacci(%AngaraObject %11)
  %n5 = load %AngaraObject, ptr %n1, align 8
  %13 = extractvalue %AngaraObject %n5, 1
  %14 = sub i64 %13, 2
  %15 = insertvalue %AngaraObject { i32 2, i64 undef }, i64 %14, 1
  %16 = call %AngaraObject @__ang_test_llvm_simple_fibonacci(%AngaraObject %15)
  %17 = extractvalue %AngaraObject %12, 0
  %18 = extractvalue %AngaraObject %16, 0
  %19 = icmp eq i32 %17, 2
  %20 = icmp eq i32 %18, 2
  %21 = and i1 %19, %20
  br i1 %21, label %iadd, label %sadd

iadd:                                             ; preds = %ifm
  %22 = extractvalue %AngaraObject %12, 1
  %23 = extractvalue %AngaraObject %16, 1
  %24 = add i64 %22, %23
  %25 = insertvalue %AngaraObject { i32 2, i64 undef }, i64 %24, 1
  br label %madd

sadd:                                             ; preds = %ifm
  %26 = call %AngaraObject @__ang_string_concat(%AngaraObject %12, %AngaraObject %16)
  br label %madd

madd:                                             ; preds = %sadd, %iadd
  %27 = phi %AngaraObject [ %25, %iadd ], [ %26, %sadd ]
  ret %AngaraObject %27
}

define %AngaraObject @__ang_test_llvm_simple_main() {
entry:
  %fib = alloca %AngaraObject, align 8
  %f = alloca %AngaraObject, align 8
  %x = alloca %AngaraObject, align 8
  %0 = call %AngaraObject @__ang_test_llvm_simple_add(%AngaraObject { i32 2, i64 3 }, %AngaraObject { i32 2, i64 4 })
  store %AngaraObject %0, ptr %x, align 8
  %1 = call %AngaraObject @__ang_test_llvm_simple_factorial(%AngaraObject { i32 2, i64 10 })
  store %AngaraObject %1, ptr %f, align 8
  %2 = call %AngaraObject @__ang_test_llvm_simple_fibonacci(%AngaraObject { i32 2, i64 10 })
  store %AngaraObject %2, ptr %fib, align 8
  ret %AngaraObject zeroinitializer
}

define i32 @main() {
entry:
  %0 = call %AngaraObject @__ang_test_llvm_simple_main()
  ret i32 0
}
