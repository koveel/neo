
@gstr = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

declare void @printf(ptr)

declare ptr @food_to_string(i32)

define void @print(ptr %msg) {
entry:
  %0 = alloca ptr, align 8
  store ptr %msg, ptr %0, align 8
  call void @printf(ptr %0)
  call void @printf(ptr @gstr)
  br label %exit

exit:                                             ; preds = %entry
  ret void
}

define i32 @main() {
entry:
  %0 = alloca i32, align 4
  %1 = alloca [6 x i32], align 4
  %2 = getelementptr inbounds [6 x i32], ptr %1, i32 0, i32 0
  store i32 0, ptr %2, align 4
  %3 = getelementptr inbounds [6 x i32], ptr %1, i32 0, i32 1
  store i32 1, ptr %3, align 4
  %4 = getelementptr inbounds [6 x i32], ptr %1, i32 0, i32 2
  store i32 0, ptr %4, align 4
  %5 = getelementptr inbounds [6 x i32], ptr %1, i32 0, i32 3
  store i32 3, ptr %5, align 4
  %6 = getelementptr inbounds [6 x i32], ptr %1, i32 0, i32 4
  store i32 4, ptr %6, align 4
  %7 = getelementptr inbounds [6 x i32], ptr %1, i32 0, i32 5
  store i32 5, ptr %7, align 4
  %8 = getelementptr inbounds [6 x i32], ptr %1, i32 0, i32 0
  %9 = alloca ptr, align 8
  store ptr %8, ptr %9, align 8
  %10 = load ptr, ptr %9, align 8
  %11 = getelementptr inbounds i32, ptr %10, i32 1
  store ptr %11, ptr %9, align 8
  %12 = load ptr, ptr %9, align 8
  %13 = getelementptr inbounds i32, ptr %12, i32 1
  store ptr %13, ptr %9, align 8
  %14 = load ptr, ptr %9, align 8
  %15 = getelementptr inbounds i32, ptr %14, i32 1
  store ptr %15, ptr %9, align 8
  %16 = load ptr, ptr %9, align 8
  %17 = load i32, ptr %16, align 4
  store i32 %17, ptr %0, align 4
  br label %exit

exit:                                             ; preds = %entry
  %18 = load i32, ptr %0, align 4
  ret i32 %18
}
