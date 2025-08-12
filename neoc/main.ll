
@gstr = private unnamed_addr constant [4 x i8] c"One\00", align 1
@gstr.1 = private unnamed_addr constant [4 x i8] c"Two\00", align 1
@gstr.2 = private unnamed_addr constant [6 x i8] c"Three\00", align 1
@gstr.3 = private unnamed_addr constant [5 x i8] c"Four\00", align 1
@gstr.4 = private unnamed_addr constant [5 x i8] c"Five\00", align 1
@gstr.5 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

declare void @printf(ptr)

define [5 x ptr] @get_messages() {
entry:
  %0 = alloca [5 x ptr], align 8
  %1 = alloca [5 x ptr], align 8
  %2 = getelementptr inbounds [5 x ptr], ptr %1, i32 0, i32 0
  store ptr @gstr, ptr %2, align 8
  %3 = getelementptr inbounds [5 x ptr], ptr %1, i32 0, i32 1
  store ptr @gstr.1, ptr %3, align 8
  %4 = getelementptr inbounds [5 x ptr], ptr %1, i32 0, i32 2
  store ptr @gstr.2, ptr %4, align 8
  %5 = getelementptr inbounds [5 x ptr], ptr %1, i32 0, i32 3
  store ptr @gstr.3, ptr %5, align 8
  %6 = getelementptr inbounds [5 x ptr], ptr %1, i32 0, i32 4
  store ptr @gstr.4, ptr %6, align 8
  %7 = load [5 x ptr], ptr %1, align 8
  store [5 x ptr] %7, ptr %0, align 8
  br label %exit

exit:                                             ; preds = %entry
  %8 = load [5 x ptr], ptr %0, align 8
  ret [5 x ptr] %8
}

define i32 @main() {
entry:
  %0 = alloca i32, align 4
  %1 = call [5 x ptr] @get_messages()
  %2 = alloca [5 x ptr], align 8
  store [5 x ptr] %1, ptr %2, align 8
  %3 = alloca i32, align 4
  %4 = alloca ptr, align 8
  %5 = getelementptr inbounds [5 x ptr], ptr %2, i32 0, i32 0
  %6 = load ptr, ptr %5, align 8
  store ptr %6, ptr %4, align 8
  store i32 0, ptr %3, align 4
  br label %for_cond

for_cond:                                         ; preds = %entry, %for_inc
  %7 = load i32, ptr %3, align 4
  %8 = icmp slt i32 %7, 5
  br i1 %8, label %for_body, label %for_end

for_inc:                                          ; preds = %for_body
  %9 = load i32, ptr %3, align 4
  %inc = add i32 %9, 1
  store i32 %inc, ptr %3, align 4
  br label %for_cond

for_body:                                         ; preds = %for_cond
  %10 = load i32, ptr %3, align 4
  %11 = getelementptr inbounds [5 x ptr], ptr %2, i32 0, i32 %10
  %12 = load ptr, ptr %11, align 8
  store ptr %12, ptr %4, align 8
  %13 = load ptr, ptr %4, align 8
  call void @printf(ptr %13)
  call void @printf(ptr @gstr.5)
  br label %for_inc

for_end:                                          ; preds = %for_cond
  store i32 0, ptr %0, align 4
  br label %exit

exit:                                             ; preds = %for_end
  %14 = load i32, ptr %0, align 4
  ret i32 %14
}
