
@gstr = private unnamed_addr constant [14 x i8] c"Hello, World!\00", align 1

declare void @printf(ptr)

define i32 @sum(i32 %a, i32 %b) {
entry:
  %returnv = alloca i32, align 4
  %0 = alloca i32, align 4
  store i32 %a, ptr %0, align 4
  %1 = alloca i32, align 4
  store i32 %b, ptr %1, align 4
  %2 = load i32, ptr %0, align 4
  %3 = load i32, ptr %1, align 4
  %4 = add i32 %2, %3
  store i32 %4, ptr %returnv, align 4
  br label %exit

exit:                                             ; preds = %entry
  %5 = load i32, ptr %returnv, align 4
  ret i32 %5
}

define i32 @main() {
entry:
  %returnv = alloca i32, align 4
  %text = alloca ptr, align 8
  store ptr @gstr, ptr %text, align 8
  %0 = alloca i32, align 4
  store i32 0, ptr %0, align 4
  br label %for_cond

for_cond:                                         ; preds = %entry, %for_inc
  %1 = load i32, ptr %0, align 4
  %2 = icmp slt i32 %1, 100
  br i1 %2, label %for_body, label %for_end

for_inc:                                          ; preds = %for_body
  %3 = load i32, ptr %0, align 4
  %inc = add i32 %3, 1
  store i32 %inc, ptr %0, align 4
  br label %for_cond

for_body:                                         ; preds = %for_cond
  %4 = load ptr, ptr %text, align 8
  call void @printf(ptr %4)
  br label %for_inc

for_end:                                          ; preds = %for_cond
  %5 = call i32 @sum(i32 10, i32 5)
  store i32 %5, ptr %returnv, align 4
  br label %exit

exit:                                             ; preds = %for_end
  %6 = load i32, ptr %returnv, align 4
  ret i32 %6
}
