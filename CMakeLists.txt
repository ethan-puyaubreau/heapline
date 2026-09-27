cmake_minimum_required(VERSION 3.16)
project(heapline VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HEAPLINE_ENABLE_WERROR "Treat compiler warnings as errors" OFF)
option(HEAPLINE_ENABLE_TESTS "Build the tests" ON)

add_compile_options(-Wall -Wextra -Wpedantic
                    $<$<BOOL:${HEAPLINE_ENABLE_WERROR}>:-Werror>)

add_library(heapline SHARED src/heapline.cpp)
target_include_directories(heapline PUBLIC include)
target_link_libraries(heapline PRIVATE ${CMAKE_DL_LIBS})

include(GNUInstallDirs)
install(TARGETS heapline LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/heapline.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(HEAPLINE_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
