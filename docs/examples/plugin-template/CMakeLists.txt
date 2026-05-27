cmake_minimum_required(VERSION 3.14)
project(ecewo-hello VERSION 0.1.0 LANGUAGES C)

if(NOT TARGET ecewo::ecewo)
  include(FetchContent)
  FetchContent_Declare(
    ecewo
    GIT_REPOSITORY https://github.com/ecewo/ecewo.git
    GIT_TAG        v4
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(ecewo)
endif()

add_library(ecewo_hello SHARED src/hello.c)

target_include_directories(ecewo_hello
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# ecewo::ecewo carries the include dir for ecewo.h (and bundled uv.h), and the
# uv_* symbols resolve at runtime from the self-contained libecewo.
target_link_libraries(ecewo_hello PRIVATE ecewo::ecewo)

set_target_properties(ecewo_hello PROPERTIES
  C_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON
)
