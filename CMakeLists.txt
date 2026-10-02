cmake_minimum_required(VERSION 3.20)
project(gigaroute_cad_optimizer VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(CADOPT_BUILD_TESTS "Build tests" ON)
option(CADOPT_BUILD_GUI "Build the minimal Qt file-picker shell when Qt6 Widgets is available" ON)

add_library(cadopt_core
  src/dxf.cpp
  src/verifier.cpp
  src/trace.cpp
  src/dwg_backend.cpp
  src/input.cpp
  src/geometry.cpp
  src/candidate.cpp
  src/search.cpp
  src/evaluator.cpp
)
target_include_directories(cadopt_core PUBLIC include)
if(CMAKE_DL_LIBS)
  target_link_libraries(cadopt_core PRIVATE ${CMAKE_DL_LIBS})
endif()
if(MSVC)
  target_compile_options(cadopt_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(cadopt_core PRIVATE -Wall -Wextra -Wpedantic -finput-charset=UTF-8)
endif()

add_executable(cadopt src/main.cpp)
target_link_libraries(cadopt PRIVATE cadopt_core)

if(CADOPT_BUILD_TESTS)
  enable_testing()

  add_executable(cadopt_tests tests/test_roundtrip.cpp)
  target_link_libraries(cadopt_tests PRIVATE cadopt_core)
  target_compile_definitions(cadopt_tests PRIVATE CADOPT_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  add_test(NAME cadopt_tests COMMAND cadopt_tests)

  add_executable(cadopt_geometry_tests tests/test_geometry.cpp)
  target_link_libraries(cadopt_geometry_tests PRIVATE cadopt_core)
  target_compile_definitions(cadopt_geometry_tests PRIVATE CADOPT_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  add_test(NAME cadopt_geometry_tests COMMAND cadopt_geometry_tests)

  add_executable(cadopt_candidate_tests tests/test_candidate.cpp)
  target_link_libraries(cadopt_candidate_tests PRIVATE cadopt_core)
  add_test(NAME cadopt_candidate_tests COMMAND cadopt_candidate_tests)

  add_executable(cadopt_search_tests tests/test_search.cpp)
  target_link_libraries(cadopt_search_tests PRIVATE cadopt_core)
  add_test(NAME cadopt_search_tests COMMAND cadopt_search_tests)

  add_executable(cadopt_input_tests tests/test_input.cpp)
  target_link_libraries(cadopt_input_tests PRIVATE cadopt_core)
  add_test(NAME cadopt_input_tests COMMAND cadopt_input_tests)

  add_library(cadopt_fake_native_backend SHARED tests/fake_native_backend.cpp)
  target_include_directories(cadopt_fake_native_backend PRIVATE include)
  set_target_properties(cadopt_fake_native_backend PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
  )
  if(MSVC)
    target_compile_options(cadopt_fake_native_backend PRIVATE /W4 /utf-8)
  else()
    target_compile_options(cadopt_fake_native_backend PRIVATE -Wall -Wextra -Wpedantic -finput-charset=UTF-8)
  endif()

  add_executable(cadopt_evaluator_tests tests/test_evaluator.cpp)
  target_link_libraries(cadopt_evaluator_tests PRIVATE cadopt_core)
  add_dependencies(cadopt_evaluator_tests cadopt_fake_native_backend)
  target_compile_definitions(cadopt_evaluator_tests PRIVATE
    CADOPT_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    CADOPT_TEST_NATIVE_BACKEND_PATH="$<TARGET_FILE:cadopt_fake_native_backend>"
  )
  add_test(NAME cadopt_evaluator_tests COMMAND cadopt_evaluator_tests)

  add_executable(cadopt_native_backend_tests tests/test_native_backend.cpp)
  target_link_libraries(cadopt_native_backend_tests PRIVATE cadopt_core)
  add_dependencies(cadopt_native_backend_tests cadopt_fake_native_backend)
  target_compile_definitions(cadopt_native_backend_tests PRIVATE
    CADOPT_TEST_NATIVE_BACKEND_PATH="$<TARGET_FILE:cadopt_fake_native_backend>"
  )
  add_test(NAME cadopt_native_backend_tests COMMAND cadopt_native_backend_tests)

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME cadopt_python_search_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python_search.py)
  else()
    message(STATUS "Python3 interpreter not found: cadopt_python_search_tests will not run")
  endif()
endif()

if(CADOPT_BUILD_GUI)
  find_package(Qt6 COMPONENTS Widgets QUIET)
  if(Qt6Widgets_FOUND)
    add_executable(cadopt_gui app/minimal_qt/main.cpp)
    target_link_libraries(cadopt_gui PRIVATE Qt6::Widgets)
    if(MSVC)
      target_compile_options(cadopt_gui PRIVATE /utf-8)
    endif()
  else()
    message(STATUS "Qt6 Widgets not found: cadopt_gui will not be built")
  endif()
endif()
