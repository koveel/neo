
@gstr = private unnamed_addr constant [4 x i8] c"one\00", align 1
@gstr.1 = private unnamed_addr constant [4 x i8] c"two\00", align 1
@gstr.2 = private unnamed_addr constant [6 x i8] c"three\00", align 1

declare void @printf(ptr)

define i32 @main() {
entry:
  %return = alloca i32, align 4
  %nums = alloca [5 x i32], align 4
  %0 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 0
  store i32 0, ptr %0, align 4
  %1 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 1
  store i32 1, ptr %1, align 4
  %2 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 2
  store i32 2, ptr %2, align 4
  %3 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 3
  store i32 3, ptr %3, align 4
  %4 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 4
  store i32 4, ptr %4, align 4
  %msgs = alloca [3 x ptr], align 8
  %5 = getelementptr inbounds [3 x ptr], ptr %msgs, i32 0, i32 0
  store ptr @gstr, ptr %5, align 8
  %6 = getelementptr inbounds [3 x ptr], ptr %msgs, i32 0, i32 1
  store ptr @gstr.1, ptr %6, align 8
  %7 = getelementptr inbounds [3 x ptr], ptr %msgs, i32 0, i32 2
  store ptr @gstr.2, ptr %7, align 8
  %arr.idx = getelementptr inbounds [3 x ptr], ptr %msgs, i32 0, i32 0
  %msg = alloca ptr, align 8
  store ptr %arr.idx, ptr %msg, align 8
  %8 = load ptr, ptr %msg, align 8
  %9 = getelementptr inbounds ptr, ptr %8, i32 1
  store ptr %9, ptr %msg, align 8
  %10 = load ptr, ptr %msg, align 8
  %11 = load ptr, ptr %10, align 8
  call void @printf(ptr %11)
  %arr.idx1 = getelementptr inbounds [5 x i32], ptr %nums, i32 0, i32 0
  %num = alloca ptr, align 8
  store ptr %arr.idx1, ptr %num, align 8
  %12 = load ptr, ptr %num, align 8
  %13 = getelementptr inbounds i32, ptr %12, i32 1
  store ptr %13, ptr %num, align 8
  %14 = load ptr, ptr %num, align 8
  %15 = getelementptr inbounds i32, ptr %14, i32 1
  store ptr %15, ptr %num, align 8
  %16 = load ptr, ptr %num, align 8
  %17 = load ptr, ptr %16, align 8
  store ptr %17, ptr %return, align 8
  br label %exit

exit:                                             ; preds = %entry
  %18 = load i32, ptr %return, align 4
  ret i32 %18
}
