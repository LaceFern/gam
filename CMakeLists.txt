cmake_minimum_required(VERSION 3.10)
project(GAM)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -std=c++11 -DNDEBUG -rdynamic")

option(DHT OFF)

if(DHT)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDHT")
endif(DHT)

# inlcude: common headers
include_directories("${CMAKE_SOURCE_DIR}/include")

# include cuckoo headers
include_directories("${CMAKE_SOURCE_DIR}/lib/libcuckoo/src")

# include cityhash headers
include_directories("${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src")

# include custom headers
include_directories("${CMAKE_SOURCE_DIR}/include/custom_include")
# include hdr_histogram headers
include_directories(${CMAKE_SOURCE_DIR}/lib/HdrHistogram/include)

set(LIBRARIES ${LIBRARIES} ibverbs pthread boost_thread boost_system hdr_histogram_static)


file(GLOB GAMSOURCESLIST ${CMAKE_SOURCE_DIR}/src/*.cc)
list(APPEND GAMSOURCE ${GAMSOURCESLIST})

set(GAMSOURCE
        ${CMAKE_SOURCE_DIR}/src/gfunc.cc
        ${CMAKE_SOURCE_DIR}/src/ae.cc
        ${CMAKE_SOURCE_DIR}/src/client.cc
        ${CMAKE_SOURCE_DIR}/src/server.cc
        ${CMAKE_SOURCE_DIR}/src/worker.cc
        ${CMAKE_SOURCE_DIR}/src/gallocator.cc
        ${CMAKE_SOURCE_DIR}/src/master.cc
        ${CMAKE_SOURCE_DIR}/src/tcp.cc
        ${CMAKE_SOURCE_DIR}/src/worker_handle.cc
        ${CMAKE_SOURCE_DIR}/src/anet.cc
        ${CMAKE_SOURCE_DIR}/src/rdma.cc
        ${CMAKE_SOURCE_DIR}/src/util.cc
        ${CMAKE_SOURCE_DIR}/src/zmalloc.cc
        ${CMAKE_SOURCE_DIR}/src/log.cc
        ${CMAKE_SOURCE_DIR}/src/slabs.cc
        ${CMAKE_SOURCE_DIR}/src/workrequest.cc
        ${CMAKE_SOURCE_DIR}/src/cache.cc
        ${CMAKE_SOURCE_DIR}/src/directory.cc
        ${CMAKE_SOURCE_DIR}/src/local_request.cc
        ${CMAKE_SOURCE_DIR}/src/remote_request.cc
        ${CMAKE_SOURCE_DIR}/src/pending_request.cc
        ${CMAKE_SOURCE_DIR}/src/MurmurHash.cc
        ${CMAKE_SOURCE_DIR}/src/logging.cc
        ${CMAKE_SOURCE_DIR}/src/custom_src/histogram.cpp
)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/HdrHistogram)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src/.libs/libcityhash.a")
    add_custom_command(OUTPUT LIBCUCKOO
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/libcuckoo
        COMMAND "autoreconf" "-fis"
        COMMAND "./configure"
        COMMAND "make"
    )

    add_custom_target(libcuckoo ALL DEPENDS LIBCUCKOO)

    add_dependencies(galloc libcuckoo)
endif()


add_library(galloc ${GAMSOURCE})


file(GLOB TESTSOURCESLIST ${CMAKE_SOURCE_DIR}/test/*.cc)
list(APPEND TESTSOURCE ${TESTSOURCESLIST})

foreach (test_file_path IN LISTS TESTSOURCE)
    file(RELATIVE_PATH test_name ${CMAKE_SOURCE_DIR}/test ${test_file_path})
    string(REPLACE ".cc" "" test_name ${test_name})  

    add_executable(${test_name} ${test_file_path})
    target_link_libraries(${test_name} galloc ${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src/.libs/libcityhash.a ${LIBRARIES})
  
endforeach()