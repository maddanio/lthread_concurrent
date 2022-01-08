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
target_link_libraries(
    lthread
    PRIVATE
    pthread
)
install(TARGETS lthread DESTINATION lib)
install(FILES src/lthread.h DESTINATION include)

include_directories(src)

add_executable(lthread_sleep tests/lthread_sleep.c)
target_link_libraries(lthread_sleep lthread)

add_executable(lthread_join tests/lthread_join.c)
target_link_libraries(lthread_join lthread)

add_executable(bench tests/bench.cpp)
target_link_libraries(bench lthread)

#add_executable(test_poller tests/test_poller.c)
#target_link_libraries(test_poller lthread)

add_executable(udp_client examples/udp_client.c)
target_link_libraries(udp_client lthread)

add_executable(udp_server examples/udp_server.c)
target_link_libraries(udp_server lthread)

add_executable(webserver examples/webserver.c)
target_link_libraries(webserver lthread)

enable_testing()
add_test(lthread_sleep lthread_sleep)
add_test(lthread_join lthread_join)
add_test(lthread_join bench)
#add_test(lthread_join ${UNIT_TEST_PATH}/test_poller)
