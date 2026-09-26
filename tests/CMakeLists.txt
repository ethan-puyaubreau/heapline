add_executable(alloc_pattern alloc_pattern.cpp)
target_include_directories(alloc_pattern PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(alloc_pattern PRIVATE ${CMAKE_DL_LIBS})
# Keep the compiler from folding the allocation calls under test.
target_compile_options(alloc_pattern PRIVATE -fno-builtin)

add_test(NAME alloc_pattern COMMAND alloc_pattern)
set_tests_properties(alloc_pattern PROPERTIES
                     ENVIRONMENT LD_PRELOAD=$<TARGET_FILE:heapline>)

# Run under a %p file pattern and expect exactly one summary file, named after
# the process id.
add_test(NAME output_file
         COMMAND sh -c "rm -f summary.*.txt \
           && LD_PRELOAD=\"$1\" HEAPLINE_OUTPUT=summary.%p.txt \"$2\" \
           && test $(ls summary.[0-9]*.txt | wc -l) -eq 1 \
           && grep -q '^heapline: ' summary.[0-9]*.txt"
                 sh $<TARGET_FILE:heapline> $<TARGET_FILE:alloc_pattern>)
