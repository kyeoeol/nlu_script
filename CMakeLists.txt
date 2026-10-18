cmake_minimum_required(VERSION 3.14)
project(nlu_script VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(NLU_SCRIPT_BUILD_TESTS "Build the test suite" ON)
# libFuzzer targets; needs clang, and the sources built with
# -fsanitize=fuzzer-no-link (see the fuzz job in .github/workflows/ci.yml).
option(NLU_SCRIPT_BUILD_FUZZERS "Build the libFuzzer targets" OFF)

find_package(Threads REQUIRED)

# Sources and tests contain UTF-8 string literals. Without /utf-8, MSVC reads
# them in the system code page and mangles them.
if(MSVC)
  add_compile_options(/utf-8)
endif()

set(NLU_SCRIPT_SOURCES
  src/c_api.cpp
  src/compiler.cpp
  src/datetime.cpp
  src/lint.cpp
  src/numbers.cpp
  src/parser.cpp
  src/script.cpp
  src/system_entities.cpp
  src/unicode.cpp)

# The sources compile once into an object library shared by the static
# library (C++ users and the tools) and the shared library (the C API and the
# Python package). Only the C API is exported from the shared library; hidden
# visibility does not affect static linking.
add_library(nlu_script_objects OBJECT ${NLU_SCRIPT_SOURCES})
target_include_directories(nlu_script_objects PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
if(MSVC)
  target_compile_options(nlu_script_objects PRIVATE /W4)
else()
  target_compile_options(nlu_script_objects PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_compile_definitions(nlu_script_objects PRIVATE NLU_SCRIPT_BUILDING_DLL)
set_target_properties(nlu_script_objects PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

add_library(nlu_script_static STATIC $<TARGET_OBJECTS:nlu_script_objects>)
add_library(nlu_script SHARED $<TARGET_OBJECTS:nlu_script_objects>)
foreach(target nlu_script_static nlu_script)
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
  target_link_libraries(${target} PUBLIC Threads::Threads)
endforeach()

# The script test runner (`--test`) is shared by the CLI and the test suite.
add_library(nlu_script_tools STATIC tools/script_tests.cpp)
target_include_directories(nlu_script_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tools)
target_link_libraries(nlu_script_tools PUBLIC nlu_script_static)

add_executable(nlu_script_cli tools/nlu_script_cli.cpp)
target_link_libraries(nlu_script_cli PRIVATE nlu_script_tools)

if(NLU_SCRIPT_BUILD_TESTS)
  enable_testing()
  add_executable(nlu_script_tests
    tests/test_datetime.cpp
    tests/test_entities.cpp
    tests/test_explain.cpp
    tests/test_include.cpp
    tests/test_lint.cpp
    tests/test_main.cpp
    tests/test_pattern.cpp
    tests/test_script.cpp
    tests/test_script_tests.cpp)
  target_link_libraries(nlu_script_tests PRIVATE nlu_script_tools)
  add_test(NAME nlu_script_tests COMMAND nlu_script_tests)

  # The demo script's own test cases, run the way script authors run theirs.
  add_test(NAME nlu_script_demo_test
    COMMAND nlu_script_cli --test examples/demo.nlu examples/demo.test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # The demo has no lint findings; intended overlaps are marked in it.
  add_test(NAME nlu_script_demo_lint
    COMMAND nlu_script_cli --lint examples/demo.nlu
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # A failing case must make `--test` exit with failure.
  add_test(NAME nlu_script_cli_test_failure
    COMMAND nlu_script_cli --test examples/demo.nlu tests/data/demo_failing.test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(nlu_script_cli_test_failure PROPERTIES WILL_FAIL TRUE)

  add_executable(nlu_script_c_api_test tests/test_c_api.c)
  target_link_libraries(nlu_script_c_api_test PRIVATE nlu_script)
  add_test(NAME nlu_script_c_api_test COMMAND nlu_script_c_api_test)

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME nlu_script_python_tests
      COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/python/tests -t ${CMAKE_CURRENT_SOURCE_DIR}/python/tests)
    set_tests_properties(nlu_script_python_tests PROPERTIES
      ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python;NLU_SCRIPT_LIBRARY=$<TARGET_FILE:nlu_script>")
  endif()
endif()

if(NLU_SCRIPT_BUILD_FUZZERS)
  foreach(target fuzz_script fuzz_match)
    add_executable(${target} fuzz/${target}.cpp)
    target_link_libraries(${target} PRIVATE nlu_script_static)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
  endforeach()
endif()

include(GNUInstallDirs)
install(TARGETS nlu_script nlu_script_static nlu_script_cli
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
