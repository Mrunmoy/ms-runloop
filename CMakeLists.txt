cmake_minimum_required(VERSION 3.14)
project(rpc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Library ──────────────────────────────────────────────────────────
add_library(rpc
    src/RingBuffer.cpp
    src/RunLoop.cpp
    src/Platform.cpp
    src/FrameCodec.cpp
    src/Service.cpp
    src/Client.cpp
)
target_include_directories(rpc PUBLIC include)
target_link_libraries(rpc PRIVATE pthread)

# ── Google Test ──────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ── Tests ────────────────────────────────────────────────────────────
add_executable(rpc_tests
    tests/RingBufferTest.cpp
    tests/RunLoopTest.cpp
    tests/ServiceClientTest.cpp
)
target_link_libraries(rpc_tests PRIVATE rpc GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(rpc_tests)
