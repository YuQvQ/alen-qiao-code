# tests/CMakeLists.txt — GoogleTest 集成
#
# 用 FetchContent 拉取 GoogleTest（首次配置时联网）
# 离线场景：设置 -DVISION_ALGO_BUILD_TESTS=OFF 跳过
#
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(googletest)

add_executable(vision_algo_tests
    test_template_match.cpp
)

target_link_libraries(vision_algo_tests PRIVATE
    vision_algo
    GTest::gtest_main
)

target_include_directories(vision_algo_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# 复制 DLL 到测试目录以便运行
add_custom_command(TARGET vision_algo_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:vision_algo>
        $<TARGET_FILE_DIR:vision_algo_tests>
)

include(GoogleTest)
gtest_discover_tests(vision_algo_tests)
