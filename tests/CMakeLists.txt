add_executable(alloc_pattern alloc_pattern.cpp)
target_include_directories(alloc_pattern PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(alloc_pattern PRIVATE ${CMAKE_DL_LIBS})
# Keep the compiler from folding the allocation calls under test.
target_compile_options(alloc_pattern PRIVATE -fno-builtin)

add_test(NAME alloc_pattern COMMAND alloc_pattern)
set_tests_properties(alloc_pattern PROPERTIES
                     ENVIRONMENT LD_PRELOAD=$<TARGET_FILE:heapline>)

set(check_record ${CMAKE_CURRENT_SOURCE_DIR}/check_record.sh)
set(lib $<TARGET_FILE:heapline>)
set(prog $<TARGET_FILE:alloc_pattern>)
add_test(NAME record_no_rank COMMAND sh ${check_record} ${lib} ${prog} -1)
add_test(NAME record_slurm_rank
         COMMAND sh ${check_record} ${lib} ${prog} 3 SLURM_PROCID=3)
add_test(NAME record_ompi_rank
         COMMAND sh ${check_record} ${lib} ${prog} 5 OMPI_COMM_WORLD_RANK=5)
add_test(NAME record_rank_priority
         COMMAND sh ${check_record} ${lib} ${prog} 3
                 OMPI_COMM_WORLD_RANK=5 SLURM_PROCID=3)

# When the file cannot be opened, the record goes to stderr.
add_test(NAME record_fallback
         COMMAND sh -c "LD_PRELOAD=\"$1\" \
           HEAPLINE_OUTPUT=/nonexistent/dir/record.csv \"$2\" 2>&1 \
           | grep -q '^# heapline 1$'"
                 sh ${lib} ${prog})

# The heap peak is reached at the start or one second in, and the record must
# say so.
add_executable(peak_time peak_time.cpp)
target_compile_options(peak_time PRIVATE -fno-builtin)
set(check_peak_time ${CMAKE_CURRENT_SOURCE_DIR}/check_peak_time.sh)
add_test(NAME peak_time_early
         COMMAND sh ${check_peak_time} ${lib} $<TARGET_FILE:peak_time> early)
add_test(NAME peak_time_late
         COMMAND sh ${check_peak_time} ${lib} $<TARGET_FILE:peak_time> late)

# Pure helpers of the library, tested without preloading it.
add_executable(record_test record_test.cpp ${PROJECT_SOURCE_DIR}/src/record.cpp)
target_include_directories(record_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME record COMMAND record_test)
