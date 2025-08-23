
@gstr = private unnamed_addr constant [14 x i8] c"Hello, World!\00", align 1

declare void @printf(ptr)

define i32 @main() {
entry:
  %return = alloca i32, align 4
  call void @printf(ptr @gstr)
  store i32 0, ptr %return, align 4
  br label %exit

exit:                                             ; preds = %entry
  %0 = load i32, ptr %return, align 4
  ret i32 %0
}
