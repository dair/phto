# Applies third_party/crow/crow-body-sink.patch to the fetched Crow tree. Idempotent: an already
# patched tree (reverse-applies cleanly) is left alone, so re-configures never fail.
#   cmake -DPATCH_FILE=<patch> -DSOURCE_DIR=<crow-src> -P apply_patch.cmake
find_package(Git REQUIRED)

# GIT_CEILING_DIRECTORIES keeps git from discovering an enclosing repository (build/ lives inside the
# project checkout), so `git apply` acts as a plain patch tool relative to SOURCE_DIR.
get_filename_component(_parent "${SOURCE_DIR}" DIRECTORY)
set(_git ${CMAKE_COMMAND} -E env GIT_CEILING_DIRECTORIES=${_parent} ${GIT_EXECUTABLE} apply --recount)

execute_process(
    COMMAND ${_git} --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _applied
    OUTPUT_QUIET ERROR_QUIET)
if(_applied EQUAL 0)
    message(STATUS "Crow body-sink patch already applied")
    return()
endif()

execute_process(
    COMMAND ${_git} "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE _rc
    ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Cannot apply ${PATCH_FILE} to ${SOURCE_DIR}: ${_err}")
endif()
message(STATUS "Applied Crow body-sink patch")
