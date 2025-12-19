; RUN: opt -passes=loop-vectorize -mtriple=thumbv7s-apple-ios6.0.0 -S -enable-interleaved-mem-accesses=false < %s | FileCheck %s

target datalayout = "e-p:32:32:32-i1:8:32-i8:8:32-i16:16:32-i32:32:32-i64:32:64-f32:32:32-f64:32:64-v64:32:64-v128:32:128-a0:0:32-n32-S32"

@kernel = global [512 x float] zeroinitializer, align 4
@kernel2 = global [512 x float] zeroinitializer, align 4
@kernel3 = global [512 x float] zeroinitializer, align 4
@kernel4 = global [512 x float] zeroinitializer, align 4
@src_data = global [1536 x float] zeroinitializer, align 4
@r_ = global i8 0, align 4
@g_ = global i8 0, align 4
@b_ = global i8 0, align 4

<<<<<<< HEAD
; We don't want to vectorize most loops containing gathers because they are
; expensive. This function represents a point where vectorization starts to
; become beneficial.
; Make sure we are conservative and don't vectorize it.
; CHECK-NOT: <2 x float>
; CHECK-NOT: <4 x float>

define void @_Z4testmm(i32 %size, i32 %offset) {
=======
; The cost of gathers in the loop gets offset by the vector math.

define float @_Z4testmm(i64 %size, i64 %offset) {
; CHECK-LABEL: define float @_Z4testmm(
; CHECK-SAME: i64 [[SIZE:%.*]], i64 [[OFFSET:%.*]]) {
; CHECK-NEXT:  [[ENTRY:.*]]:
; CHECK-NEXT:    [[MIN_ITERS_CHECK:%.*]] = icmp ult i64 [[SIZE]], 4
; CHECK-NEXT:    br i1 [[MIN_ITERS_CHECK]], label %[[SCALAR_PH:.*]], label %[[VECTOR_PH:.*]]
; CHECK:       [[VECTOR_PH]]:
; CHECK-NEXT:    [[N_MOD_VF:%.*]] = urem i64 [[SIZE]], 4
; CHECK-NEXT:    [[N_VEC:%.*]] = sub i64 [[SIZE]], [[N_MOD_VF]]
; CHECK-NEXT:    [[BROADCAST_SPLATINSERT:%.*]] = insertelement <4 x i64> poison, i64 [[OFFSET]], i64 0
; CHECK-NEXT:    [[BROADCAST_SPLAT:%.*]] = shufflevector <4 x i64> [[BROADCAST_SPLATINSERT]], <4 x i64> poison, <4 x i32> zeroinitializer
; CHECK-NEXT:    br label %[[VECTOR_BODY:.*]]
; CHECK:       [[VECTOR_BODY]]:
; CHECK-NEXT:    [[INDEX:%.*]] = phi i64 [ 0, %[[VECTOR_PH]] ], [ [[INDEX_NEXT:%.*]], %[[VECTOR_BODY]] ]
; CHECK-NEXT:    [[VEC_IND:%.*]] = phi <4 x i64> [ <i64 0, i64 1, i64 2, i64 3>, %[[VECTOR_PH]] ], [ [[VEC_IND_NEXT:%.*]], %[[VECTOR_BODY]] ]
; CHECK-NEXT:    [[VEC_PHI:%.*]] = phi <4 x float> [ zeroinitializer, %[[VECTOR_PH]] ], [ [[TMP26:%.*]], %[[VECTOR_BODY]] ]
; CHECK-NEXT:    [[VEC_PHI1:%.*]] = phi <4 x float> [ zeroinitializer, %[[VECTOR_PH]] ], [ [[TMP48:%.*]], %[[VECTOR_BODY]] ]
; CHECK-NEXT:    [[VEC_PHI2:%.*]] = phi <4 x float> [ zeroinitializer, %[[VECTOR_PH]] ], [ [[TMP70:%.*]], %[[VECTOR_BODY]] ]
; CHECK-NEXT:    [[TMP75:%.*]] = add <4 x i64> [[VEC_IND]], [[BROADCAST_SPLAT]]
; CHECK-NEXT:    [[TMP76:%.*]] = mul <4 x i64> [[TMP75]], splat (i64 3)
; CHECK-NEXT:    [[TMP77:%.*]] = extractelement <4 x i64> [[TMP76]], i32 0
; CHECK-NEXT:    [[TMP78:%.*]] = extractelement <4 x i64> [[TMP76]], i32 1
; CHECK-NEXT:    [[TMP79:%.*]] = extractelement <4 x i64> [[TMP76]], i32 2
; CHECK-NEXT:    [[TMP80:%.*]] = extractelement <4 x i64> [[TMP76]], i32 3
; CHECK-NEXT:    [[TMP81:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP77]]
; CHECK-NEXT:    [[TMP7:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP78]]
; CHECK-NEXT:    [[TMP8:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP79]]
; CHECK-NEXT:    [[TMP9:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP80]]
; CHECK-NEXT:    [[TMP10:%.*]] = load float, ptr [[TMP81]], align 4
; CHECK-NEXT:    [[TMP11:%.*]] = load float, ptr [[TMP7]], align 4
; CHECK-NEXT:    [[TMP12:%.*]] = load float, ptr [[TMP8]], align 4
; CHECK-NEXT:    [[TMP13:%.*]] = load float, ptr [[TMP9]], align 4
; CHECK-NEXT:    [[TMP14:%.*]] = insertelement <4 x float> poison, float [[TMP10]], i32 0
; CHECK-NEXT:    [[TMP15:%.*]] = insertelement <4 x float> [[TMP14]], float [[TMP11]], i32 1
; CHECK-NEXT:    [[TMP16:%.*]] = insertelement <4 x float> [[TMP15]], float [[TMP12]], i32 2
; CHECK-NEXT:    [[TMP17:%.*]] = insertelement <4 x float> [[TMP16]], float [[TMP13]], i32 3
; CHECK-NEXT:    [[TMP18:%.*]] = getelementptr inbounds [512 x float], ptr @kernel, i64 0, i64 [[INDEX]]
; CHECK-NEXT:    [[WIDE_LOAD:%.*]] = load <4 x float>, ptr [[TMP18]], align 4
; CHECK-NEXT:    [[TMP19:%.*]] = fmul fast <4 x float> [[TMP17]], [[WIDE_LOAD]]
; CHECK-NEXT:    [[TMP20:%.*]] = getelementptr inbounds [512 x float], ptr @kernel2, i64 0, i64 [[INDEX]]
; CHECK-NEXT:    [[WIDE_LOAD3:%.*]] = load <4 x float>, ptr [[TMP20]], align 4
; CHECK-NEXT:    [[TMP21:%.*]] = fmul fast <4 x float> [[TMP19]], [[WIDE_LOAD3]]
; CHECK-NEXT:    [[TMP22:%.*]] = getelementptr inbounds [512 x float], ptr @kernel3, i64 0, i64 [[INDEX]]
; CHECK-NEXT:    [[WIDE_LOAD4:%.*]] = load <4 x float>, ptr [[TMP22]], align 4
; CHECK-NEXT:    [[TMP23:%.*]] = fmul fast <4 x float> [[TMP21]], [[WIDE_LOAD4]]
; CHECK-NEXT:    [[TMP24:%.*]] = getelementptr inbounds [512 x float], ptr @kernel4, i64 0, i64 [[INDEX]]
; CHECK-NEXT:    [[WIDE_LOAD5:%.*]] = load <4 x float>, ptr [[TMP24]], align 4
; CHECK-NEXT:    [[TMP25:%.*]] = fmul fast <4 x float> [[TMP23]], [[WIDE_LOAD5]]
; CHECK-NEXT:    [[TMP26]] = fadd fast <4 x float> [[VEC_PHI]], [[TMP25]]
; CHECK-NEXT:    [[TMP27:%.*]] = add <4 x i64> [[TMP76]], splat (i64 1)
; CHECK-NEXT:    [[TMP28:%.*]] = extractelement <4 x i64> [[TMP27]], i32 0
; CHECK-NEXT:    [[TMP29:%.*]] = extractelement <4 x i64> [[TMP27]], i32 1
; CHECK-NEXT:    [[TMP30:%.*]] = extractelement <4 x i64> [[TMP27]], i32 2
; CHECK-NEXT:    [[TMP31:%.*]] = extractelement <4 x i64> [[TMP27]], i32 3
; CHECK-NEXT:    [[TMP32:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP28]]
; CHECK-NEXT:    [[TMP33:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP29]]
; CHECK-NEXT:    [[TMP34:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP30]]
; CHECK-NEXT:    [[TMP35:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP31]]
; CHECK-NEXT:    [[TMP36:%.*]] = load float, ptr [[TMP32]], align 4
; CHECK-NEXT:    [[TMP37:%.*]] = load float, ptr [[TMP33]], align 4
; CHECK-NEXT:    [[TMP38:%.*]] = load float, ptr [[TMP34]], align 4
; CHECK-NEXT:    [[TMP39:%.*]] = load float, ptr [[TMP35]], align 4
; CHECK-NEXT:    [[TMP40:%.*]] = insertelement <4 x float> poison, float [[TMP36]], i32 0
; CHECK-NEXT:    [[TMP41:%.*]] = insertelement <4 x float> [[TMP40]], float [[TMP37]], i32 1
; CHECK-NEXT:    [[TMP42:%.*]] = insertelement <4 x float> [[TMP41]], float [[TMP38]], i32 2
; CHECK-NEXT:    [[TMP43:%.*]] = insertelement <4 x float> [[TMP42]], float [[TMP39]], i32 3
; CHECK-NEXT:    [[TMP44:%.*]] = fmul fast <4 x float> [[WIDE_LOAD]], [[TMP43]]
; CHECK-NEXT:    [[TMP45:%.*]] = fmul fast <4 x float> [[WIDE_LOAD3]], [[TMP44]]
; CHECK-NEXT:    [[TMP46:%.*]] = fmul fast <4 x float> [[WIDE_LOAD4]], [[TMP45]]
; CHECK-NEXT:    [[TMP47:%.*]] = fmul fast <4 x float> [[WIDE_LOAD5]], [[TMP46]]
; CHECK-NEXT:    [[TMP48]] = fadd fast <4 x float> [[VEC_PHI1]], [[TMP47]]
; CHECK-NEXT:    [[TMP49:%.*]] = add <4 x i64> [[TMP76]], splat (i64 2)
; CHECK-NEXT:    [[TMP50:%.*]] = extractelement <4 x i64> [[TMP49]], i32 0
; CHECK-NEXT:    [[TMP51:%.*]] = extractelement <4 x i64> [[TMP49]], i32 1
; CHECK-NEXT:    [[TMP52:%.*]] = extractelement <4 x i64> [[TMP49]], i32 2
; CHECK-NEXT:    [[TMP53:%.*]] = extractelement <4 x i64> [[TMP49]], i32 3
; CHECK-NEXT:    [[TMP54:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP50]]
; CHECK-NEXT:    [[TMP55:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP51]]
; CHECK-NEXT:    [[TMP56:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP52]]
; CHECK-NEXT:    [[TMP57:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[TMP53]]
; CHECK-NEXT:    [[TMP58:%.*]] = load float, ptr [[TMP54]], align 4
; CHECK-NEXT:    [[TMP59:%.*]] = load float, ptr [[TMP55]], align 4
; CHECK-NEXT:    [[TMP60:%.*]] = load float, ptr [[TMP56]], align 4
; CHECK-NEXT:    [[TMP61:%.*]] = load float, ptr [[TMP57]], align 4
; CHECK-NEXT:    [[TMP62:%.*]] = insertelement <4 x float> poison, float [[TMP58]], i32 0
; CHECK-NEXT:    [[TMP63:%.*]] = insertelement <4 x float> [[TMP62]], float [[TMP59]], i32 1
; CHECK-NEXT:    [[TMP64:%.*]] = insertelement <4 x float> [[TMP63]], float [[TMP60]], i32 2
; CHECK-NEXT:    [[TMP65:%.*]] = insertelement <4 x float> [[TMP64]], float [[TMP61]], i32 3
; CHECK-NEXT:    [[TMP66:%.*]] = fmul fast <4 x float> [[WIDE_LOAD]], [[TMP65]]
; CHECK-NEXT:    [[TMP67:%.*]] = fmul fast <4 x float> [[WIDE_LOAD3]], [[TMP66]]
; CHECK-NEXT:    [[TMP68:%.*]] = fmul fast <4 x float> [[WIDE_LOAD4]], [[TMP67]]
; CHECK-NEXT:    [[TMP69:%.*]] = fmul fast <4 x float> [[WIDE_LOAD5]], [[TMP68]]
; CHECK-NEXT:    [[TMP70]] = fadd fast <4 x float> [[VEC_PHI2]], [[TMP69]]
; CHECK-NEXT:    [[INDEX_NEXT]] = add nuw i64 [[INDEX]], 4
; CHECK-NEXT:    [[VEC_IND_NEXT]] = add <4 x i64> [[VEC_IND]], splat (i64 4)
; CHECK-NEXT:    [[TMP71:%.*]] = icmp eq i64 [[INDEX_NEXT]], [[N_VEC]]
; CHECK-NEXT:    br i1 [[TMP71]], label %[[MIDDLE_BLOCK:.*]], label %[[VECTOR_BODY]], !llvm.loop [[LOOP0:![0-9]+]]
; CHECK:       [[MIDDLE_BLOCK]]:
; CHECK-NEXT:    [[TMP72:%.*]] = call fast float @llvm.vector.reduce.fadd.v4f32(float 0.000000e+00, <4 x float> [[TMP26]])
; CHECK-NEXT:    [[TMP73:%.*]] = call fast float @llvm.vector.reduce.fadd.v4f32(float 0.000000e+00, <4 x float> [[TMP48]])
; CHECK-NEXT:    [[TMP74:%.*]] = call fast float @llvm.vector.reduce.fadd.v4f32(float 0.000000e+00, <4 x float> [[TMP70]])
; CHECK-NEXT:    [[CMP_N:%.*]] = icmp eq i64 [[SIZE]], [[N_VEC]]
; CHECK-NEXT:    br i1 [[CMP_N]], label %[[EXIT:.*]], label %[[SCALAR_PH]]
; CHECK:       [[SCALAR_PH]]:
; CHECK-NEXT:    [[BC_RESUME_VAL:%.*]] = phi i64 [ [[N_VEC]], %[[MIDDLE_BLOCK]] ], [ 0, %[[ENTRY]] ]
; CHECK-NEXT:    [[BC_MERGE_RDX:%.*]] = phi float [ [[TMP72]], %[[MIDDLE_BLOCK]] ], [ 0.000000e+00, %[[ENTRY]] ]
; CHECK-NEXT:    [[BC_MERGE_RDX6:%.*]] = phi float [ [[TMP73]], %[[MIDDLE_BLOCK]] ], [ 0.000000e+00, %[[ENTRY]] ]
; CHECK-NEXT:    [[BC_MERGE_RDX7:%.*]] = phi float [ [[TMP74]], %[[MIDDLE_BLOCK]] ], [ 0.000000e+00, %[[ENTRY]] ]
; CHECK-NEXT:    br label %[[LOOP:.*]]
; CHECK:       [[LOOP]]:
; CHECK-NEXT:    [[IV:%.*]] = phi i64 [ [[BC_RESUME_VAL]], %[[SCALAR_PH]] ], [ [[IV_NEXT:%.*]], %[[LOOP]] ]
; CHECK-NEXT:    [[RDX_0:%.*]] = phi float [ [[BC_MERGE_RDX]], %[[SCALAR_PH]] ], [ [[RDX_0_NEXT:%.*]], %[[LOOP]] ]
; CHECK-NEXT:    [[RDX_1:%.*]] = phi float [ [[BC_MERGE_RDX6]], %[[SCALAR_PH]] ], [ [[RDX_1_NEXT:%.*]], %[[LOOP]] ]
; CHECK-NEXT:    [[RED_2:%.*]] = phi float [ [[BC_MERGE_RDX7]], %[[SCALAR_PH]] ], [ [[RDX_2_NEXT:%.*]], %[[LOOP]] ]
; CHECK-NEXT:    [[ADD:%.*]] = add i64 [[IV]], [[OFFSET]]
; CHECK-NEXT:    [[MUL:%.*]] = mul i64 [[ADD]], 3
; CHECK-NEXT:    [[GEP_SRC_DATA:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[MUL]]
; CHECK-NEXT:    [[TMP0:%.*]] = load float, ptr [[GEP_SRC_DATA]], align 4
; CHECK-NEXT:    [[GEP_KERNEL:%.*]] = getelementptr inbounds [512 x float], ptr @kernel, i64 0, i64 [[IV]]
; CHECK-NEXT:    [[TMP1:%.*]] = load float, ptr [[GEP_KERNEL]], align 4
; CHECK-NEXT:    [[MUL3:%.*]] = fmul fast float [[TMP0]], [[TMP1]]
; CHECK-NEXT:    [[GEP_KERNEL2:%.*]] = getelementptr inbounds [512 x float], ptr @kernel2, i64 0, i64 [[IV]]
; CHECK-NEXT:    [[TMP2:%.*]] = load float, ptr [[GEP_KERNEL2]], align 4
; CHECK-NEXT:    [[MUL5:%.*]] = fmul fast float [[MUL3]], [[TMP2]]
; CHECK-NEXT:    [[GEP_KERNEL3:%.*]] = getelementptr inbounds [512 x float], ptr @kernel3, i64 0, i64 [[IV]]
; CHECK-NEXT:    [[TMP3:%.*]] = load float, ptr [[GEP_KERNEL3]], align 4
; CHECK-NEXT:    [[MUL7:%.*]] = fmul fast float [[MUL5]], [[TMP3]]
; CHECK-NEXT:    [[GEP_KERNEL4:%.*]] = getelementptr inbounds [512 x float], ptr @kernel4, i64 0, i64 [[IV]]
; CHECK-NEXT:    [[TMP4:%.*]] = load float, ptr [[GEP_KERNEL4]], align 4
; CHECK-NEXT:    [[MUL9:%.*]] = fmul fast float [[MUL7]], [[TMP4]]
; CHECK-NEXT:    [[RDX_0_NEXT]] = fadd fast float [[RDX_0]], [[MUL9]]
; CHECK-NEXT:    [[GEP_SRC_DATA_SUM:%.*]] = add i64 [[MUL]], 1
; CHECK-NEXT:    [[ARRAYIDX11:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[GEP_SRC_DATA_SUM]]
; CHECK-NEXT:    [[TMP5:%.*]] = load float, ptr [[ARRAYIDX11]], align 4
; CHECK-NEXT:    [[MUL13:%.*]] = fmul fast float [[TMP1]], [[TMP5]]
; CHECK-NEXT:    [[MUL15:%.*]] = fmul fast float [[TMP2]], [[MUL13]]
; CHECK-NEXT:    [[MUL17:%.*]] = fmul fast float [[TMP3]], [[MUL15]]
; CHECK-NEXT:    [[MUL19:%.*]] = fmul fast float [[TMP4]], [[MUL17]]
; CHECK-NEXT:    [[RDX_1_NEXT]] = fadd fast float [[RDX_1]], [[MUL19]]
; CHECK-NEXT:    [[GEP_SRC_DATA_SUM52:%.*]] = add i64 [[MUL]], 2
; CHECK-NEXT:    [[ARRAYIDX21:%.*]] = getelementptr inbounds [1536 x float], ptr @src_data, i64 0, i64 [[GEP_SRC_DATA_SUM52]]
; CHECK-NEXT:    [[TMP6:%.*]] = load float, ptr [[ARRAYIDX21]], align 4
; CHECK-NEXT:    [[MUL23:%.*]] = fmul fast float [[TMP1]], [[TMP6]]
; CHECK-NEXT:    [[MUL25:%.*]] = fmul fast float [[TMP2]], [[MUL23]]
; CHECK-NEXT:    [[MUL27:%.*]] = fmul fast float [[TMP3]], [[MUL25]]
; CHECK-NEXT:    [[MUL29:%.*]] = fmul fast float [[TMP4]], [[MUL27]]
; CHECK-NEXT:    [[RDX_2_NEXT]] = fadd fast float [[RED_2]], [[MUL29]]
; CHECK-NEXT:    [[IV_NEXT]] = add i64 [[IV]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp ne i64 [[IV_NEXT]], [[SIZE]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label %[[LOOP]], label %[[EXIT]], !llvm.loop [[LOOP3:![0-9]+]]
; CHECK:       [[EXIT]]:
; CHECK-NEXT:    [[RDX_0_NEXT_LCSSA:%.*]] = phi float [ [[RDX_0_NEXT]], %[[LOOP]] ], [ [[TMP72]], %[[MIDDLE_BLOCK]] ]
; CHECK-NEXT:    [[RDX_1_NEXT_LCSSA:%.*]] = phi float [ [[RDX_1_NEXT]], %[[LOOP]] ], [ [[TMP73]], %[[MIDDLE_BLOCK]] ]
; CHECK-NEXT:    [[RDX_2_NEXT_LCSSA:%.*]] = phi float [ [[RDX_2_NEXT]], %[[LOOP]] ], [ [[TMP74]], %[[MIDDLE_BLOCK]] ]
; CHECK-NEXT:    [[RES_0:%.*]] = fadd float [[RDX_0_NEXT_LCSSA]], [[RDX_1_NEXT_LCSSA]]
; CHECK-NEXT:    [[RES_1:%.*]] = fadd float [[RES_0]], [[RDX_2_NEXT_LCSSA]]
; CHECK-NEXT:    ret float [[RES_1]]
;
>>>>>>> 1f78f6a2d629 ([LV] Check Addr in getAddressAccessSCEV in terms of SCEV expressions. (#171204))
entry:
  %cmp53 = icmp eq i32 %size, 0
  br i1 %cmp53, label %for.end, label %for.body.lr.ph

for.body.lr.ph:
  br label %for.body

for.body:
  %r.057 = phi float [ 0.000000e+00, %for.body.lr.ph ], [ %add10, %for.body ]
  %g.056 = phi float [ 0.000000e+00, %for.body.lr.ph ], [ %add20, %for.body ]
  %v.055 = phi i32 [ 0, %for.body.lr.ph ], [ %inc, %for.body ]
  %b.054 = phi float [ 0.000000e+00, %for.body.lr.ph ], [ %add30, %for.body ]
  %add = add i32 %v.055, %offset
  %mul = mul i32 %add, 3
  %arrayidx = getelementptr inbounds [1536 x float], ptr @src_data, i32 0, i32 %mul
  %0 = load float, ptr %arrayidx, align 4
  %arrayidx2 = getelementptr inbounds [512 x float], ptr @kernel, i32 0, i32 %v.055
  %1 = load float, ptr %arrayidx2, align 4
  %mul3 = fmul fast float %0, %1
  %arrayidx4 = getelementptr inbounds [512 x float], ptr @kernel2, i32 0, i32 %v.055
  %2 = load float, ptr %arrayidx4, align 4
  %mul5 = fmul fast float %mul3, %2
  %arrayidx6 = getelementptr inbounds [512 x float], ptr @kernel3, i32 0, i32 %v.055
  %3 = load float, ptr %arrayidx6, align 4
  %mul7 = fmul fast float %mul5, %3
  %arrayidx8 = getelementptr inbounds [512 x float], ptr @kernel4, i32 0, i32 %v.055
  %4 = load float, ptr %arrayidx8, align 4
  %mul9 = fmul fast float %mul7, %4
  %add10 = fadd fast float %r.057, %mul9
  %arrayidx.sum = add i32 %mul, 1
  %arrayidx11 = getelementptr inbounds [1536 x float], ptr @src_data, i32 0, i32 %arrayidx.sum
  %5 = load float, ptr %arrayidx11, align 4
  %mul13 = fmul fast float %1, %5
  %mul15 = fmul fast float %2, %mul13
  %mul17 = fmul fast float %3, %mul15
  %mul19 = fmul fast float %4, %mul17
  %add20 = fadd fast float %g.056, %mul19
  %arrayidx.sum52 = add i32 %mul, 2
  %arrayidx21 = getelementptr inbounds [1536 x float], ptr @src_data, i32 0, i32 %arrayidx.sum52
  %6 = load float, ptr %arrayidx21, align 4
  %mul23 = fmul fast float %1, %6
  %mul25 = fmul fast float %2, %mul23
  %mul27 = fmul fast float %3, %mul25
  %mul29 = fmul fast float %4, %mul27
  %add30 = fadd fast float %b.054, %mul29
  %inc = add i32 %v.055, 1
  %exitcond = icmp ne i32 %inc, %size
  br i1 %exitcond, label %for.body, label %for.cond.for.end_crit_edge

for.cond.for.end_crit_edge:
  %add30.lcssa = phi float [ %add30, %for.body ]
  %add20.lcssa = phi float [ %add20, %for.body ]
  %add10.lcssa = phi float [ %add10, %for.body ]
  %phitmp = fptoui float %add10.lcssa to i8
  %phitmp60 = fptoui float %add20.lcssa to i8
  %phitmp61 = fptoui float %add30.lcssa to i8
  br label %for.end

for.end:
  %r.0.lcssa = phi i8 [ %phitmp, %for.cond.for.end_crit_edge ], [ 0, %entry ]
  %g.0.lcssa = phi i8 [ %phitmp60, %for.cond.for.end_crit_edge ], [ 0, %entry ]
  %b.0.lcssa = phi i8 [ %phitmp61, %for.cond.for.end_crit_edge ], [ 0, %entry ]
  store i8 %r.0.lcssa, ptr @r_, align 4
  store i8 %g.0.lcssa, ptr @g_, align 4
  store i8 %b.0.lcssa, ptr @b_, align 4
  ret void
}
;.
; CHECK: [[LOOP0]] = distinct !{[[LOOP0]], [[META1:![0-9]+]], [[META2:![0-9]+]]}
; CHECK: [[META1]] = !{!"llvm.loop.isvectorized", i32 1}
; CHECK: [[META2]] = !{!"llvm.loop.unroll.runtime.disable"}
; CHECK: [[LOOP3]] = distinct !{[[LOOP3]], [[META2]], [[META1]]}
;.
