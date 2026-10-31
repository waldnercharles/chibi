@cmd0 = private constant [9 x i16] [i16 112, i16 114, i16 111, i16 103, i16 46, i16 101, i16 120, i16 101, i16 0]
@exp0_0 = private constant [8 x i8] [i8 112, i8 114, i8 111, i8 103, i8 46, i8 101, i8 120, i8 101]
@cmd1 = private constant [13 x i16] [i16 112, i16 114, i16 111, i16 103, i16 46, i16 101, i16 120, i16 101, i16 32, i16 97, i16 32, i16 98, i16 0]
@exp1_0 = private constant [8 x i8] [i8 112, i8 114, i8 111, i8 103, i8 46, i8 101, i8 120, i8 101]
@exp1_1 = private constant [1 x i8] [i8 97]
@exp1_2 = private constant [1 x i8] [i8 98]
@cmd2 = private constant [33 x i16] [i16 34, i16 67, i16 58, i16 92, i16 80, i16 114, i16 111, i16 103, i16 114, i16 97, i16 109, i16 32, i16 70, i16 105, i16 108, i16 101, i16 115, i16 92, i16 120, i16 46, i16 101, i16 120, i16 101, i16 34, i16 32, i16 34, i16 97, i16 32, i16 98, i16 34, i16 32, i16 99, i16 0]
@exp2_0 = private constant [22 x i8] [i8 67, i8 58, i8 92, i8 80, i8 114, i8 111, i8 103, i8 114, i8 97, i8 109, i8 32, i8 70, i8 105, i8 108, i8 101, i8 115, i8 92, i8 120, i8 46, i8 101, i8 120, i8 101]
@exp2_1 = private constant [3 x i8] [i8 97, i8 32, i8 98]
@exp2_2 = private constant [1 x i8] [i8 99]
@cmd3 = private constant [12 x i16] [i16 112, i16 32, i16 32, i16 9, i16 32, i16 97, i16 32, i16 32, i16 32, i16 98, i16 32, i16 0]
@exp3_0 = private constant [1 x i8] [i8 112]
@exp3_1 = private constant [1 x i8] [i8 97]
@exp3_2 = private constant [1 x i8] [i8 98]
@cmd4 = private constant [7 x i16] [i16 112, i16 32, i16 34, i16 34, i16 32, i16 120, i16 0]
@exp4_0 = private constant [1 x i8] [i8 112]
@exp4_1 = private constant [1 x i8] [i8 0]
@exp4_2 = private constant [1 x i8] [i8 120]
@cmd5 = private constant [11 x i16] [i16 112, i16 32, i16 97, i16 92, i16 98, i16 32, i16 99, i16 92, i16 92, i16 100, i16 0]
@exp5_0 = private constant [1 x i8] [i8 112]
@exp5_1 = private constant [3 x i8] [i8 97, i8 92, i8 98]
@exp5_2 = private constant [4 x i8] [i8 99, i8 92, i8 92, i8 100]
@cmd6 = private constant [11 x i16] [i16 112, i16 32, i16 34, i16 97, i16 92, i16 34, i16 98, i16 34, i16 32, i16 99, i16 0]
@exp6_0 = private constant [1 x i8] [i8 112]
@exp6_1 = private constant [3 x i8] [i8 97, i8 34, i8 98]
@exp6_2 = private constant [1 x i8] [i8 99]
@cmd7 = private constant [10 x i16] [i16 112, i16 32, i16 34, i16 97, i16 92, i16 92, i16 34, i16 32, i16 98, i16 0]
@exp7_0 = private constant [1 x i8] [i8 112]
@exp7_1 = private constant [2 x i8] [i8 97, i8 92]
@exp7_2 = private constant [1 x i8] [i8 98]
@cmd8 = private constant [8 x i16] [i16 112, i16 32, i16 92, i16 92, i16 92, i16 34, i16 120, i16 0]
@exp8_0 = private constant [1 x i8] [i8 112]
@exp8_1 = private constant [3 x i8] [i8 92, i8 34, i8 120]
@cmd9 = private constant [11 x i16] [i16 112, i16 32, i16 34, i16 97, i16 32, i16 98, i16 34, i16 99, i16 32, i16 100, i16 0]
@exp9_0 = private constant [1 x i8] [i8 112]
@exp9_1 = private constant [4 x i8] [i8 97, i8 32, i8 98, i8 99]
@exp9_2 = private constant [1 x i8] [i8 100]
@cmd10 = private constant [13 x i16] [i16 112, i16 32, i16 104, i16 233, i16 108, i16 108, i16 111, i16 32, i16 8364, i16 32, i16 55357, i16 56832, i16 0]
@exp10_0 = private constant [1 x i8] [i8 112]
@exp10_1 = private constant [6 x i8] [i8 104, i8 195, i8 169, i8 108, i8 108, i8 111]
@exp10_2 = private constant [3 x i8] [i8 226, i8 130, i8 172]
@exp10_3 = private constant [4 x i8] [i8 240, i8 159, i8 152, i8 128]
@cmd11 = private constant [6 x i16] [i16 34, i16 112, i16 32, i16 113, i16 34, i16 0]
@exp11_0 = private constant [3 x i8] [i8 112, i8 32, i8 113]
define internal i1 @same(ptr %a, ptr %b, i64 %n) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %body ]
  %done = icmp uge i64 %i, %n
  br i1 %done, label %yes, label %body
body:
  %pa = getelementptr inbounds i8, ptr %a, i64 %i
  %pb = getelementptr inbounds i8, ptr %b, i64 %i
  %ba = load i8, ptr %pa
  %bb = load i8, ptr %pb
  %eq = icmp eq i8 %ba, %bb
  %next = add i64 %i, 1
  br i1 %eq, label %loop, label %no
yes:
  ret i1 true
no:
  ret i1 false
}
define i32 @chibi_scalar() {
entry:
  %buf = alloca [256 x i8]
  %len0_0 = call i64 @chibi_command_argument(ptr @cmd0, i64 0, ptr %buf, i64 256)
  %ok0 = icmp eq i64 %len0_0, 8
  br i1 %ok0, label %pass0, label %fail0
fail0:
  ret i32 100
pass0:
  %same0_0 = call i1 @same(ptr %buf, ptr @exp0_0, i64 8)
  %ok1 = icmp eq i1 %same0_0, true
  br i1 %ok1, label %pass1, label %fail1
fail1:
  ret i32 100
pass1:
  %short0_0 = call i64 @chibi_command_argument(ptr @cmd0, i64 0, ptr %buf, i64 1)
  %ok2 = icmp eq i64 %short0_0, 8
  br i1 %ok2, label %pass2, label %fail2
fail2:
  ret i32 200
pass2:
  %past0 = call i64 @chibi_command_argument(ptr @cmd0, i64 1, ptr %buf, i64 256)
  %ok3 = icmp eq i64 %past0, -1
  br i1 %ok3, label %pass3, label %fail3
fail3:
  ret i32 300
pass3:
  %count0 = call i64 @chibi_command_count(ptr @cmd0)
  %ok4 = icmp eq i64 %count0, 1
  br i1 %ok4, label %pass4, label %fail4
fail4:
  ret i32 400
pass4:
  %len1_0 = call i64 @chibi_command_argument(ptr @cmd1, i64 0, ptr %buf, i64 256)
  %ok5 = icmp eq i64 %len1_0, 8
  br i1 %ok5, label %pass5, label %fail5
fail5:
  ret i32 101
pass5:
  %same1_0 = call i1 @same(ptr %buf, ptr @exp1_0, i64 8)
  %ok6 = icmp eq i1 %same1_0, true
  br i1 %ok6, label %pass6, label %fail6
fail6:
  ret i32 101
pass6:
  %short1_0 = call i64 @chibi_command_argument(ptr @cmd1, i64 0, ptr %buf, i64 1)
  %ok7 = icmp eq i64 %short1_0, 8
  br i1 %ok7, label %pass7, label %fail7
fail7:
  ret i32 201
pass7:
  %len1_1 = call i64 @chibi_command_argument(ptr @cmd1, i64 1, ptr %buf, i64 256)
  %ok8 = icmp eq i64 %len1_1, 1
  br i1 %ok8, label %pass8, label %fail8
fail8:
  ret i32 101
pass8:
  %same1_1 = call i1 @same(ptr %buf, ptr @exp1_1, i64 1)
  %ok9 = icmp eq i1 %same1_1, true
  br i1 %ok9, label %pass9, label %fail9
fail9:
  ret i32 101
pass9:
  %short1_1 = call i64 @chibi_command_argument(ptr @cmd1, i64 1, ptr %buf, i64 1)
  %ok10 = icmp eq i64 %short1_1, 1
  br i1 %ok10, label %pass10, label %fail10
fail10:
  ret i32 201
pass10:
  %len1_2 = call i64 @chibi_command_argument(ptr @cmd1, i64 2, ptr %buf, i64 256)
  %ok11 = icmp eq i64 %len1_2, 1
  br i1 %ok11, label %pass11, label %fail11
fail11:
  ret i32 101
pass11:
  %same1_2 = call i1 @same(ptr %buf, ptr @exp1_2, i64 1)
  %ok12 = icmp eq i1 %same1_2, true
  br i1 %ok12, label %pass12, label %fail12
fail12:
  ret i32 101
pass12:
  %short1_2 = call i64 @chibi_command_argument(ptr @cmd1, i64 2, ptr %buf, i64 1)
  %ok13 = icmp eq i64 %short1_2, 1
  br i1 %ok13, label %pass13, label %fail13
fail13:
  ret i32 201
pass13:
  %past1 = call i64 @chibi_command_argument(ptr @cmd1, i64 3, ptr %buf, i64 256)
  %ok14 = icmp eq i64 %past1, -1
  br i1 %ok14, label %pass14, label %fail14
fail14:
  ret i32 301
pass14:
  %count1 = call i64 @chibi_command_count(ptr @cmd1)
  %ok15 = icmp eq i64 %count1, 3
  br i1 %ok15, label %pass15, label %fail15
fail15:
  ret i32 401
pass15:
  %len2_0 = call i64 @chibi_command_argument(ptr @cmd2, i64 0, ptr %buf, i64 256)
  %ok16 = icmp eq i64 %len2_0, 22
  br i1 %ok16, label %pass16, label %fail16
fail16:
  ret i32 102
pass16:
  %same2_0 = call i1 @same(ptr %buf, ptr @exp2_0, i64 22)
  %ok17 = icmp eq i1 %same2_0, true
  br i1 %ok17, label %pass17, label %fail17
fail17:
  ret i32 102
pass17:
  %short2_0 = call i64 @chibi_command_argument(ptr @cmd2, i64 0, ptr %buf, i64 1)
  %ok18 = icmp eq i64 %short2_0, 22
  br i1 %ok18, label %pass18, label %fail18
fail18:
  ret i32 202
pass18:
  %len2_1 = call i64 @chibi_command_argument(ptr @cmd2, i64 1, ptr %buf, i64 256)
  %ok19 = icmp eq i64 %len2_1, 3
  br i1 %ok19, label %pass19, label %fail19
fail19:
  ret i32 102
pass19:
  %same2_1 = call i1 @same(ptr %buf, ptr @exp2_1, i64 3)
  %ok20 = icmp eq i1 %same2_1, true
  br i1 %ok20, label %pass20, label %fail20
fail20:
  ret i32 102
pass20:
  %short2_1 = call i64 @chibi_command_argument(ptr @cmd2, i64 1, ptr %buf, i64 1)
  %ok21 = icmp eq i64 %short2_1, 3
  br i1 %ok21, label %pass21, label %fail21
fail21:
  ret i32 202
pass21:
  %len2_2 = call i64 @chibi_command_argument(ptr @cmd2, i64 2, ptr %buf, i64 256)
  %ok22 = icmp eq i64 %len2_2, 1
  br i1 %ok22, label %pass22, label %fail22
fail22:
  ret i32 102
pass22:
  %same2_2 = call i1 @same(ptr %buf, ptr @exp2_2, i64 1)
  %ok23 = icmp eq i1 %same2_2, true
  br i1 %ok23, label %pass23, label %fail23
fail23:
  ret i32 102
pass23:
  %short2_2 = call i64 @chibi_command_argument(ptr @cmd2, i64 2, ptr %buf, i64 1)
  %ok24 = icmp eq i64 %short2_2, 1
  br i1 %ok24, label %pass24, label %fail24
fail24:
  ret i32 202
pass24:
  %past2 = call i64 @chibi_command_argument(ptr @cmd2, i64 3, ptr %buf, i64 256)
  %ok25 = icmp eq i64 %past2, -1
  br i1 %ok25, label %pass25, label %fail25
fail25:
  ret i32 302
pass25:
  %count2 = call i64 @chibi_command_count(ptr @cmd2)
  %ok26 = icmp eq i64 %count2, 3
  br i1 %ok26, label %pass26, label %fail26
fail26:
  ret i32 402
pass26:
  %len3_0 = call i64 @chibi_command_argument(ptr @cmd3, i64 0, ptr %buf, i64 256)
  %ok27 = icmp eq i64 %len3_0, 1
  br i1 %ok27, label %pass27, label %fail27
fail27:
  ret i32 103
pass27:
  %same3_0 = call i1 @same(ptr %buf, ptr @exp3_0, i64 1)
  %ok28 = icmp eq i1 %same3_0, true
  br i1 %ok28, label %pass28, label %fail28
fail28:
  ret i32 103
pass28:
  %short3_0 = call i64 @chibi_command_argument(ptr @cmd3, i64 0, ptr %buf, i64 1)
  %ok29 = icmp eq i64 %short3_0, 1
  br i1 %ok29, label %pass29, label %fail29
fail29:
  ret i32 203
pass29:
  %len3_1 = call i64 @chibi_command_argument(ptr @cmd3, i64 1, ptr %buf, i64 256)
  %ok30 = icmp eq i64 %len3_1, 1
  br i1 %ok30, label %pass30, label %fail30
fail30:
  ret i32 103
pass30:
  %same3_1 = call i1 @same(ptr %buf, ptr @exp3_1, i64 1)
  %ok31 = icmp eq i1 %same3_1, true
  br i1 %ok31, label %pass31, label %fail31
fail31:
  ret i32 103
pass31:
  %short3_1 = call i64 @chibi_command_argument(ptr @cmd3, i64 1, ptr %buf, i64 1)
  %ok32 = icmp eq i64 %short3_1, 1
  br i1 %ok32, label %pass32, label %fail32
fail32:
  ret i32 203
pass32:
  %len3_2 = call i64 @chibi_command_argument(ptr @cmd3, i64 2, ptr %buf, i64 256)
  %ok33 = icmp eq i64 %len3_2, 1
  br i1 %ok33, label %pass33, label %fail33
fail33:
  ret i32 103
pass33:
  %same3_2 = call i1 @same(ptr %buf, ptr @exp3_2, i64 1)
  %ok34 = icmp eq i1 %same3_2, true
  br i1 %ok34, label %pass34, label %fail34
fail34:
  ret i32 103
pass34:
  %short3_2 = call i64 @chibi_command_argument(ptr @cmd3, i64 2, ptr %buf, i64 1)
  %ok35 = icmp eq i64 %short3_2, 1
  br i1 %ok35, label %pass35, label %fail35
fail35:
  ret i32 203
pass35:
  %past3 = call i64 @chibi_command_argument(ptr @cmd3, i64 3, ptr %buf, i64 256)
  %ok36 = icmp eq i64 %past3, -1
  br i1 %ok36, label %pass36, label %fail36
fail36:
  ret i32 303
pass36:
  %count3 = call i64 @chibi_command_count(ptr @cmd3)
  %ok37 = icmp eq i64 %count3, 3
  br i1 %ok37, label %pass37, label %fail37
fail37:
  ret i32 403
pass37:
  %len4_0 = call i64 @chibi_command_argument(ptr @cmd4, i64 0, ptr %buf, i64 256)
  %ok38 = icmp eq i64 %len4_0, 1
  br i1 %ok38, label %pass38, label %fail38
fail38:
  ret i32 104
pass38:
  %same4_0 = call i1 @same(ptr %buf, ptr @exp4_0, i64 1)
  %ok39 = icmp eq i1 %same4_0, true
  br i1 %ok39, label %pass39, label %fail39
fail39:
  ret i32 104
pass39:
  %short4_0 = call i64 @chibi_command_argument(ptr @cmd4, i64 0, ptr %buf, i64 1)
  %ok40 = icmp eq i64 %short4_0, 1
  br i1 %ok40, label %pass40, label %fail40
fail40:
  ret i32 204
pass40:
  %len4_1 = call i64 @chibi_command_argument(ptr @cmd4, i64 1, ptr %buf, i64 256)
  %ok41 = icmp eq i64 %len4_1, 0
  br i1 %ok41, label %pass41, label %fail41
fail41:
  ret i32 104
pass41:
  %same4_1 = call i1 @same(ptr %buf, ptr @exp4_1, i64 0)
  %ok42 = icmp eq i1 %same4_1, true
  br i1 %ok42, label %pass42, label %fail42
fail42:
  ret i32 104
pass42:
  %short4_1 = call i64 @chibi_command_argument(ptr @cmd4, i64 1, ptr %buf, i64 1)
  %ok43 = icmp eq i64 %short4_1, 0
  br i1 %ok43, label %pass43, label %fail43
fail43:
  ret i32 204
pass43:
  %len4_2 = call i64 @chibi_command_argument(ptr @cmd4, i64 2, ptr %buf, i64 256)
  %ok44 = icmp eq i64 %len4_2, 1
  br i1 %ok44, label %pass44, label %fail44
fail44:
  ret i32 104
pass44:
  %same4_2 = call i1 @same(ptr %buf, ptr @exp4_2, i64 1)
  %ok45 = icmp eq i1 %same4_2, true
  br i1 %ok45, label %pass45, label %fail45
fail45:
  ret i32 104
pass45:
  %short4_2 = call i64 @chibi_command_argument(ptr @cmd4, i64 2, ptr %buf, i64 1)
  %ok46 = icmp eq i64 %short4_2, 1
  br i1 %ok46, label %pass46, label %fail46
fail46:
  ret i32 204
pass46:
  %past4 = call i64 @chibi_command_argument(ptr @cmd4, i64 3, ptr %buf, i64 256)
  %ok47 = icmp eq i64 %past4, -1
  br i1 %ok47, label %pass47, label %fail47
fail47:
  ret i32 304
pass47:
  %count4 = call i64 @chibi_command_count(ptr @cmd4)
  %ok48 = icmp eq i64 %count4, 3
  br i1 %ok48, label %pass48, label %fail48
fail48:
  ret i32 404
pass48:
  %len5_0 = call i64 @chibi_command_argument(ptr @cmd5, i64 0, ptr %buf, i64 256)
  %ok49 = icmp eq i64 %len5_0, 1
  br i1 %ok49, label %pass49, label %fail49
fail49:
  ret i32 105
pass49:
  %same5_0 = call i1 @same(ptr %buf, ptr @exp5_0, i64 1)
  %ok50 = icmp eq i1 %same5_0, true
  br i1 %ok50, label %pass50, label %fail50
fail50:
  ret i32 105
pass50:
  %short5_0 = call i64 @chibi_command_argument(ptr @cmd5, i64 0, ptr %buf, i64 1)
  %ok51 = icmp eq i64 %short5_0, 1
  br i1 %ok51, label %pass51, label %fail51
fail51:
  ret i32 205
pass51:
  %len5_1 = call i64 @chibi_command_argument(ptr @cmd5, i64 1, ptr %buf, i64 256)
  %ok52 = icmp eq i64 %len5_1, 3
  br i1 %ok52, label %pass52, label %fail52
fail52:
  ret i32 105
pass52:
  %same5_1 = call i1 @same(ptr %buf, ptr @exp5_1, i64 3)
  %ok53 = icmp eq i1 %same5_1, true
  br i1 %ok53, label %pass53, label %fail53
fail53:
  ret i32 105
pass53:
  %short5_1 = call i64 @chibi_command_argument(ptr @cmd5, i64 1, ptr %buf, i64 1)
  %ok54 = icmp eq i64 %short5_1, 3
  br i1 %ok54, label %pass54, label %fail54
fail54:
  ret i32 205
pass54:
  %len5_2 = call i64 @chibi_command_argument(ptr @cmd5, i64 2, ptr %buf, i64 256)
  %ok55 = icmp eq i64 %len5_2, 4
  br i1 %ok55, label %pass55, label %fail55
fail55:
  ret i32 105
pass55:
  %same5_2 = call i1 @same(ptr %buf, ptr @exp5_2, i64 4)
  %ok56 = icmp eq i1 %same5_2, true
  br i1 %ok56, label %pass56, label %fail56
fail56:
  ret i32 105
pass56:
  %short5_2 = call i64 @chibi_command_argument(ptr @cmd5, i64 2, ptr %buf, i64 1)
  %ok57 = icmp eq i64 %short5_2, 4
  br i1 %ok57, label %pass57, label %fail57
fail57:
  ret i32 205
pass57:
  %past5 = call i64 @chibi_command_argument(ptr @cmd5, i64 3, ptr %buf, i64 256)
  %ok58 = icmp eq i64 %past5, -1
  br i1 %ok58, label %pass58, label %fail58
fail58:
  ret i32 305
pass58:
  %count5 = call i64 @chibi_command_count(ptr @cmd5)
  %ok59 = icmp eq i64 %count5, 3
  br i1 %ok59, label %pass59, label %fail59
fail59:
  ret i32 405
pass59:
  %len6_0 = call i64 @chibi_command_argument(ptr @cmd6, i64 0, ptr %buf, i64 256)
  %ok60 = icmp eq i64 %len6_0, 1
  br i1 %ok60, label %pass60, label %fail60
fail60:
  ret i32 106
pass60:
  %same6_0 = call i1 @same(ptr %buf, ptr @exp6_0, i64 1)
  %ok61 = icmp eq i1 %same6_0, true
  br i1 %ok61, label %pass61, label %fail61
fail61:
  ret i32 106
pass61:
  %short6_0 = call i64 @chibi_command_argument(ptr @cmd6, i64 0, ptr %buf, i64 1)
  %ok62 = icmp eq i64 %short6_0, 1
  br i1 %ok62, label %pass62, label %fail62
fail62:
  ret i32 206
pass62:
  %len6_1 = call i64 @chibi_command_argument(ptr @cmd6, i64 1, ptr %buf, i64 256)
  %ok63 = icmp eq i64 %len6_1, 3
  br i1 %ok63, label %pass63, label %fail63
fail63:
  ret i32 106
pass63:
  %same6_1 = call i1 @same(ptr %buf, ptr @exp6_1, i64 3)
  %ok64 = icmp eq i1 %same6_1, true
  br i1 %ok64, label %pass64, label %fail64
fail64:
  ret i32 106
pass64:
  %short6_1 = call i64 @chibi_command_argument(ptr @cmd6, i64 1, ptr %buf, i64 1)
  %ok65 = icmp eq i64 %short6_1, 3
  br i1 %ok65, label %pass65, label %fail65
fail65:
  ret i32 206
pass65:
  %len6_2 = call i64 @chibi_command_argument(ptr @cmd6, i64 2, ptr %buf, i64 256)
  %ok66 = icmp eq i64 %len6_2, 1
  br i1 %ok66, label %pass66, label %fail66
fail66:
  ret i32 106
pass66:
  %same6_2 = call i1 @same(ptr %buf, ptr @exp6_2, i64 1)
  %ok67 = icmp eq i1 %same6_2, true
  br i1 %ok67, label %pass67, label %fail67
fail67:
  ret i32 106
pass67:
  %short6_2 = call i64 @chibi_command_argument(ptr @cmd6, i64 2, ptr %buf, i64 1)
  %ok68 = icmp eq i64 %short6_2, 1
  br i1 %ok68, label %pass68, label %fail68
fail68:
  ret i32 206
pass68:
  %past6 = call i64 @chibi_command_argument(ptr @cmd6, i64 3, ptr %buf, i64 256)
  %ok69 = icmp eq i64 %past6, -1
  br i1 %ok69, label %pass69, label %fail69
fail69:
  ret i32 306
pass69:
  %count6 = call i64 @chibi_command_count(ptr @cmd6)
  %ok70 = icmp eq i64 %count6, 3
  br i1 %ok70, label %pass70, label %fail70
fail70:
  ret i32 406
pass70:
  %len7_0 = call i64 @chibi_command_argument(ptr @cmd7, i64 0, ptr %buf, i64 256)
  %ok71 = icmp eq i64 %len7_0, 1
  br i1 %ok71, label %pass71, label %fail71
fail71:
  ret i32 107
pass71:
  %same7_0 = call i1 @same(ptr %buf, ptr @exp7_0, i64 1)
  %ok72 = icmp eq i1 %same7_0, true
  br i1 %ok72, label %pass72, label %fail72
fail72:
  ret i32 107
pass72:
  %short7_0 = call i64 @chibi_command_argument(ptr @cmd7, i64 0, ptr %buf, i64 1)
  %ok73 = icmp eq i64 %short7_0, 1
  br i1 %ok73, label %pass73, label %fail73
fail73:
  ret i32 207
pass73:
  %len7_1 = call i64 @chibi_command_argument(ptr @cmd7, i64 1, ptr %buf, i64 256)
  %ok74 = icmp eq i64 %len7_1, 2
  br i1 %ok74, label %pass74, label %fail74
fail74:
  ret i32 107
pass74:
  %same7_1 = call i1 @same(ptr %buf, ptr @exp7_1, i64 2)
  %ok75 = icmp eq i1 %same7_1, true
  br i1 %ok75, label %pass75, label %fail75
fail75:
  ret i32 107
pass75:
  %short7_1 = call i64 @chibi_command_argument(ptr @cmd7, i64 1, ptr %buf, i64 1)
  %ok76 = icmp eq i64 %short7_1, 2
  br i1 %ok76, label %pass76, label %fail76
fail76:
  ret i32 207
pass76:
  %len7_2 = call i64 @chibi_command_argument(ptr @cmd7, i64 2, ptr %buf, i64 256)
  %ok77 = icmp eq i64 %len7_2, 1
  br i1 %ok77, label %pass77, label %fail77
fail77:
  ret i32 107
pass77:
  %same7_2 = call i1 @same(ptr %buf, ptr @exp7_2, i64 1)
  %ok78 = icmp eq i1 %same7_2, true
  br i1 %ok78, label %pass78, label %fail78
fail78:
  ret i32 107
pass78:
  %short7_2 = call i64 @chibi_command_argument(ptr @cmd7, i64 2, ptr %buf, i64 1)
  %ok79 = icmp eq i64 %short7_2, 1
  br i1 %ok79, label %pass79, label %fail79
fail79:
  ret i32 207
pass79:
  %past7 = call i64 @chibi_command_argument(ptr @cmd7, i64 3, ptr %buf, i64 256)
  %ok80 = icmp eq i64 %past7, -1
  br i1 %ok80, label %pass80, label %fail80
fail80:
  ret i32 307
pass80:
  %count7 = call i64 @chibi_command_count(ptr @cmd7)
  %ok81 = icmp eq i64 %count7, 3
  br i1 %ok81, label %pass81, label %fail81
fail81:
  ret i32 407
pass81:
  %len8_0 = call i64 @chibi_command_argument(ptr @cmd8, i64 0, ptr %buf, i64 256)
  %ok82 = icmp eq i64 %len8_0, 1
  br i1 %ok82, label %pass82, label %fail82
fail82:
  ret i32 108
pass82:
  %same8_0 = call i1 @same(ptr %buf, ptr @exp8_0, i64 1)
  %ok83 = icmp eq i1 %same8_0, true
  br i1 %ok83, label %pass83, label %fail83
fail83:
  ret i32 108
pass83:
  %short8_0 = call i64 @chibi_command_argument(ptr @cmd8, i64 0, ptr %buf, i64 1)
  %ok84 = icmp eq i64 %short8_0, 1
  br i1 %ok84, label %pass84, label %fail84
fail84:
  ret i32 208
pass84:
  %len8_1 = call i64 @chibi_command_argument(ptr @cmd8, i64 1, ptr %buf, i64 256)
  %ok85 = icmp eq i64 %len8_1, 3
  br i1 %ok85, label %pass85, label %fail85
fail85:
  ret i32 108
pass85:
  %same8_1 = call i1 @same(ptr %buf, ptr @exp8_1, i64 3)
  %ok86 = icmp eq i1 %same8_1, true
  br i1 %ok86, label %pass86, label %fail86
fail86:
  ret i32 108
pass86:
  %short8_1 = call i64 @chibi_command_argument(ptr @cmd8, i64 1, ptr %buf, i64 1)
  %ok87 = icmp eq i64 %short8_1, 3
  br i1 %ok87, label %pass87, label %fail87
fail87:
  ret i32 208
pass87:
  %past8 = call i64 @chibi_command_argument(ptr @cmd8, i64 2, ptr %buf, i64 256)
  %ok88 = icmp eq i64 %past8, -1
  br i1 %ok88, label %pass88, label %fail88
fail88:
  ret i32 308
pass88:
  %count8 = call i64 @chibi_command_count(ptr @cmd8)
  %ok89 = icmp eq i64 %count8, 2
  br i1 %ok89, label %pass89, label %fail89
fail89:
  ret i32 408
pass89:
  %len9_0 = call i64 @chibi_command_argument(ptr @cmd9, i64 0, ptr %buf, i64 256)
  %ok90 = icmp eq i64 %len9_0, 1
  br i1 %ok90, label %pass90, label %fail90
fail90:
  ret i32 109
pass90:
  %same9_0 = call i1 @same(ptr %buf, ptr @exp9_0, i64 1)
  %ok91 = icmp eq i1 %same9_0, true
  br i1 %ok91, label %pass91, label %fail91
fail91:
  ret i32 109
pass91:
  %short9_0 = call i64 @chibi_command_argument(ptr @cmd9, i64 0, ptr %buf, i64 1)
  %ok92 = icmp eq i64 %short9_0, 1
  br i1 %ok92, label %pass92, label %fail92
fail92:
  ret i32 209
pass92:
  %len9_1 = call i64 @chibi_command_argument(ptr @cmd9, i64 1, ptr %buf, i64 256)
  %ok93 = icmp eq i64 %len9_1, 4
  br i1 %ok93, label %pass93, label %fail93
fail93:
  ret i32 109
pass93:
  %same9_1 = call i1 @same(ptr %buf, ptr @exp9_1, i64 4)
  %ok94 = icmp eq i1 %same9_1, true
  br i1 %ok94, label %pass94, label %fail94
fail94:
  ret i32 109
pass94:
  %short9_1 = call i64 @chibi_command_argument(ptr @cmd9, i64 1, ptr %buf, i64 1)
  %ok95 = icmp eq i64 %short9_1, 4
  br i1 %ok95, label %pass95, label %fail95
fail95:
  ret i32 209
pass95:
  %len9_2 = call i64 @chibi_command_argument(ptr @cmd9, i64 2, ptr %buf, i64 256)
  %ok96 = icmp eq i64 %len9_2, 1
  br i1 %ok96, label %pass96, label %fail96
fail96:
  ret i32 109
pass96:
  %same9_2 = call i1 @same(ptr %buf, ptr @exp9_2, i64 1)
  %ok97 = icmp eq i1 %same9_2, true
  br i1 %ok97, label %pass97, label %fail97
fail97:
  ret i32 109
pass97:
  %short9_2 = call i64 @chibi_command_argument(ptr @cmd9, i64 2, ptr %buf, i64 1)
  %ok98 = icmp eq i64 %short9_2, 1
  br i1 %ok98, label %pass98, label %fail98
fail98:
  ret i32 209
pass98:
  %past9 = call i64 @chibi_command_argument(ptr @cmd9, i64 3, ptr %buf, i64 256)
  %ok99 = icmp eq i64 %past9, -1
  br i1 %ok99, label %pass99, label %fail99
fail99:
  ret i32 309
pass99:
  %count9 = call i64 @chibi_command_count(ptr @cmd9)
  %ok100 = icmp eq i64 %count9, 3
  br i1 %ok100, label %pass100, label %fail100
fail100:
  ret i32 409
pass100:
  %len10_0 = call i64 @chibi_command_argument(ptr @cmd10, i64 0, ptr %buf, i64 256)
  %ok101 = icmp eq i64 %len10_0, 1
  br i1 %ok101, label %pass101, label %fail101
fail101:
  ret i32 110
pass101:
  %same10_0 = call i1 @same(ptr %buf, ptr @exp10_0, i64 1)
  %ok102 = icmp eq i1 %same10_0, true
  br i1 %ok102, label %pass102, label %fail102
fail102:
  ret i32 110
pass102:
  %short10_0 = call i64 @chibi_command_argument(ptr @cmd10, i64 0, ptr %buf, i64 1)
  %ok103 = icmp eq i64 %short10_0, 1
  br i1 %ok103, label %pass103, label %fail103
fail103:
  ret i32 210
pass103:
  %len10_1 = call i64 @chibi_command_argument(ptr @cmd10, i64 1, ptr %buf, i64 256)
  %ok104 = icmp eq i64 %len10_1, 6
  br i1 %ok104, label %pass104, label %fail104
fail104:
  ret i32 110
pass104:
  %same10_1 = call i1 @same(ptr %buf, ptr @exp10_1, i64 6)
  %ok105 = icmp eq i1 %same10_1, true
  br i1 %ok105, label %pass105, label %fail105
fail105:
  ret i32 110
pass105:
  %short10_1 = call i64 @chibi_command_argument(ptr @cmd10, i64 1, ptr %buf, i64 1)
  %ok106 = icmp eq i64 %short10_1, 6
  br i1 %ok106, label %pass106, label %fail106
fail106:
  ret i32 210
pass106:
  %len10_2 = call i64 @chibi_command_argument(ptr @cmd10, i64 2, ptr %buf, i64 256)
  %ok107 = icmp eq i64 %len10_2, 3
  br i1 %ok107, label %pass107, label %fail107
fail107:
  ret i32 110
pass107:
  %same10_2 = call i1 @same(ptr %buf, ptr @exp10_2, i64 3)
  %ok108 = icmp eq i1 %same10_2, true
  br i1 %ok108, label %pass108, label %fail108
fail108:
  ret i32 110
pass108:
  %short10_2 = call i64 @chibi_command_argument(ptr @cmd10, i64 2, ptr %buf, i64 1)
  %ok109 = icmp eq i64 %short10_2, 3
  br i1 %ok109, label %pass109, label %fail109
fail109:
  ret i32 210
pass109:
  %len10_3 = call i64 @chibi_command_argument(ptr @cmd10, i64 3, ptr %buf, i64 256)
  %ok110 = icmp eq i64 %len10_3, 4
  br i1 %ok110, label %pass110, label %fail110
fail110:
  ret i32 110
pass110:
  %same10_3 = call i1 @same(ptr %buf, ptr @exp10_3, i64 4)
  %ok111 = icmp eq i1 %same10_3, true
  br i1 %ok111, label %pass111, label %fail111
fail111:
  ret i32 110
pass111:
  %short10_3 = call i64 @chibi_command_argument(ptr @cmd10, i64 3, ptr %buf, i64 1)
  %ok112 = icmp eq i64 %short10_3, 4
  br i1 %ok112, label %pass112, label %fail112
fail112:
  ret i32 210
pass112:
  %past10 = call i64 @chibi_command_argument(ptr @cmd10, i64 4, ptr %buf, i64 256)
  %ok113 = icmp eq i64 %past10, -1
  br i1 %ok113, label %pass113, label %fail113
fail113:
  ret i32 310
pass113:
  %count10 = call i64 @chibi_command_count(ptr @cmd10)
  %ok114 = icmp eq i64 %count10, 4
  br i1 %ok114, label %pass114, label %fail114
fail114:
  ret i32 410
pass114:
  %len11_0 = call i64 @chibi_command_argument(ptr @cmd11, i64 0, ptr %buf, i64 256)
  %ok115 = icmp eq i64 %len11_0, 3
  br i1 %ok115, label %pass115, label %fail115
fail115:
  ret i32 111
pass115:
  %same11_0 = call i1 @same(ptr %buf, ptr @exp11_0, i64 3)
  %ok116 = icmp eq i1 %same11_0, true
  br i1 %ok116, label %pass116, label %fail116
fail116:
  ret i32 111
pass116:
  %short11_0 = call i64 @chibi_command_argument(ptr @cmd11, i64 0, ptr %buf, i64 1)
  %ok117 = icmp eq i64 %short11_0, 3
  br i1 %ok117, label %pass117, label %fail117
fail117:
  ret i32 211
pass117:
  %past11 = call i64 @chibi_command_argument(ptr @cmd11, i64 1, ptr %buf, i64 256)
  %ok118 = icmp eq i64 %past11, -1
  br i1 %ok118, label %pass118, label %fail118
fail118:
  ret i32 311
pass118:
  %count11 = call i64 @chibi_command_count(ptr @cmd11)
  %ok119 = icmp eq i64 %count11, 1
  br i1 %ok119, label %pass119, label %fail119
fail119:
  ret i32 411
pass119:
  ret i32 42
}
