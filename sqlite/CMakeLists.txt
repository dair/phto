cmake_minimum_required(VERSION 3.28)
project(SQLite3 LANGUAGES C)

set(SQLITE_TOP     "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SQLITE_SRC_DIR "${SQLITE_TOP}/src")
set(SQLITE_TOOL_DIR "${SQLITE_TOP}/tool")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")

file(MAKE_DIRECTORY "${GEN_DIR}")

find_program(TCLSH tclsh REQUIRED)

# ---- Generate sqlite3.h ----
# mksqlite3h.tcl needs 'mksourceid' binary in its working directory (GEN_DIR).
if(NOT EXISTS "${GEN_DIR}/sqlite3.h")
    # Compile mksourceid tool
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -o "${GEN_DIR}/mksourceid"
                "${SQLITE_TOOL_DIR}/mksourceid.c"
        RESULT_VARIABLE rc ERROR_VARIABLE err
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to compile mksourceid: ${err}")
    endif()

    execute_process(
        COMMAND "${TCLSH}" "${SQLITE_TOOL_DIR}/mksqlite3h.tcl" "${SQLITE_TOP}"
                -o "${GEN_DIR}/sqlite3.h"
        WORKING_DIRECTORY "${GEN_DIR}"
        RESULT_VARIABLE rc
        ERROR_VARIABLE  err
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to generate sqlite3.h: ${err}")
    endif()
endif()

# ---- Build lemon + generate parse.c / parse.h ----
if(NOT EXISTS "${GEN_DIR}/parse.c")
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -o "${GEN_DIR}/lemon" "${SQLITE_TOOL_DIR}/lemon.c"
        RESULT_VARIABLE rc ERROR_VARIABLE err
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to compile lemon: ${err}")
    endif()

    file(COPY "${SQLITE_SRC_DIR}/parse.y"    DESTINATION "${GEN_DIR}")
    file(COPY "${SQLITE_TOOL_DIR}/lempar.c"  DESTINATION "${GEN_DIR}")

    execute_process(
        COMMAND "${GEN_DIR}/lemon" "${GEN_DIR}/parse.y"
        WORKING_DIRECTORY "${GEN_DIR}"
        RESULT_VARIABLE rc ERROR_VARIABLE err
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to run lemon on parse.y: ${err}")
    endif()
endif()

# ---- Build mkkeywordhash + generate keywordhash.h ----
if(NOT EXISTS "${GEN_DIR}/keywordhash.h")
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}"
                -o "${GEN_DIR}/mkkeywordhash"
                "${SQLITE_TOOL_DIR}/mkkeywordhash.c"
        RESULT_VARIABLE rc ERROR_VARIABLE err
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to compile mkkeywordhash: ${err}")
    endif()

    execute_process(
        COMMAND "${GEN_DIR}/mkkeywordhash"
        OUTPUT_FILE "${GEN_DIR}/keywordhash.h"
        RESULT_VARIABLE rc ERROR_VARIABLE err
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to generate keywordhash.h: ${err}")
    endif()
endif()

# ---- Generate pragma.h ----
if(NOT EXISTS "${GEN_DIR}/pragma.h")
    execute_process(
        COMMAND "${TCLSH}" "${SQLITE_TOOL_DIR}/mkpragmatab.tcl" "${GEN_DIR}/pragma.h"
        RESULT_VARIABLE rc ERROR_VARIABLE err
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to generate pragma.h: ${err}")
    endif()
endif()

# ---- Generate opcodes.h ----
if(NOT EXISTS "${GEN_DIR}/opcodes.h")
    execute_process(
        COMMAND sh -c
            "cat '${GEN_DIR}/parse.h' '${SQLITE_SRC_DIR}/vdbe.c' | '${TCLSH}' '${SQLITE_TOOL_DIR}/mkopcodeh.tcl'"
        OUTPUT_FILE "${GEN_DIR}/opcodes.h"
        RESULT_VARIABLE rc ERROR_VARIABLE err
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to generate opcodes.h: ${err}")
    endif()
endif()

# ---- Generate opcodes.c ----
if(NOT EXISTS "${GEN_DIR}/opcodes.c")
    execute_process(
        COMMAND sh -c
            "'${TCLSH}' '${SQLITE_TOOL_DIR}/mkopcodec.tcl' '${GEN_DIR}/opcodes.h'"
        OUTPUT_FILE "${GEN_DIR}/opcodes.c"
        RESULT_VARIABLE rc ERROR_VARIABLE err
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to generate opcodes.c: ${err}")
    endif()
endif()

# ---- Generate ctime.c ----
if(NOT EXISTS "${GEN_DIR}/ctime.c")
    execute_process(
        COMMAND "${TCLSH}" "${SQLITE_TOOL_DIR}/mkctimec.tcl" "${GEN_DIR}/ctime.c"
        RESULT_VARIABLE rc ERROR_VARIABLE err
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to generate ctime.c: ${err}")
    endif()
endif()

# ---- Copy sqlite3ext.h so consumers can include it ----
if(NOT EXISTS "${GEN_DIR}/sqlite3ext.h")
    file(COPY "${SQLITE_SRC_DIR}/sqlite3ext.h" DESTINATION "${GEN_DIR}")
endif()

# ---- Core source files ----
set(SQLITE_SOURCES
    "${SQLITE_SRC_DIR}/alter.c"
    "${SQLITE_SRC_DIR}/analyze.c"
    "${SQLITE_SRC_DIR}/attach.c"
    "${SQLITE_SRC_DIR}/auth.c"
    "${SQLITE_SRC_DIR}/backup.c"
    "${SQLITE_SRC_DIR}/bitvec.c"
    "${SQLITE_SRC_DIR}/btmutex.c"
    "${SQLITE_SRC_DIR}/btree.c"
    "${SQLITE_SRC_DIR}/build.c"
    "${SQLITE_SRC_DIR}/callback.c"
    "${SQLITE_SRC_DIR}/carray.c"
    "${SQLITE_SRC_DIR}/complete.c"
    "${SQLITE_SRC_DIR}/date.c"
    "${SQLITE_SRC_DIR}/dbpage.c"
    "${SQLITE_SRC_DIR}/dbstat.c"
    "${SQLITE_SRC_DIR}/delete.c"
    "${SQLITE_SRC_DIR}/expr.c"
    "${SQLITE_SRC_DIR}/fault.c"
    "${SQLITE_SRC_DIR}/fkey.c"
    "${SQLITE_SRC_DIR}/func.c"
    "${SQLITE_SRC_DIR}/global.c"
    "${SQLITE_SRC_DIR}/hash.c"
    "${SQLITE_SRC_DIR}/insert.c"
    "${SQLITE_SRC_DIR}/json.c"
    "${SQLITE_SRC_DIR}/legacy.c"
    "${SQLITE_SRC_DIR}/loadext.c"
    "${SQLITE_SRC_DIR}/main.c"
    "${SQLITE_SRC_DIR}/malloc.c"
    "${SQLITE_SRC_DIR}/mem0.c"
    "${SQLITE_SRC_DIR}/mem1.c"
    "${SQLITE_SRC_DIR}/mem2.c"
    "${SQLITE_SRC_DIR}/mem3.c"
    "${SQLITE_SRC_DIR}/mem5.c"
    "${SQLITE_SRC_DIR}/memdb.c"
    "${SQLITE_SRC_DIR}/memjournal.c"
    "${SQLITE_SRC_DIR}/mutex.c"
    "${SQLITE_SRC_DIR}/mutex_noop.c"
    "${SQLITE_SRC_DIR}/notify.c"
    "${SQLITE_SRC_DIR}/os.c"
    "${SQLITE_SRC_DIR}/os_kv.c"
    "${SQLITE_SRC_DIR}/pager.c"
    "${SQLITE_SRC_DIR}/pcache.c"
    "${SQLITE_SRC_DIR}/pcache1.c"
    "${SQLITE_SRC_DIR}/pragma.c"
    "${SQLITE_SRC_DIR}/prepare.c"
    "${SQLITE_SRC_DIR}/printf.c"
    "${SQLITE_SRC_DIR}/random.c"
    "${SQLITE_SRC_DIR}/resolve.c"
    "${SQLITE_SRC_DIR}/rowset.c"
    "${SQLITE_SRC_DIR}/select.c"
    "${SQLITE_SRC_DIR}/status.c"
    "${SQLITE_SRC_DIR}/table.c"
    "${SQLITE_SRC_DIR}/threads.c"
    "${SQLITE_SRC_DIR}/tokenize.c"
    "${SQLITE_SRC_DIR}/treeview.c"
    "${SQLITE_SRC_DIR}/trigger.c"
    "${SQLITE_SRC_DIR}/update.c"
    "${SQLITE_SRC_DIR}/upsert.c"
    "${SQLITE_SRC_DIR}/utf.c"
    "${SQLITE_SRC_DIR}/util.c"
    "${SQLITE_SRC_DIR}/vacuum.c"
    "${SQLITE_SRC_DIR}/vdbe.c"
    "${SQLITE_SRC_DIR}/vdbeapi.c"
    "${SQLITE_SRC_DIR}/vdbeaux.c"
    "${SQLITE_SRC_DIR}/vdbeblob.c"
    "${SQLITE_SRC_DIR}/vdbemem.c"
    "${SQLITE_SRC_DIR}/vdbesort.c"
    "${SQLITE_SRC_DIR}/vdbetrace.c"
    "${SQLITE_SRC_DIR}/vdbevtab.c"
    "${SQLITE_SRC_DIR}/vtab.c"
    "${SQLITE_SRC_DIR}/wal.c"
    "${SQLITE_SRC_DIR}/walker.c"
    "${SQLITE_SRC_DIR}/where.c"
    "${SQLITE_SRC_DIR}/wherecode.c"
    "${SQLITE_SRC_DIR}/whereexpr.c"
    "${SQLITE_SRC_DIR}/window.c"
    # Generated files
    "${GEN_DIR}/parse.c"
    "${GEN_DIR}/opcodes.c"
    "${GEN_DIR}/ctime.c"
)

if(UNIX)
    list(APPEND SQLITE_SOURCES
        "${SQLITE_SRC_DIR}/mutex_unix.c"
        "${SQLITE_SRC_DIR}/os_unix.c"
    )
elseif(WIN32)
    list(APPEND SQLITE_SOURCES
        "${SQLITE_SRC_DIR}/mutex_w32.c"
        "${SQLITE_SRC_DIR}/os_win.c"
    )
endif()

add_library(sqlite3_lib STATIC ${SQLITE_SOURCES})

target_include_directories(sqlite3_lib
    PUBLIC
        "${GEN_DIR}"        # sqlite3.h (public API header)
    PRIVATE
        "${SQLITE_SRC_DIR}" # sqliteInt.h and other internal headers
        "${GEN_DIR}"        # generated internal headers (parse.h, keywordhash.h, etc.)
)

target_compile_definitions(sqlite3_lib
    PUBLIC
        SQLITE_THREADSAFE=1
    PRIVATE
        SQLITE_CORE
        SQLITE_PRIVATE=static
)

# Silence all warnings in bundled third-party code
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(sqlite3_lib PRIVATE -w)
endif()
