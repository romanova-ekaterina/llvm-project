
add_llvm_library(DTLTOLocalPlugin SHARED PARTIAL_SOURCES_INTENDED
  Local.cpp

  OUTPUT_NAME DTLTOlocal

  LINK_COMPONENTS
  Support
  )
