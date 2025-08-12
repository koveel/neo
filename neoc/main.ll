
%Player = type { ptr }

@gstr = private unnamed_addr constant [20 x i8] c"sonic the hedgehog\0A\00", align 1
@gstr.1 = private unnamed_addr constant [14 x i8] c"Hello, World!\00", align 1

declare void @printf(ptr)

define i32 @main() {
entry:
  %0 = alloca i32, align 4
  %1 = alloca %Player, align 8
  %2 = getelementptr inbounds %Player, ptr %1, i32 0, i32 0
  store ptr @gstr, ptr %2, align 8
  %3 = getelementptr inbounds %Player, ptr %1, i32 0, i32 0
  %4 = load ptr, ptr %3, align 8
  call void @printf(ptr %4)
  call void @printf(ptr @gstr.1)
  store i32 0, ptr %0, align 4
  br label %exit

exit:                                             ; preds = %entry
  %5 = load i32, ptr %0, align 4
  ret i32 %5
}
