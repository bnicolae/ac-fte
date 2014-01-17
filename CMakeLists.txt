# The name of our project is "BlobSeer". CMakeLists files in this project can
# refer to the root source directory of the project as ${BLOBSEER_SOURCE_DIR} and
# to the root binary directory of the project as ${BLOBSEER_BINARY_DIR}.
cmake_minimum_required (VERSION 2.8)
project (ACFTE)

# set the root prefix of the library dependencies
set(EXTERNAL_ROOT ${HOME}/deploy)

# some global compiler flags
add_definitions(-O2 -Wall -D__BENCHMARK -D__ASSERT -D__INFO)
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=c++11")

# set the path to the cmake modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

# Now set up the libraries
# -----------------------------------------------------------------------------------

# the elegant way to set up boost
set(BOOST_ROOT ${EXTERNAL_ROOT})
set(Boost_FIND_REQUIRED True)
find_package(Boost 1.40 COMPONENTS thread mpi)
include_directories(${Boost_INCLUDE_DIR})

find_package(Threads)
set(Boost_LIBRARIES ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

# set up OpenSSL
find_package(OpenSSL)
include_directories(${OPENSSL_INCLUDE_DIR})

# set up MPI
find_package(MPI)
include_directories(${MPI_CXX_INCLUDE_PATH})

# -----------------------------------------------------------------------------------

include_directories(${ACFTE_SOURCE_DIR})
add_subdirectory (lib)
add_subdirectory (test)