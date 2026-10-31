# Applies the patches in PATCH_FILES ("|"-separated, applied in order) to the fetched Crow tree. Each patch is
# idempotent on its own: one that reverse-applies cleanly is already in place and is skipped, so re-configures never
# fail and a tree that has only the older patches simply gets the newer ones.
#   cmake "-DPATCH_FILES=<a.patch>|<b.patch>" -DSOURCE_DIR=<crow-src> -P apply_patch.cmake
find_package(Git REQUIRED)

# GIT_CEILING_DIRECTORIES keeps git from discovering an enclosing repository (build/ lives inside the
# project checkout), so `git apply` acts as a plain patch tool relative to SOURCE_DIR.
get_filename_component(_parent "${SOURCE_DIR}" DIRECTORY)
set(_git ${CMAKE_COMMAND} -E env GIT_CEILING_DIRECTORIES=${_parent} ${GIT_EXECUTABLE} apply --recount)

string(REPLACE "|" ";" _patches "${PATCH_FILES}")
foreach(_patch IN LISTS _patches)
    get_filename_component(_name "${_patch}" NAME)
    execute_process(
        COMMAND ${_git} --reverse --check "${_patch}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE _applied
        OUTPUT_QUIET ERROR_QUIET)
    if(_applied EQUAL 0)
        message(STATUS "Crow patch ${_name} already applied")
        continue()
    endif()

    execute_process(
        COMMAND ${_git} "${_patch}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE _rc
        ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Cannot apply ${_patch} to ${SOURCE_DIR}: ${_err}")
    endif()
    message(STATUS "Applied Crow patch ${_name}")
endforeach()
