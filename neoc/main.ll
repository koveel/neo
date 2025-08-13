
@gstr = private unnamed_addr constant [4 x i8] c"One\00", align 1
@gstr.1 = private unnamed_addr constant [4 x i8] c"Two\00", align 1
@gstr.2 = private unnamed_addr constant [6 x i8] c"Three\00", align 1
@gstr.3 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

declare void @printf(ptr)

define i32 @main() {
entry:
  %0 = alloca i32, align 4
  %1 = alloca [3 x ptr], align 8
  %2 = getelementptr inbounds [3 x ptr], ptr %1, i32 0, i32 0
  store ptr @gstr, ptr %2, align 8
  %3 = getelementptr inbounds [3 x ptr], ptr %1, i32 0, i32 1
  store ptr @gstr.1, ptr %3, align 8
  %4 = getelementptr inbounds [3 x ptr], ptr %1, i32 0, i32 2
  store ptr @gstr.2, ptr %4, align 8
  %5 = alloca i32, align 4
  store i32 0, ptr %5, align 4
  %6 = alloca i32, align 4
  %it = alloca ptr, align 8
  %7 = getelementptr inbounds [3 x ptr], ptr %1, i32 0, i32 0
  %8 = load ptr, ptr %7, align 8
  store ptr %8, ptr %it, align 8
  store i32 0, ptr %6, align 4
  br label %for_cond

for_cond:                                         ; preds = %entry, %for_inc
  %9 = load i32, ptr %6, align 4
  %10 = icmp slt i32 %9, 3
  br i1 %10, label %for_body, label %for_end

for_inc:                                          ; preds = %brend, %btrue
  %11 = load i32, ptr %6, align 4
  %inc = add i32 %11, 1
  store i32 %inc, ptr %6, align 4
  %12 = load i32, ptr %6, align 4
  %13 = getelementptr inbounds [3 x ptr], ptr %1, i32 0, i32 %12
  %14 = load ptr, ptr %13, align 8
  store ptr %14, ptr %it, align 8
  br label %for_cond

for_body:                                         ; preds = %for_cond
  %15 = load i32, ptr %5, align 4
  %16 = add i32 %15, 1
  store i32 %16, ptr %5, align 4
  %17 = load i32, ptr %5, align 4
  %18 = icmp eq i32 %17, 0
  br i1 %18, label %btrue, label %brend

for_end:                                          ; preds = %for_cond
  store i32 2, ptr %0, align 4
  br label %exit

btrue:                                            ; preds = %for_body
  br label %for_inc

brend:                                            ; preds = %for_body
  %19 = load ptr, ptr %it, align 8
  call void @printf(ptr %19)
  call void @printf(ptr @gstr.3)
  br label %for_inc

exit:                                             ; preds = %for_end
  %20 = load i32, ptr %0, align 4
  ret i32 %20
}
