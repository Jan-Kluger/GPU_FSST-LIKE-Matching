cmake_minimum_required(VERSION 3.5)
project(fsst-like-matching)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_VERBOSE_MAKEFILE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    add_compile_options("-march=native")
endif()

if(NOT MSVC)
    add_compile_options("$<$<CONFIG:Release>:-O3;-DNDEBUG>")
else()
    add_compile_options("$<$<CONFIG:Release>:/O3;/DNDEBUG>")
endif()

include(FetchContent)
FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG        12.1.0
)
FetchContent_Declare(
        fsst_external
        GIT_REPOSITORY https://github.com/calin2110/fsst.git
        GIT_TAG        master
)
FetchContent_MakeAvailable(fmt fsst_external)

set(FSST_INCLUDE_DIR "${CMAKE_BINARY_DIR}/dependencies/include")
file(MAKE_DIRECTORY "${FSST_INCLUDE_DIR}")
if(NOT EXISTS "${FSST_INCLUDE_DIR}/fsst")
    file(CREATE_LINK "${fsst_external_SOURCE_DIR}" "${FSST_INCLUDE_DIR}/fsst" SYMBOLIC)
endif()
target_include_directories(fsst INTERFACE "${FSST_INCLUDE_DIR}")


find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(VECTORSCAN IMPORTED_TARGET libhs)
endif()

if(NOT DEFINED LLVM_DIR AND EXISTS "/usr/lib/llvm-16/lib/cmake/llvm")
    set(LLVM_DIR "/usr/lib/llvm-16/lib/cmake/llvm")
endif()
find_package(LLVM 16 CONFIG)

if(LLVM_FOUND)
    message(STATUS "LLVM found. Enabling server.")

    llvm_map_components_to_libnames(llvm_libs core orcjit native nativecodegen)
    add_library(core_base STATIC
            src/automata.cpp
            src/percentage.cpp
            src/common.cpp
            src/like_pattern_automaton.cpp
            src/encoder.cpp
            src/pattern.cpp
            src/utils.cpp
            src/serializer.cpp
            src/codegen/codegen.cpp
            src/codegen/cppcodegen.cpp
            src/codegen/llvmcodegen.cpp
    )
    target_include_directories(core_base PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(core_base PUBLIC fsst fmt ${llvm_libs})
    target_include_directories(core_base PUBLIC ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(core_base PUBLIC ${LLVM_DEFINITIONS})
    add_subdirectory(fa-drawing)

    if(VECTORSCAN_FOUND)
        message(STATUS "Vectorscan found. Enabling extended features.")
        add_library(core_extended STATIC
                src/vectorscan.cpp
        )
        target_link_libraries(core_extended PUBLIC core_base PkgConfig::VECTORSCAN)


        add_subdirectory(benchmark)
        add_subdirectory(test)
    else()
        message(STATUS "Vectorscan not found. Benchmarks and tests will be unavailable")
    endif()
else()
    message(WARNING "LLVM 16 not found. All executables are unavailable.")
endif()

