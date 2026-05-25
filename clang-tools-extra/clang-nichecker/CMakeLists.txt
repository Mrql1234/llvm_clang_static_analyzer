set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_executable(clang-nichecker
  ClangNIChecker.cpp
  lib/Analysis/ProgramAnalyzer.cpp
  lib/Backend/CBMCDriverPass.cpp
  lib/Driver/Frontend.cpp
  lib/Driver/PipelineBuilder.cpp
  lib/Passes/ConditionExtractionPass.cpp
  lib/Passes/InterruptLoweringPass.cpp
  lib/Passes/LabelInsertionPass.cpp
  lib/Passes/LoopUnrollPass.cpp
  lib/Passes/ProgramClassifierPass.cpp
  lib/Passes/SequentializationPass.cpp
  lib/Passes/SourceEmissionPass.cpp
  lib/Passes/VariableRenamingPass.cpp
  lib/Support/SourceUtils.cpp
  )

target_include_directories(clang-nichecker
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

clang_target_link_libraries(clang-nichecker
  PRIVATE
  clangAST
  clangBasic
  clangFrontend
  clangLex
  clangSerialization
  clangTooling
  )
