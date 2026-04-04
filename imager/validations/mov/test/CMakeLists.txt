find_library(CPPUNIT_LIB cppunit)
find_path(CPPUNIT_INCLUDE cppunit/TestFixture.h)

if(CPPUNIT_LIB AND CPPUNIT_INCLUDE)
    add_executable(mov_validator_tests MovValidatorTest.cpp)
    target_include_directories(mov_validator_tests PRIVATE ${CPPUNIT_INCLUDE})
    target_link_libraries(mov_validator_tests PRIVATE mov_validator ${CPPUNIT_LIB})
    target_compile_definitions(mov_validator_tests PRIVATE
        MOV_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    add_test(NAME mov_validator_tests COMMAND mov_validator_tests)
else()
    message(STATUS "CppUnit not found — skipping mov_validator_tests")
endif()
