add_library (ac_fte SHARED 
    ac_fte.cpp 
    region_manager.cpp 
    cow_allocator.cpp
    dedup_engine.cpp
    syscall_overrides.c
)

# Link the executable to the necessary libraries.
target_link_libraries (ac_fte ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES})

# Install libraries
install (TARGETS ac_fte
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install (FILES
  ${ACFTE_SOURCE_DIR}/lib/ac_fte.h
  DESTINATION include
)
