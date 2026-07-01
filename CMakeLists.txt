cmake_minimum_required(VERSION 3.20)

project(Sigil
  VERSION 0.1.0
  DESCRIPTION "A GCC JIT based systems language with first-class compile-time invariants."
  LANGUAGES CXX)

option(SIGIL_ENABLE_GCCJIT "Enable the libgccjit backend when libgccjit is available" ON)
option(SIGIL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sigil_core
  src/ast.cpp
  src/lexer.cpp
  src/parser.cpp
  src/proof.cpp
  src/backend/gccjit_backend.cpp)

target_include_directories(sigil_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(sigil_core PUBLIC cxx_std_17)
target_compile_definitions(sigil_core PUBLIC SIGIL_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(sigil_core PRIVATE /W4)
  if(SIGIL_WARNINGS_AS_ERRORS)
    target_compile_options(sigil_core PRIVATE /WX)
  endif()
else()
  target_compile_options(sigil_core PRIVATE -Wall -Wextra -Wpedantic)
  if(SIGIL_WARNINGS_AS_ERRORS)
    target_compile_options(sigil_core PRIVATE -Werror)
  endif()
endif()

if(SIGIL_ENABLE_GCCJIT)
  find_path(SIGIL_GCCJIT_INCLUDE_DIR NAMES libgccjit.h)
  find_library(SIGIL_GCCJIT_LIBRARY NAMES gccjit libgccjit)
endif()

if(SIGIL_ENABLE_GCCJIT AND SIGIL_GCCJIT_INCLUDE_DIR AND SIGIL_GCCJIT_LIBRARY)
  target_compile_definitions(sigil_core PUBLIC SIGIL_HAVE_GCCJIT=1)
  target_include_directories(sigil_core PRIVATE "${SIGIL_GCCJIT_INCLUDE_DIR}")
  target_link_libraries(sigil_core PRIVATE "${SIGIL_GCCJIT_LIBRARY}")
  message(STATUS "libgccjit backend enabled")
else()
  target_compile_definitions(sigil_core PUBLIC SIGIL_HAVE_GCCJIT=0)
  message(STATUS "libgccjit backend disabled; install libgccjit to enable native lowering")
endif()

add_executable(sigil src/main.cpp)
target_link_libraries(sigil PRIVATE sigil_core)

enable_testing()

add_executable(sigil_parser_tests tests/test_parser.cpp)
target_link_libraries(sigil_parser_tests PRIVATE sigil_core)
add_test(NAME sigil_parser_tests COMMAND sigil_parser_tests)

add_executable(sigil_proof_tests tests/test_proof.cpp)
target_link_libraries(sigil_proof_tests PRIVATE sigil_core)
add_test(NAME sigil_proof_tests COMMAND sigil_proof_tests)

install(TARGETS sigil sigil_core)
install(DIRECTORY include/ DESTINATION include)
