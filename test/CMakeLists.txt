# GoogleTest
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        f8d7d77c06936315286eb55f8de22cd23c188571
)
FetchContent_MakeAvailable(googletest)

set(TEST_CC
        "StartPatternMatchingTest.cpp"
        "StartPatternBuildingTest.cpp"
        "MiddlePatternMatchingTest.cpp"
        "PfacMiddlePatternMatchingTest.cpp"
        "EndPatternMatchingTest.cpp"
        "EndPatternBuildingTest.cpp"
        "FullPatternMatchingTest.cpp"
        "CppCompiledPatternMatchingTest.cpp"
        "LLVMCompiledPatternMatchingTest.cpp"
        "DecodePatternMatchingTest.cpp"
)

set(TEST_ENV
        "tester.cpp"
        "env.cpp"
        "shared.cpp"
)

add_executable(tester ${TEST_ENV} ${TEST_CC})

set(SANITIZER_FLAGS "-fsanitize=address,undefined,leak" "-fno-omit-frame-pointer")
target_compile_options(tester PRIVATE
        $<$<CONFIG:RelWithDebInfo>:${SANITIZER_FLAGS}>
        $<$<CONFIG:Debug>:${SANITIZER_FLAGS}>
)
target_link_options(tester PRIVATE
        $<$<CONFIG:RelWithDebInfo>:${SANITIZER_FLAGS}>
        $<$<CONFIG:Debug>:${SANITIZER_FLAGS}>
)

target_link_libraries(tester
        PRIVATE
        core_extended
        gtest_main
        gmock
        PkgConfig::VECTORSCAN
        ${llvm_libs}
)

enable_testing()
add_test(NAME like_tests COMMAND tester)
