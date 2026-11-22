cmake_minimum_required(VERSION 3.16)
project(redis_imgui_dashboard CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw)
endif()
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

add_executable(redis_dashboard main.cpp
  bench.h bench_panel.h
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

target_include_directories(redis_dashboard PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(redis_dashboard PRIVATE glfw OpenGL::GL Threads::Threads)

# Warnings for your own code only (not the ImGui sources)
set_source_files_properties(main.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

# Optional: configure with -DDASH_SANITIZE=ON to catch threading and memory bugs in the dashboard
option(DASH_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
if(DASH_SANITIZE)
  target_compile_options(redis_dashboard PRIVATE -fsanitize=address,undefined -g)
  target_link_options(redis_dashboard PRIVATE -fsanitize=address,undefined)
endif()
