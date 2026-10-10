find_package(Threads REQUIRED)
add_executable(malloc_churn malloc_churn.cpp)
target_link_libraries(malloc_churn PRIVATE Threads::Threads)
# Keep the compiler from removing the malloc and free pairs.
target_compile_options(malloc_churn PRIVATE -fno-builtin)
