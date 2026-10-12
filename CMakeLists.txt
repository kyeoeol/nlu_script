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

set(NLU_SCRIPT_SOURCES
  src/c_api.cpp
  src/compiler.cpp
  src/parser.cpp
  src/script.cpp
  src/unicode.cpp)

# Static library for C++ users and the tools; shared library for the C API
# and the Python package.
add_library(nlu_script_static STATIC ${NLU_SCRIPT_SOURCES})
add_library(nlu_script SHARED ${NLU_SCRIPT_SOURCES})
foreach(target nlu_script_static nlu_script)
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
target_compile_definitions(nlu_script PRIVATE NLU_SCRIPT_BUILDING_DLL)
set_target_properties(nlu_script PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

add_executable(nlu_script_cli tools/nlu_script_cli.cpp)
target_link_libraries(nlu_script_cli PRIVATE nlu_script_static)

if(NLU_SCRIPT_BUILD_TESTS)
  enable_testing()
  add_executable(nlu_script_tests
    tests/test_main.cpp
    tests/test_pattern.cpp
    tests/test_script.cpp)
  target_link_libraries(nlu_script_tests PRIVATE nlu_script_static)
  add_test(NAME nlu_script_tests COMMAND nlu_script_tests)

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

include(GNUInstallDirs)
install(TARGETS nlu_script nlu_script_static nlu_script_cli
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
