cmake_minimum_required(VERSION 3.3)
project(cs3223)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    assign1/test_bufmgr/testcases/testcase0.c
    assign1/test_bufmgr/testcases/testcase1.c
    assign1/test_bufmgr/testcases/testcase2.c
    assign1/test_bufmgr/testcases/testcase3.c
    assign1/test_bufmgr/testcases/testcase4.c
    assign1/test_bufmgr/testcases/testcase5.c
    assign1/test_bufmgr/testcases/testcase6.c
    assign1/test_bufmgr/testcases/testcase7.c
    assign1/test_bufmgr/testcases/testcase8.c
    assign1/test_bufmgr/testcases/testcase9.c
    assign1/test_bufmgr/test_bufmgr.c
    assign1/bufmgr.c
    assign1/bufmgr.original.c
        assign1/freelist-lru-wh.c
        assign1/freelist-lru2.c
    assign1/freelist.original.c
    "assign1 2/test_bufmgr/testcases/testcase0.c"
    "assign1 2/test_bufmgr/testcases/testcase1.c"
    "assign1 2/test_bufmgr/testcases/testcase2.c"
    "assign1 2/test_bufmgr/testcases/testcase3.c"
    "assign1 2/test_bufmgr/testcases/testcase4.c"
    "assign1 2/test_bufmgr/testcases/testcase5.c"
    "assign1 2/test_bufmgr/testcases/testcase6.c"
    "assign1 2/test_bufmgr/testcases/testcase7.c"
    "assign1 2/test_bufmgr/testcases/testcase8.c"
    "assign1 2/test_bufmgr/testcases/testcase9.c"
    "assign1 2/test_bufmgr/test_bufmgr.c"
    "assign1 2/bufmgr.c"
    "assign1 2/bufmgr.original.c"
    "assign1 2/freelist-lru2.c"
    "assign1 2/freelist.original.c")

add_executable(cs3223 ${SOURCE_FILES})