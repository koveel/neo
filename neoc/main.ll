
@gstr = private unnamed_addr constant [4 x i8] c"One\00", align 1
@gstr.1 = private unnamed_addr constant [4 x i8] c"Two\00", align 1
@gstr.2 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

declare void @printf(ptr)

define i32 @main() {
entry:
  %0 = alloca i32, align 4
  %1 = alloca [2 x ptr], align 8
  %2 = getelementptr inbounds [2 x ptr], ptr %1, i32 0, i32 0
  store ptr @gstr, ptr %2, align 8
  %3 = getelementptr inbounds [2 x ptr], ptr %1, i32 0, i32 1
  store ptr @gstr.1, ptr %3, align 8
  %4 = alloca i32, align 4
  %5 = alloca ptr, align 8
  %6 = getelementptr inbounds [2 x ptr], ptr %1, i32 0, i32 0
  %7 = load ptr, ptr %6, align 8
  store ptr %7, ptr %5, align 8
  store i32 0, ptr %4, align 4
  br label %for_cond

for_cond:                                         ; preds = %entry, %for_inc
  %8 = load i32, ptr %4, align 4
  %9 = icmp slt i32 %8, 2
  br i1 %9, label %for_body, label %for_end

for_inc:                                          ; preds = %for_body
  %10 = load i32, ptr %4, align 4
  %inc = add i32 %10, 1
  store i32 %inc, ptr %4, align 4
  br label %for_cond

for_body:                                         ; preds = %for_cond
  %11 = load i32, ptr %4, align 4
  %12 = getelementptr inbounds [2 x ptr], ptr %1, i32 0, i32 %11
  %13 = load ptr, ptr %12, align 8
  store ptr %13, ptr %5, align 8
  %14 = load ptr, ptr %5, align 8
  call void @printf(ptr %14)
  call void @printf(ptr @gstr.2)
  br label %for_inc

for_end:                                          ; preds = %for_cond
  store i32 2, ptr %0, align 4
  br label %exit

exit:                                             ; preds = %for_end
  %15 = load i32, ptr %0, align 4
  ret i32 %15
}
