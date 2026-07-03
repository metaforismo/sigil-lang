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
  src/typecheck.cpp
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

set(SIGIL_GCCJIT_AVAILABLE OFF)

if(SIGIL_ENABLE_GCCJIT)
  set(SIGIL_GCCJIT_HINTS
    /opt/homebrew/opt/libgccjit
    /opt/homebrew
    /usr/local/opt/libgccjit
    /usr/local
    /usr)

  find_program(SIGIL_BREW_EXECUTABLE brew)
  if(SIGIL_BREW_EXECUTABLE)
    execute_process(
      COMMAND "${SIGIL_BREW_EXECUTABLE}" --prefix libgccjit
      OUTPUT_VARIABLE SIGIL_BREW_LIBGCCJIT_PREFIX
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(SIGIL_BREW_LIBGCCJIT_PREFIX)
      list(PREPEND SIGIL_GCCJIT_HINTS "${SIGIL_BREW_LIBGCCJIT_PREFIX}")
    endif()
  endif()

  find_path(SIGIL_GCCJIT_INCLUDE_DIR
    NAMES libgccjit.h
    HINTS ${SIGIL_GCCJIT_HINTS}
    PATH_SUFFIXES
      include
      lib/gcc/current/include
      lib/gcc/16/include
      lib/gcc/15/include
      lib/gcc/14/include
      lib/gcc/13/include
      lib/gcc/12/include
      lib/gcc/x86_64-linux-gnu/16/include
      lib/gcc/x86_64-linux-gnu/15/include
      lib/gcc/x86_64-linux-gnu/14/include
      lib/gcc/x86_64-linux-gnu/13/include
      lib/gcc/x86_64-linux-gnu/12/include
      lib/gcc/aarch64-linux-gnu/16/include
      lib/gcc/aarch64-linux-gnu/15/include
      lib/gcc/aarch64-linux-gnu/14/include
      lib/gcc/aarch64-linux-gnu/13/include
      lib/gcc/aarch64-linux-gnu/12/include)

  find_library(SIGIL_GCCJIT_LIBRARY
    NAMES gccjit libgccjit
    HINTS ${SIGIL_GCCJIT_HINTS}
    PATH_SUFFIXES
      lib
      lib/gcc/current
      lib/gcc/16
      lib/gcc/15
      lib/gcc/14
      lib/gcc/13
      lib/gcc/12
      lib/gcc/x86_64-linux-gnu/16
      lib/gcc/x86_64-linux-gnu/15
      lib/gcc/x86_64-linux-gnu/14
      lib/gcc/x86_64-linux-gnu/13
      lib/gcc/x86_64-linux-gnu/12
      lib/gcc/aarch64-linux-gnu/16
      lib/gcc/aarch64-linux-gnu/15
      lib/gcc/aarch64-linux-gnu/14
      lib/gcc/aarch64-linux-gnu/13
      lib/gcc/aarch64-linux-gnu/12)
endif()

if(SIGIL_ENABLE_GCCJIT AND SIGIL_GCCJIT_INCLUDE_DIR AND SIGIL_GCCJIT_LIBRARY)
  set(SIGIL_GCCJIT_AVAILABLE ON)
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

add_executable(sigil_typecheck_tests tests/test_typecheck.cpp)
target_link_libraries(sigil_typecheck_tests PRIVATE sigil_core)
add_test(NAME sigil_typecheck_tests COMMAND sigil_typecheck_tests)

add_executable(sigil_backend_tests tests/test_backend.cpp)
target_link_libraries(sigil_backend_tests PRIVATE sigil_core)
add_test(NAME sigil_backend_tests COMMAND sigil_backend_tests)

add_test(
  NAME sigil_cli_artifact_tests
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_artifacts.sh"
          "$<TARGET_FILE:sigil>"
          "${CMAKE_CURRENT_SOURCE_DIR}/examples/cache.sigil"
          "${CMAKE_CURRENT_BINARY_DIR}/smt-artifacts")

add_test(
  NAME sigil_cli_type_error_tests
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_type_errors.sh"
          "$<TARGET_FILE:sigil>"
          "${CMAKE_CURRENT_BINARY_DIR}/type-errors")

set(SIGIL_EXPECTED_BACKEND_STATUS unavailable)
if(SIGIL_GCCJIT_AVAILABLE)
  set(SIGIL_EXPECTED_BACKEND_STATUS available)
endif()

set(SIGIL_GCCJIT_TEST_ENV)
if(SIGIL_GCCJIT_AVAILABLE AND APPLE)
  find_program(SIGIL_XCRUN_EXECUTABLE xcrun)
  if(SIGIL_XCRUN_EXECUTABLE)
    execute_process(
      COMMAND "${SIGIL_XCRUN_EXECUTABLE}" --show-sdk-path
      OUTPUT_VARIABLE SIGIL_APPLE_SDKROOT
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(SIGIL_APPLE_SDKROOT)
      list(APPEND SIGIL_GCCJIT_TEST_ENV "SDKROOT=${SIGIL_APPLE_SDKROOT}")
    endif()
  endif()

  file(GLOB SIGIL_GCC_EMUTLS_CANDIDATES
    "/opt/homebrew/lib/gcc/current/gcc/*/*/libemutls_w.a"
    "/opt/homebrew/opt/gcc/lib/gcc/current/gcc/*/*/libemutls_w.a"
    "/usr/local/lib/gcc/current/gcc/*/*/libemutls_w.a"
    "/usr/local/opt/gcc/lib/gcc/current/gcc/*/*/libemutls_w.a")
  list(LENGTH SIGIL_GCC_EMUTLS_CANDIDATES SIGIL_GCC_EMUTLS_COUNT)
  if(SIGIL_GCC_EMUTLS_COUNT GREATER 0)
    list(GET SIGIL_GCC_EMUTLS_CANDIDATES 0 SIGIL_GCC_EMUTLS_LIBRARY)
    get_filename_component(SIGIL_GCC_EMUTLS_DIR "${SIGIL_GCC_EMUTLS_LIBRARY}" DIRECTORY)
    get_filename_component(SIGIL_GCC_RUNTIME_DIR "${SIGIL_GCC_EMUTLS_DIR}/../../.." ABSOLUTE)
    list(APPEND SIGIL_GCCJIT_TEST_ENV
      "LIBRARY_PATH=${SIGIL_GCC_EMUTLS_DIR}:${SIGIL_GCC_RUNTIME_DIR}")
  endif()
endif()

add_test(
  NAME sigil_cli_backend_capabilities
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_backend_capabilities.sh"
          "$<TARGET_FILE:sigil>"
          "${SIGIL_EXPECTED_BACKEND_STATUS}")

if(SIGIL_GCCJIT_AVAILABLE)
  if(SIGIL_GCCJIT_TEST_ENV)
    set_tests_properties(sigil_backend_tests sigil_cli_backend_capabilities
      PROPERTIES ENVIRONMENT "${SIGIL_GCCJIT_TEST_ENV}")
  endif()
  add_test(
    NAME sigil_gccjit_compile_native
    COMMAND sigil compile "${CMAKE_CURRENT_SOURCE_DIR}/examples/native.sigil")
  add_test(
    NAME sigil_gccjit_run_native
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_run_native.sh"
            "$<TARGET_FILE:sigil>"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/native.sigil")
  add_test(
    NAME sigil_gccjit_native_diagnostics
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_native_diagnostics.sh"
            "$<TARGET_FILE:sigil>"
            "${CMAKE_CURRENT_BINARY_DIR}/native-diagnostics")
  add_test(
    NAME sigil_gccjit_native_artifacts
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_native_artifacts.sh"
            "$<TARGET_FILE:sigil>"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/native.sigil"
            "${CMAKE_CURRENT_BINARY_DIR}/native-ir-artifacts")
  if(SIGIL_GCCJIT_TEST_ENV)
    set_tests_properties(sigil_gccjit_compile_native PROPERTIES
      ENVIRONMENT "${SIGIL_GCCJIT_TEST_ENV}")
    set_tests_properties(sigil_gccjit_run_native PROPERTIES
      ENVIRONMENT "${SIGIL_GCCJIT_TEST_ENV}")
    set_tests_properties(sigil_gccjit_native_diagnostics PROPERTIES
      ENVIRONMENT "${SIGIL_GCCJIT_TEST_ENV}")
    set_tests_properties(sigil_gccjit_native_artifacts PROPERTIES
      ENVIRONMENT "${SIGIL_GCCJIT_TEST_ENV}")
  endif()
endif()

find_program(SIGIL_Z3_EXECUTABLE z3)
if(SIGIL_Z3_EXECUTABLE)
  add_test(
    NAME sigil_z3_strict_cache
    COMMAND sigil check "${CMAKE_CURRENT_SOURCE_DIR}/examples/cache.sigil" --strict)
  set_tests_properties(sigil_z3_strict_cache PROPERTIES
    ENVIRONMENT "SIGIL_Z3=${SIGIL_Z3_EXECUTABLE}")

  add_test(
    NAME sigil_z3_branch_abs
    COMMAND sigil check "${CMAKE_CURRENT_SOURCE_DIR}/examples/branches.sigil" --strict)
  set_tests_properties(sigil_z3_branch_abs PROPERTIES
    ENVIRONMENT "SIGIL_Z3=${SIGIL_Z3_EXECUTABLE}")

  add_test(
    NAME sigil_z3_assignments
    COMMAND sigil check "${CMAKE_CURRENT_SOURCE_DIR}/examples/assignments.sigil" --strict)
  set_tests_properties(sigil_z3_assignments PROPERTIES
    ENVIRONMENT "SIGIL_Z3=${SIGIL_Z3_EXECUTABLE}")

  add_test(
    NAME sigil_z3_native
    COMMAND sigil check "${CMAKE_CURRENT_SOURCE_DIR}/examples/native.sigil" --strict)
  set_tests_properties(sigil_z3_native PROPERTIES
    ENVIRONMENT "SIGIL_Z3=${SIGIL_Z3_EXECUTABLE}")

  add_test(
    NAME sigil_z3_structs
    COMMAND sigil check "${CMAKE_CURRENT_SOURCE_DIR}/examples/structs.sigil" --strict)
  set_tests_properties(sigil_z3_structs PROPERTIES
    ENVIRONMENT "SIGIL_Z3=${SIGIL_Z3_EXECUTABLE}")

  add_test(
    NAME sigil_z3_loops
    COMMAND sigil check "${CMAKE_CURRENT_SOURCE_DIR}/examples/loops.sigil" --strict)
  set_tests_properties(sigil_z3_loops PROPERTIES
    ENVIRONMENT "SIGIL_Z3=${SIGIL_Z3_EXECUTABLE}")

  add_test(
    NAME sigil_z3_void_returns
    COMMAND sigil check "${CMAKE_CURRENT_SOURCE_DIR}/examples/voids.sigil" --strict)
  set_tests_properties(sigil_z3_void_returns PROPERTIES
    ENVIRONMENT "SIGIL_Z3=${SIGIL_Z3_EXECUTABLE}")

  add_test(
    NAME sigil_z3_division_safety
    COMMAND sigil check "${CMAKE_CURRENT_SOURCE_DIR}/examples/division.sigil" --strict)
  set_tests_properties(sigil_z3_division_safety PROPERTIES
    ENVIRONMENT "SIGIL_Z3=${SIGIL_Z3_EXECUTABLE}")

  add_test(
    NAME sigil_z3_refuted_example
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_refuted.sh"
            "$<TARGET_FILE:sigil>"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/refuted.sigil")
  set_tests_properties(sigil_z3_refuted_example PROPERTIES
    ENVIRONMENT "SIGIL_Z3=${SIGIL_Z3_EXECUTABLE}")
endif()

install(TARGETS sigil sigil_core)
install(DIRECTORY include/ DESTINATION include)
