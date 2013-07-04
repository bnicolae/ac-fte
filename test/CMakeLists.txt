# Comment as needed to reduce build time
add_executable (basic_test basic_test.cpp)
add_executable (bench bench.cpp)
add_executable (dist_bench dist_bench.cpp)

# Link the executable to the necessary libraries.
target_link_libraries (basic_test ac_fte)
target_link_libraries (bench ac_fte)
target_link_libraries (dist_bench ac_fte ${MPI_CXX_LIBRARIES})
