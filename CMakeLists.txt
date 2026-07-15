cmake_minimum_required(VERSION 3.16)
project(sdbg_traverse CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

include_directories(include)

# ── Tests: dependency-free (InMemoryOracle). Always built. ────────────────────
enable_testing()
add_executable(test_traversal tests/test_traversal.cpp)
add_test(NAME traversal COMMAND test_traversal)

# ── Production backend + benchmark over cosmo/VARI BOSS. Opt-in. ──────────────
# Needs cosmo + sdsl on the include/link path (POSIX). Point COSMO_INCLUDE_DIR
# and SDSL_* at your install, then: cmake -DSDBGT_WITH_COSMO=ON ...
option(SDBGT_WITH_COSMO "Build the cosmo/VARI BOSS backend and benchmark" OFF)
if(SDBGT_WITH_COSMO)
    # Verified: g++15 + xxsds/sdsl-lite (header-only) + cosmo headers, -std=c++17.
    set(COSMO_INCLUDE_DIR "" CACHE PATH "Path to cosmo headers (debruijn_graph.hpp)")
    set(SDSL_INCLUDE_DIR  "" CACHE PATH "Path to xxsds sdsl-lite include dir (e.g. ~/sdsl-lite/include)")
    add_executable(bench bench/bench_main.cpp)
    # NDEBUG is REQUIRED: strips a broken assert at cosmo debruijn_graph.hpp:143.
    target_compile_definitions(bench PRIVATE SDBGT_WITH_COSMO NDEBUG)
    target_include_directories(bench PRIVATE ${COSMO_INCLUDE_DIR} ${SDSL_INCLUDE_DIR} backends)
    # sdsl fork is header-only; no library link required for load+navigate.
endif()

# ── MEGAHIT SDBG backend (PRIMARY). Auto-enabled when the submodule is present. ─
# Vendored at libs/megahit (git submodule). Loading + navigating an SDBG needs
# only two .cpp; kmlib / parallel_hashmap / pprintpp are header-only.
set(MEGAHIT_SRC "${CMAKE_SOURCE_DIR}/libs/megahit/src")
if(EXISTS "${MEGAHIT_SRC}/sdbg/sdbg.h")
    message(STATUS "MEGAHIT SDBG backend: ENABLED (libs/megahit)")
    add_library(megahit_sdbg STATIC
        ${MEGAHIT_SRC}/sdbg/sdbg_meta.cpp
        ${MEGAHIT_SRC}/sdbg/sdbg_raw_content.cpp)
    target_include_directories(megahit_sdbg SYSTEM PUBLIC ${MEGAHIT_SRC})   # SYSTEM = silence 3rd-party warnings
    target_compile_options(megahit_sdbg PRIVATE -march=native -w)

    find_package(Threads REQUIRED)
    set(MH_TOOLS megahit_run:tests/megahit_run.cpp
                 megahit_bench:bench/megahit_bench.cpp
                 sdbg_report:bench/sdbg_report.cpp)
    foreach(pair ${MH_TOOLS})
        string(REPLACE ":" ";" kv ${pair})
        list(GET kv 0 tool)
        list(GET kv 1 src)
        add_executable(${tool} ${src})
        target_compile_definitions(${tool} PRIVATE SDBGT_WITH_MEGAHIT)
        target_compile_options(${tool} PRIVATE -march=native)
        target_include_directories(${tool} PRIVATE backends)
        target_link_libraries(${tool} PRIVATE megahit_sdbg Threads::Threads)
    endforeach()
    target_compile_definitions(sdbg_report PRIVATE _GNU_SOURCE)   # expose popen() regardless of -std mode
else()
    message(STATUS "MEGAHIT SDBG backend: DISABLED — run: git submodule update --init --recursive")
endif()
