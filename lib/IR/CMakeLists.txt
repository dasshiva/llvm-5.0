set(LLVM_TARGET_DEFINITIONS AttributesCompatFunc.td)
tablegen(LLVM AttributesCompatFunc.inc -gen-attrs)
add_public_tablegen_target(AttributeCompatFuncTableGen)

add_llvm_library(LLVMCore
  AsmWriter.cpp
  Attributes.cpp
  AutoUpgrade.cpp
  BasicBlock.cpp
  Comdat.cpp
  ConstantFold.cpp
  ConstantRange.cpp
  Constants.cpp
  Core.cpp
  DIBuilder.cpp
  DataLayout.cpp
  DebugInfo.cpp
  DebugInfoMetadata.cpp
  DebugLoc.cpp
  DiagnosticInfo.cpp
  DiagnosticPrinter.cpp
  Dominators.cpp
  Function.cpp
  GCOV.cpp
  GVMaterializer.cpp
  Globals.cpp
  Exporter.cpp
  IRBuilder.cpp
  IRPrintingPasses.cpp
  InlineAsm.cpp
  Instruction.cpp
  Instructions.cpp
  IntrinsicInst.cpp
  LLVMContext.cpp
  LLVMContextImpl.cpp
  LegacyPassManager.cpp
  MDBuilder.cpp
  Mangler.cpp
  Metadata.cpp
  Module.cpp
  ModuleSummaryIndex.cpp
  Operator.cpp
  OptBisect.cpp
  Pass.cpp
  PassManager.cpp
  PassRegistry.cpp
  SafepointIRVerifier.cpp
  ProfileSummary.cpp
  Statepoint.cpp
  Type.cpp
  TypeFinder.cpp
  Use.cpp
  User.cpp
  Value.cpp
  ValueSymbolTable.cpp
  ValueTypes.cpp
  Verifier.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/IR

  DEPENDS
  intrinsics_gen
  )
