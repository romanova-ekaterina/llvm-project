
add_llvm_component_library(LLVMDTLTO
  CodeGen.cpp

  LINK_COMPONENTS
  LTO
  Support
  )

add_subdirectory(plugins)
