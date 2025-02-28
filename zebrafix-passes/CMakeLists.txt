# Adapted from https://github.com/banach-space/llvm-tutor/blob/main/HelloWorld/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)
project(zebrafix-passes)

if(NOT DEFINED SVF_INSTALL_DIR)
    message(FATAL_ERROR "SVF_INSTALL_DIR is not set")
endif(NOT DEFINED SVF_INSTALL_DIR)

find_package(LLVM REQUIRED CONFIG HINTS ${LLVM_DIR} $ENV{LLVM_DIR})
if("${LLVM_VERSION_MAJOR}" VERSION_LESS 16)
  message(FATAL_ERROR "Found LLVM ${LLVM_VERSION_MAJOR}, but need LLVM 16")
endif()

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(SYSTEM ${SVF_INSTALL_DIR}/include)

link_directories(SYSTEM ${SVF_INSTALL_DIR}/lib)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "")

# LLVM is normally built without RTTI. Be consistent with that.
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

add_library(ZebrafixPasses SHARED ZebrafixInit.cpp
        ZebraAdjustGlobalsPass.cpp
        ZebraAdjustIdentifiedStructsPass.cpp
        ZebraAdjustMemoryPass.cpp
        ZebraCallgraphTraversalPass.cpp
        ZebraCounterPass.cpp
        ZebraHeapAllocsPass.cpp
        ZebraInsertStructsPass.cpp
        ZebraRewriteCallsPass.cpp
        ZebraRewriteConstantExprGEPsPass.cpp
        ZebraSVFMemIntrinsics.cpp)

# Allow undefined symbols in shared objects on Darwin (this is the default behaviour on Linux)
target_link_libraries(ZebrafixPasses
  SvfLLVM
  "$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>") 