# server/test/CMakeLists.txt — E1 smoke test: GET /health + graceful SIGTERM
#
# Mirrors the configure_file + add_test shell-test pattern from
# imagestore/CMakeLists.txt and imageradmin/CMakeLists.txt.

cmake_minimum_required(VERSION 3.28)

# Guard: skip if curl is not present (the script also guards itself at runtime).
find_program(CURL_PROGRAM curl)
if(NOT CURL_PROGRAM)
    message(STATUS "curl not found — server_health_test will self-skip at runtime")
endif()

# configure_file does NOT evaluate generator expressions, so we inject the
# binary path via the known output location (same pattern imagestore uses:
# "${CMAKE_BINARY_DIR}/imagestore/imagestore").
set(IMAGERD_BIN "${CMAKE_BINARY_DIR}/server/imagerd")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_health.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_health.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_health.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_health_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_health.sh"
)

# Allow enough time for daemon startup + poll + shutdown.
set_tests_properties(server_health_test PROPERTIES TIMEOUT 60)

# ---- server_auth_test — F1 Bearer-token auth end-to-end (shell script) ----

set(IMAGERADMIN_BIN "${CMAKE_BINARY_DIR}/imageradmin/imageradmin")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_auth.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_auth.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_auth.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_auth_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_auth.sh"
)

# Allow time for daemon startup + all auth round-trips + shutdown.
set_tests_properties(server_auth_test PROPERTIES TIMEOUT 90)

# ---- server_authz_test — F2 requireAdmin/GET /users, throttle, /auth/password ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_authz.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_authz.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_authz.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_authz_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_authz.sh"
)

# Allow time for daemon startup, 5 throttle requests, all assertion round-trips + shutdown.
set_tests_properties(server_authz_test PROPERTIES TIMEOUT 120)

# ---- server_json_tests — CPPUnit tests for server/Json.{h,cpp} (E2) --------

find_package(PkgConfig QUIET)

set(CPPUNIT_FOUND FALSE)

if(PkgConfig_FOUND)
    pkg_check_modules(CPPUNIT QUIET cppunit)
    if(CPPUNIT_FOUND)
        set(CPPUNIT_INCLUDE_DIRS ${CPPUNIT_INCLUDE_DIRS})
        set(CPPUNIT_LIBRARIES    ${CPPUNIT_LIBRARIES})
    endif()
endif()

if(NOT CPPUNIT_FOUND)
    find_library(CPPUNIT_LIB NAMES cppunit)
    find_path(CPPUNIT_INCLUDE_DIR cppunit/TestFixture.h)
    if(CPPUNIT_LIB AND CPPUNIT_INCLUDE_DIR)
        set(CPPUNIT_FOUND TRUE)
        set(CPPUNIT_LIBRARIES    ${CPPUNIT_LIB})
        set(CPPUNIT_INCLUDE_DIRS ${CPPUNIT_INCLUDE_DIR})
    endif()
endif()

if(NOT CPPUNIT_FOUND)
    message(STATUS "CppUnit not found — skipping server_json_tests target")
else()
    add_executable(server_json_tests "${CMAKE_CURRENT_SOURCE_DIR}/JsonTest.cpp")

    target_include_directories(server_json_tests
        PRIVATE ${CPPUNIT_INCLUDE_DIRS}
    )

    target_link_libraries(server_json_tests
        PRIVATE
            server_lib
            ${CPPUNIT_LIBRARIES}
    )

    include(CTest)
    add_test(NAME server_json_tests COMMAND server_json_tests)

    # ClientIp: trusted-proxy CIDR matching + X-Forwarded-For resolution
    add_executable(server_clientip_tests "${CMAKE_CURRENT_SOURCE_DIR}/ClientIpTest.cpp")
    target_include_directories(server_clientip_tests PRIVATE ${CPPUNIT_INCLUDE_DIRS})
    target_link_libraries(server_clientip_tests PRIVATE server_lib ${CPPUNIT_LIBRARIES})
    add_test(NAME server_clientip_tests COMMAND server_clientip_tests)
endif()

# ---- server_items_test — G1 PUT /items + GET /items/{id} (shell script) ----

set(JPEG_FIXTURE "${CMAKE_SOURCE_DIR}/validations/jpeg/test/testimg.jpg")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_items.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_items.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_items.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_items_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_items.sh"
)

set_tests_properties(server_items_test PROPERTIES TIMEOUT 90)

# ---- server_content_test — G3 GET /items/{id}/content (Range/ETag) + DELETE (shell script) ----

set(MOV_FIXTURE "${CMAKE_SOURCE_DIR}/validations/mov/test/fixtures/valid.mov")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_content.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_content.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_content.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_content_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_content.sh"
)

set_tests_properties(server_content_test PROPERTIES TIMEOUT 90)

# ---- server_list_test — G2 GET /items listing (filters, untagged, pagination) ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_list.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_list.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_list.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_list_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_list.sh"
)

set_tests_properties(server_list_test PROPERTIES TIMEOUT 120)

# ---- server_item_tags_test — H1 per-item tag routes (GET/PUT/POST/DELETE) ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_item_tags.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_item_tags.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_item_tags.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_item_tags_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_item_tags.sh"
)

set_tests_properties(server_item_tags_test PROPERTIES TIMEOUT 120)

# ---- server_tags_test — H2 global tag routes (GET/POST /tags, DELETE /tags/{name}) ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_tags.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_tags.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_tags.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_tags_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_tags.sh"
)

set_tests_properties(server_tags_test PROPERTIES TIMEOUT 120)

# ---- server_users_test — I1 admin user management (/users CRUD + last-admin guard) ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_users.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_users.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_users.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_users_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_users.sh"
)

set_tests_properties(server_users_test PROPERTIES TIMEOUT 120)

# ---- server_hardening_test — J1 headers, /stats, validation, body cap, throttle ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_hardening.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_hardening.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_hardening.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_hardening_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_hardening.sh"
)

set_tests_properties(server_hardening_test PROPERTIES TIMEOUT 180)

# ---- server_tls_test — J2 native TLS smoke test (self-signed cert via openssl CLI) ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_tls.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_tls.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_tls.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_tls_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_tls.sh"
)

# Exit 77 = openssl/curl missing → ctest reports "Skipped".
set_tests_properties(server_tls_test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# ---- server_api_test — J3 full end-to-end API journey (two targets, plain HTTP) ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_api.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_api.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_api.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_api_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_api.sh"
)

set_tests_properties(server_api_test PROPERTIES TIMEOUT 120)

# ---- server_fixups_test — post-0022 fix-ups: startup errors, SIGTERM drain, /auth/password throttle ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_fixups.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_fixups.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_fixups.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_fixups_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_fixups.sh"
)

set_tests_properties(server_fixups_test PROPERTIES TIMEOUT 240)

# ---- server_loose_ends_test — bounded uploads (413/spool), trusted proxies/XFF, access log, timing knobs ----

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_loose_ends.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_loose_ends.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_loose_ends.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_loose_ends_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_loose_ends.sh"
)

set_tests_properties(server_loose_ends_test PROPERTIES TIMEOUT 300)
