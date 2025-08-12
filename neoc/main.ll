
%Player = type { i32, %Tag }
%Tag = type { ptr, i32 }

@gstr = private unnamed_addr constant [9 x i8] c"Player 1\00", align 1
@gstr.1 = private unnamed_addr constant [6 x i8] c"loop\0A\00", align 1

declare void @printf(ptr)

define i32 @main() {
entry:
  %0 = alloca i32, align 4
  %1 = alloca %Player, align 8
  %2 = getelementptr inbounds %Player, ptr %1, i32 0, i32 0
  store i32 18, ptr %2, align 4
  %3 = alloca %Tag, align 8
  %4 = getelementptr inbounds %Tag, ptr %3, i32 0, i32 0
  store ptr @gstr, ptr %4, align 8
  %5 = getelementptr inbounds %Tag, ptr %3, i32 0, i32 1
  store i32 5, ptr %5, align 4
  %6 = load %Tag, ptr %3, align 8
  %7 = getelementptr inbounds %Player, ptr %1, i32 0, i32 1
  store %Tag %6, ptr %7, align 8
  %8 = alloca i32, align 4
  %9 = getelementptr inbounds %Player, ptr %1, i32 0, i32 1
  %10 = getelementptr inbounds %Tag, ptr %9, i32 0, i32 1
  %11 = load i32, ptr %10, align 4
  store i32 0, ptr %8, align 4
  br label %for_cond

for_cond:                                         ; preds = %entry, %for_inc
  %12 = load i32, ptr %8, align 4
  %13 = icmp slt i32 %12, %11
  br i1 %13, label %for_body, label %for_end

for_inc:                                          ; preds = %for_body
  %14 = load i32, ptr %8, align 4
  %inc = add i32 %14, 1
  store i32 %inc, ptr %8, align 4
  br label %for_cond

for_body:                                         ; preds = %for_cond
  call void @printf(ptr @gstr.1)
  br label %for_inc

for_end:                                          ; preds = %for_cond
  store i32 0, ptr %0, align 4
  br label %exit

exit:                                             ; preds = %for_end
  %15 = load i32, ptr %0, align 4
  ret i32 %15
}
