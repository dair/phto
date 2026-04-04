find_library(CPPUNIT_LIB cppunit)
find_path(CPPUNIT_INCLUDE cppunit/TestFixture.h)

if(CPPUNIT_LIB AND CPPUNIT_INCLUDE)
    add_executable(nef_validator_tests NefValidatorTest.cpp)
    target_include_directories(nef_validator_tests PRIVATE ${CPPUNIT_INCLUDE})
    target_link_libraries(nef_validator_tests PRIVATE nef_validator ${CPPUNIT_LIB})
    target_compile_definitions(nef_validator_tests PRIVATE
        NEF_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    add_test(NAME nef_validator_tests COMMAND nef_validator_tests)
else()
    message(STATUS "CppUnit not found — skipping nef_validator_tests")
endif()
