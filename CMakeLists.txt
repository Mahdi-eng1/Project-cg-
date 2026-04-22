cmake_minimum_required(VERSION 3.20)
project(CGFinal3DGame LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)

set(GLAD_PROFILE "core" CACHE STRING "" FORCE)
set(GLAD_API "gl=3.3" CACHE STRING "" FORCE)
set(GLAD_GENERATOR "c" CACHE STRING "" FORCE)
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG v0.1.36
)

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
)

FetchContent_MakeAvailable(glfw glm glad stb)

add_executable(CGFinal3DGame
  src/main.cpp
)

target_include_directories(CGFinal3DGame PRIVATE
  src
  ${stb_SOURCE_DIR}
)

target_link_libraries(CGFinal3DGame PRIVATE glfw glad glm::glm)

if (MSVC)
  target_compile_options(CGFinal3DGame PRIVATE /W4 /permissive-)
else()
  target_compile_options(CGFinal3DGame PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET CGFinal3DGame POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:CGFinal3DGame>/assets
)
