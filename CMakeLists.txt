cmake_minimum_required (VERSION 2.6)
project (lthread)
SET(CMAKE_BUILD_TYPE release)
set (CMAKE_CXX_STANDARD 17)
SET (CMAKE_C_FLAGS_RELEASE "-Werror -g -Wall")

set(LTHREAD_VERSION_MAJOR 1)
set(LTHREAD_VERSION_MINOR 0)
set(LTHREAD_VERSION_PATCH 0)
set(LTHREAD_VERSION_STRING ${LTHREAD_VERSION_MAJOR}.${LTHREAD_VERSION_MINOR}.${LTHREAD_VERSION_PATCH})

set(lthread_files
    src/lthread.c
    src/lthread_socket.c
    src/lthread_poller.c
    src/lthread_cond.c
    src/libcontext.c
)

add_library(lthread  ${lthread_files})

install(TARGETS lthread DESTINATION lib)
install(FILES src/lthread.h DESTINATION include)

set(UNIT_TEST_PATH ${CMAKE_SOURCE_DIR}/tests)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${UNIT_TEST_PATH})
include_directories(src)

add_executable(lthread_sleep tests/lthread_sleep.c)
target_link_libraries(lthread_sleep lthread pthread)

add_executable(lthread_join tests/lthread_join.c)
target_link_libraries(lthread_join lthread pthread)

add_executable(bench tests/bench.cpp)
target_link_libraries(bench lthread pthread)

add_executable(test_poller tests/test_poller.c)
target_link_libraries(test_poller lthread pthread)

enable_testing()
add_test(lthread_sleep ${UNIT_TEST_PATH}/lthread_sleep)
add_test(lthread_join ${UNIT_TEST_PATH}/lthread_join)
