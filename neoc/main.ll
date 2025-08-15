
@gstr = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@gstr.1 = private unnamed_addr constant [4 x i8] c"One\00", align 1
@gstr.2 = private unnamed_addr constant [4 x i8] c"Two\00", align 1
@gstr.3 = private unnamed_addr constant [18 x i8] c"Seventeenthousand\00", align 1

declare void @printf(ptr)

declare i64 @food_to_string(i32)

define void @print(ptr %msg) {
entry:
  %0 = alloca ptr, align 8
  store ptr %msg, ptr %0, align 8
  %1 = load ptr, ptr %0, align 8
  call void @printf(ptr %1)
  call void @printf(ptr @gstr)
  br label %exit

exit:                                             ; preds = %entry
  ret void
}

define i32 @main() {
entry:
  %return = alloca i32, align 4
  %msgs = alloca [3 x ptr], align 8
  %0 = getelementptr inbounds [3 x ptr], ptr %msgs, i32 0, i32 0
  store ptr @gstr.1, ptr %0, align 8
  %1 = getelementptr inbounds [3 x ptr], ptr %msgs, i32 0, i32 1
  store ptr @gstr.2, ptr %1, align 8
  %2 = getelementptr inbounds [3 x ptr], ptr %msgs, i32 0, i32 2
  store ptr @gstr.3, ptr %2, align 8
  %arr.idx = getelementptr inbounds [3 x ptr], ptr %msgs, i32 0
  %second = alloca ptr, align 8
  store ptr %arr.idx, ptr %second, align 8
  %ptr.add = getelementptr inbounds ptr, ptr %second, i32 1
  %3 = load ptr, ptr %ptr.add, align 8
  call void @print(ptr %3)
  %nums = alloca [5 x i32], align 4
  %4 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 0
  store i32 0, ptr %4, align 4
  %5 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 1
  store i32 1, ptr %5, align 4
  %6 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 2
  store i32 2, ptr %6, align 4
  %7 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 3
  store i32 3, ptr %7, align 4
  %8 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 4
  store i32 4, ptr %8, align 4
  %arr.idx1 = getelementptr inbounds [5 x i32], ptr %nums, i32 0
  %first = alloca ptr, align 8
  store ptr %arr.idx1, ptr %first, align 8
  %ptr.add2 = getelementptr inbounds i32, ptr %first, i32 1
  %9 = load i32, ptr %ptr.add2, align 4
  store i32 %9, ptr %return, align 4
  br label %exit

exit:                                             ; preds = %entry
  %10 = load i32, ptr %return, align 4
  ret i32 %10
}
