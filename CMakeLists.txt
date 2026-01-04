cmake_minimum_required(VERSION 3.10)
project(SymulacjaUrzadMiasta LANGUAGES C)

# C version setup
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# debug build mode
set(CMAKE_BUILD_TYPE Debug)

# output dir
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# helpers source
file(GLOB HELPER_SOURCES "src/helpers/*.c")
add_library(helpers STATIC ${HELPER_SOURCES})
target_include_directories(helpers PUBLIC src)

# programs source
file(GLOB PROGRAM_FILES "src/programs/*.c")

# compile programs
foreach(PROG_PATH ${PROGRAM_FILES})
  # get filename for executable
  get_filename_component(PROG_NAME ${PROG_PATH} NAME_WE)

  # create executables with linked libs
  add_executable(${PROG_NAME} ${PROG_PATH})
  target_link_libraries(${PROG_NAME} PRIVATE helpers)
endforeach()