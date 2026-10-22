cmake_minimum_required(VERSION 3.20)
project(pl0 LANGUAGES CXX)

option(PL0_ENABLE_ASAN "Enable address and undefined behavior sanitizers" ON)
option(PL0_BUILD_TOOLS "Build standalone tooling binaries" ON)
option(PL0_BUILD_TESTS "Build unit tests" ON)
option(PL0_BUILD_GUI "Build Qt graphical interface" ON)
option(PL0_QT5_COMPAT "Use Qt 5 only for local compatibility checks" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All source files and UI strings are UTF-8. Apply this globally so MSVC also
# decodes headers correctly when compiling tests and executable targets.
if(MSVC)
  add_compile_options(/utf-8)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(PL0_SOURCES
    src/Codegen.cpp
    src/Driver.cpp
    src/Diagnostics.cpp
    src/Lexer.cpp
    src/PCode.cpp
    src/Parser.cpp
    src/Symbol.cpp
    src/SymbolTable.cpp
    src/Token.cpp
    src/Utility.cpp
    src/VM.cpp
)

add_library(pl0 STATIC ${PL0_SOURCES})
add_library(pl0::pl0 ALIAS pl0)

target_include_directories(pl0
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(pl0 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
elseif(MSVC)
  target_compile_options(pl0 PRIVATE /W4 /WX)
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug" AND PL0_ENABLE_ASAN)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(PL0_SANITIZER_FLAGS -fsanitize=address -fsanitize=undefined)
    target_compile_options(pl0 PRIVATE ${PL0_SANITIZER_FLAGS})
    target_link_options(pl0 PUBLIC ${PL0_SANITIZER_FLAGS})
  endif()
endif()

if(PL0_BUILD_TOOLS)
  add_executable(pl0-cli src/main.cpp)
  target_link_libraries(pl0-cli PRIVATE pl0::pl0)
  set_target_properties(pl0-cli PROPERTIES OUTPUT_NAME pl0)

  add_executable(pl0c tools/pl0c.cpp)
  target_link_libraries(pl0c PRIVATE pl0::pl0)

  add_executable(pl0run tools/pl0run.cpp)
  target_link_libraries(pl0run PRIVATE pl0::pl0)

  add_executable(pl0dis tools/pl0dis.cpp)
  target_link_libraries(pl0dis PRIVATE pl0::pl0)
endif()

if(PL0_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(PL0_BUILD_GUI)
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)
  set(CMAKE_AUTOUIC ON)
  set(PL0_GUI_SOURCES
      gui/main.cpp
      gui/CodeEditor.cpp
      gui/MainWindow.cpp
      gui/MainWindow.hpp
      gui/CodeEditor.hpp)
  list(APPEND PL0_GUI_SOURCES gui/AstWidget.cpp gui/AstWidget.hpp)
  if(PL0_QT5_COMPAT)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
    add_executable(pl0-gui ${PL0_GUI_SOURCES})
    target_link_libraries(pl0-gui PRIVATE pl0::pl0 Qt5::Widgets)
    set_target_properties(pl0-gui PROPERTIES WIN32_EXECUTABLE OFF)
  else()
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
    qt_add_executable(pl0-gui ${PL0_GUI_SOURCES})
    target_link_libraries(pl0-gui PRIVATE pl0::pl0 Qt6::Widgets)
  endif()
  if(PL0_BUILD_TESTS)
    add_executable(pl0_replay_tests tests/gui/ParseReplayTests.cpp
        gui/MainWindow.cpp gui/MainWindow.hpp gui/CodeEditor.cpp gui/CodeEditor.hpp
        gui/AstWidget.cpp gui/AstWidget.hpp)
    target_include_directories(pl0_replay_tests PRIVATE gui)
    if(PL0_QT5_COMPAT)
      target_link_libraries(pl0_replay_tests PRIVATE pl0::pl0 Qt5::Widgets)
    else()
      target_link_libraries(pl0_replay_tests PRIVATE pl0::pl0 Qt6::Widgets)
    endif()
    add_test(NAME parse_replay COMMAND pl0_replay_tests)
    set_tests_properties(parse_replay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  endif()
endif()
