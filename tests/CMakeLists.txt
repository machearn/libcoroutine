include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest
  GIT_TAG        v1.14.0
)

FetchContent_MakeAvailable(googletest)

file(GLOB_RECURSE TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/*.cpp")
file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_executable(coro_tests ${TEST_SOURCES} ${SOURCES})

target_link_libraries(coro_tests GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(coro_tests)
