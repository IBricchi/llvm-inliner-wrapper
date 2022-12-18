cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 17)
project(InlineAdvisor)

# Find the LLVM package
find_package(LLVM REQUIRED CONFIG)
# print the LLVM path
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include(AddLLVM)

# Set the include directories
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Add the plugin library
add_llvm_pass_plugin(plugin InlineAdvisorPlugin.cpp)