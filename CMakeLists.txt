cmake_minimum_required(VERSION 3.20)
project(SysPulse-TUI LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -pthread -O2)

include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

include_directories(include)

file(GLOB_RECURSE SOURCES
  src/main.cpp
  src/core/*.cpp
  src/collectors/*.cpp
  src/ui/*.cpp
)

add_executable(syspulse ${SOURCES})

target_link_libraries(syspulse PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
  pthread
)
