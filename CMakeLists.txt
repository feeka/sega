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
