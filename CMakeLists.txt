cmake_minimum_required(VERSION 3.10)
project(GAM)


set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 强制使用 clang++ 作为 C++ 编译器
if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "clang++" CACHE STRING "C++ compiler" FORCE)
endif()


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -std=c++17 -g -rdynamic")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -latomic")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -latomic")

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
# include atomic_queue headers
include_directories(${CMAKE_SOURCE_DIR}/lib/atomic_queue/include)

set(LIBRARIES ${LIBRARIES} ibverbs pthread boost_thread boost_system hdr_histogram_static numa stdc++fs)


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
        ${CMAKE_SOURCE_DIR}/src/custom_src/agent_stat.cpp
        ${CMAKE_SOURCE_DIR}/src/custom_src/numautil.cpp
)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/HdrHistogram)

# Define library
add_library(galloc STATIC ${GAMSOURCE})
target_link_libraries(galloc ${LIBRARIES})

# Build libcuckoo
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src/.libs/libcityhash.a
    COMMAND cd ${CMAKE_SOURCE_DIR}/lib/libcuckoo && autoreconf -fis && ./configure && make
    DEPENDS ${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src/city.cc
    COMMENT "Building libcuckoo"
)
add_custom_target(libcuckoo DEPENDS ${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src/.libs/libcityhash.a)
add_dependencies(galloc libcuckoo)

# Database module sources
set(DATABASE_COMMON_SOURCES
    database/Meta.cpp
    database/txn/TransactionManager2PL.cpp
    database/txn/TransactionManagerST.cpp
    database/utils/Profiler.cpp
    database/tpcc/TpccRandomGenerator.cpp
)

# Database include directories
set(DATABASE_INCLUDES
    ${CMAKE_SOURCE_DIR}/database
    ${CMAKE_SOURCE_DIR}/database/bench
    ${CMAKE_SOURCE_DIR}/database/storage
    ${CMAKE_SOURCE_DIR}/database/txn
    ${CMAKE_SOURCE_DIR}/database/utils
    ${CMAKE_SOURCE_DIR}/database/tpcc
)

# TPCC executable
add_executable(tpcc
    database/tpcc/TpccBenchmarkMain.cpp
    ${DATABASE_COMMON_SOURCES}
)
target_include_directories(tpcc PRIVATE ${DATABASE_INCLUDES})
target_compile_definitions(tpcc PRIVATE LOCK BACKOFF)
target_link_libraries(tpcc galloc ${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src/.libs/libcityhash.a ${LIBRARIES})
add_dependencies(tpcc libcuckoo)

# Test executables
add_executable(tpcc_populate_test
    database/test/TpccPopulateTest.cpp
    ${DATABASE_COMMON_SOURCES}
)
target_include_directories(tpcc_populate_test PRIVATE ${DATABASE_INCLUDES})
target_compile_definitions(tpcc_populate_test PRIVATE LOCK BACKOFF)
target_link_libraries(tpcc_populate_test galloc ${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src/.libs/libcityhash.a ${LIBRARIES})
add_dependencies(tpcc_populate_test libcuckoo)

add_executable(hash_index_test
    database/test/HashIndexTest.cpp
    ${DATABASE_COMMON_SOURCES}
)
target_include_directories(hash_index_test PRIVATE ${DATABASE_INCLUDES})
target_compile_definitions(hash_index_test PRIVATE LOCK BACKOFF)
target_link_libraries(hash_index_test galloc ${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src/.libs/libcityhash.a ${LIBRARIES})
add_dependencies(hash_index_test libcuckoo)

# Create test programs from test/*.cc files
file(GLOB TEST_SOURCES "test/*.cc")
foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/custom_include
        ${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src
    )
    target_compile_definitions(${TEST_NAME} PRIVATE
        GALLOCLIB
        GANET
        LIBCUCKOO
    )
    target_link_libraries(${TEST_NAME} galloc ${CMAKE_SOURCE_DIR}/lib/libcuckoo/cityhash-1.1.1/src/.libs/libcityhash.a)
    add_dependencies(${TEST_NAME} libcuckoo)
endforeach()