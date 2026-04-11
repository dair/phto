# Compiler auto-selection: prefer clang++ when available, fall back to g++.
# This file is injected before project() via CMAKE_PROJECT_TOP_LEVEL_INCLUDES
# so it runs at the only point where CMAKE_CXX_COMPILER can be set safely.
#
# Users can always override with:
#   cmake -DCMAKE_CXX_COMPILER=g++ ...
# or by setting CXX in the environment before invoking cmake.

if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
    find_program(CLANGPP_EXECUTABLE NAMES clang++ DOC "clang++ compiler")
    find_program(GPP_EXECUTABLE     NAMES g++     DOC "g++ compiler")

    if(CLANGPP_EXECUTABLE)
        set(CMAKE_CXX_COMPILER "${CLANGPP_EXECUTABLE}" CACHE STRING "C++ compiler" FORCE)
        message(STATUS "SelectCompiler: using clang++ (${CLANGPP_EXECUTABLE})")
    elseif(GPP_EXECUTABLE)
        set(CMAKE_CXX_COMPILER "${GPP_EXECUTABLE}" CACHE STRING "C++ compiler" FORCE)
        message(STATUS "SelectCompiler: clang++ not found, falling back to g++ (${GPP_EXECUTABLE})")
    else()
        message(WARNING "SelectCompiler: neither clang++ nor g++ found; CMake will use its default compiler")
    endif()
endif()
