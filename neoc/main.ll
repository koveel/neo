
%String = type { ptr }

@gstr = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@gstr.1 = private unnamed_addr constant [4 x i8] c"One\00", align 1
@gstr.2 = private unnamed_addr constant [4 x i8] c"Two\00", align 1
@gstr.3 = private unnamed_addr constant [6 x i8] c"Three\00", align 1
@gstr.4 = private unnamed_addr constant [6 x i8] c"hello\00", align 1

declare void @printf(ptr)

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
  %0 = alloca %String, align 8
  %1 = getelementptr inbounds %String, ptr %0, i32 0, i32 0
  store ptr @gstr.1, ptr %1, align 8
  %2 = load %String, ptr %0, align 8
  %3 = alloca %String, align 8
  %4 = getelementptr inbounds %String, ptr %3, i32 0, i32 0
  store ptr @gstr.2, ptr %4, align 8
  %5 = load %String, ptr %3, align 8
  %6 = alloca %String, align 8
  %7 = getelementptr inbounds %String, ptr %6, i32 0, i32 0
  store ptr @gstr.3, ptr %7, align 8
  %8 = load %String, ptr %6, align 8
  %strings = alloca [3 x %String], align 8
  %9 = getelementptr inbounds [3 x %String], ptr %strings, i32 0, i32 0
  store %String %2, ptr %9, align 8
  %10 = getelementptr inbounds [3 x %String], ptr %strings, i32 0, i32 1
  store %String %5, ptr %10, align 8
  %11 = getelementptr inbounds [3 x %String], ptr %strings, i32 0, i32 2
  store %String %8, ptr %11, align 8
  %string = alloca %String, align 8
  %12 = getelementptr inbounds %String, ptr %string, i32 0, i32 0
  store ptr @gstr.4, ptr %12, align 8
  %13 = getelementptr inbounds %String, ptr %string, i32 0, i32 0
  %14 = load ptr, ptr %13, align 8
  call void @print(ptr %14)
  %arr.idx = getelementptr inbounds [3 x %String], ptr %strings, i32 0, i32 1
  %str = alloca ptr, align 8
  store ptr %arr.idx, ptr %str, align 8
  %15 = load ptr, ptr %str, align 8
  %16 = getelementptr inbounds %String, ptr %15, i32 1
  store ptr %16, ptr %str, align 8
  %17 = getelementptr inbounds %String, ptr %15, i32 0, i32 0
  %18 = load ptr, ptr %17, align 8
  call void @print(ptr %18)
  %19 = load ptr, ptr %str, align 8
  %20 = getelementptr inbounds %String, ptr %19, i32 0, i32 0
  %21 = load ptr, ptr %20, align 8
  call void @print(ptr %21)
  %nums = alloca [6 x i32], align 4
  %22 = getelementptr inbounds [6 x i32], ptr %nums, i32 0, i32 0
  store i32 0, ptr %22, align 4
  %23 = getelementptr inbounds [6 x i32], ptr %nums, i32 0, i32 1
  store i32 1, ptr %23, align 4
  %24 = getelementptr inbounds [6 x i32], ptr %nums, i32 0, i32 2
  store i32 2, ptr %24, align 4
  %25 = getelementptr inbounds [6 x i32], ptr %nums, i32 0, i32 3
  store i32 3, ptr %25, align 4
  %26 = getelementptr inbounds [6 x i32], ptr %nums, i32 0, i32 4
  store i32 4, ptr %26, align 4
  %27 = getelementptr inbounds [6 x i32], ptr %nums, i32 0, i32 5
  store i32 5, ptr %27, align 4
  %arr.idx1 = getelementptr inbounds [6 x i32], ptr %nums, i32 0, i32 2
  %pnum = alloca ptr, align 8
  store ptr %arr.idx1, ptr %pnum, align 8
  %28 = load ptr, ptr %pnum, align 8
  %29 = getelementptr inbounds i32, ptr %28, i32 1
  store ptr %29, ptr %pnum, align 8
  %30 = load i32, ptr %29, align 4
  store i32 %30, ptr %return, align 4
  br label %exit

exit:                                             ; preds = %entry
  %31 = load i32, ptr %return, align 4
  ret i32 %31
}
