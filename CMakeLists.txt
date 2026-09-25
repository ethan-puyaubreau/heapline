cmake_minimum_required(VERSION 3.16)
project(heapline VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HEAPLINE_ENABLE_WERROR "Treat compiler warnings as errors" OFF)

add_library(heapline SHARED src/heapline.cpp)
target_link_libraries(heapline PRIVATE ${CMAKE_DL_LIBS})
target_compile_options(heapline PRIVATE -Wall -Wextra -Wpedantic
                       $<$<BOOL:${HEAPLINE_ENABLE_WERROR}>:-Werror>)
