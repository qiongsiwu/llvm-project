add_llvm_component_library(LLVMCoroutines
  Coroutines.cpp
  CoroAnnotationElide.cpp
  CoroCleanup.cpp
  CoroConditionalWrapper.cpp
  CoroEarly.cpp
  CoroElide.cpp
  CoroFrame.cpp
  CoroSplit.cpp
  SuspendCrossingInfo.cpp
  SpillUtils.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/Coroutines

  DEPENDS
  intrinsics_gen
  LLVMAnalysis

  LINK_COMPONENTS
  Analysis
  Core
  IPO
  Scalar
  Support
  TransformUtils
  TargetParser
  )
