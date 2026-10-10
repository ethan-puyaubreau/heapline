cmake_minimum_required(VERSION 3.16)
project(heapline VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HEAPLINE_ENABLE_WERROR "Treat compiler warnings as errors" OFF)
option(HEAPLINE_ENABLE_TESTS "Build the tests" ON)
option(HEAPLINE_ENABLE_BENCH "Build the overhead benchmark" OFF)
option(HEAPLINE_ENABLE_EXAMPLES "Build the examples, which need MPI" OFF)

add_compile_options(-Wall -Wextra -Wpedantic
                    $<$<BOOL:${HEAPLINE_ENABLE_WERROR}>:-Werror>)

add_library(heapline SHARED src/heapline.cpp src/record.cpp)
target_include_directories(heapline PUBLIC include PRIVATE src)
target_link_libraries(heapline PRIVATE ${CMAKE_DL_LIBS})

add_executable(heapline-report tools/heapline-report.cpp)
target_compile_definitions(heapline-report
                           PRIVATE HEAPLINE_VERSION="${PROJECT_VERSION}")
# std::filesystem needs -lstdc++fs before GCC 9.1 (RHEL and Rocky 8 system GCC).
target_link_libraries(heapline-report PRIVATE
  $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.1>>:stdc++fs>)

include(GNUInstallDirs)
install(TARGETS heapline LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/heapline.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS heapline-report RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(HEAPLINE_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(HEAPLINE_ENABLE_BENCH)
  add_subdirectory(bench)
endif()

if(HEAPLINE_ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()
