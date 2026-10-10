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

set(report $<TARGET_FILE:heapline-report>)
set(data ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_test(NAME report_job
         COMMAND sh -c "\"$1\" \"$2/job\" | diff -u \"$2/job.expected\" -"
                 sh ${report} ${data})
add_test(NAME report_bad_version
         COMMAND sh -c "! \"$1\" \"$2/bad-version\" 2>bad_version.err \
           && grep -q 'unsupported format version 2' bad_version.err \
           && grep -q 'no valid record' bad_version.err"
                 sh ${report} ${data})
add_test(NAME report_truncated
         COMMAND sh -c "\"$1\" \"$2/truncated\" >/dev/null 2>truncated.err \
           && grep -q 'missing values' truncated.err"
                 sh ${report} ${data})
add_test(NAME report_missing_rank
         COMMAND sh -c "\"$1\" \"$2/missing-rank\" >/dev/null \
           2>missing_rank.err && grep -q 'missing ranks: 2 ' missing_rank.err"
                 sh ${report} ${data})
add_test(NAME report_top
         COMMAND sh -c "\"$1\" --top 2 \"$2/job\" >top.out \
           && grep -q '^Ranks by peak heap (2 of 4)$' top.out \
           && test $(grep -c '^ *[0-9]  node-' top.out) -eq 2"
                 sh ${report} ${data})
add_test(NAME report_all
         COMMAND sh -c "\"$1\" --top 1 --all \"$2/job\" \
           | grep -q '^Ranks by peak heap (4 of 4)$'"
                 sh ${report} ${data})
add_test(NAME report_csv
         COMMAND sh -c "\"$1\" --csv merged.csv \"$2/job\" >/dev/null \
           && diff -u \"$2/job.merged.csv\" merged.csv"
                 sh ${report} ${data})
add_test(NAME report_version
         COMMAND sh -c "\"$1\" --version | grep -q '^heapline-report [0-9]'"
                 sh ${report})
add_test(NAME report_usage
         COMMAND sh -c "! \"$1\" 2>usage.err && grep -q '^usage:' usage.err"
                 sh ${report})
