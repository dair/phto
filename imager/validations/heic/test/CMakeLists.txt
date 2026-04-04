find_library(CPPUNIT_LIB cppunit)
find_path(CPPUNIT_INCLUDE cppunit/TestFixture.h)

if(CPPUNIT_LIB AND CPPUNIT_INCLUDE)
    add_executable(heic_validator_tests HeicValidatorTest.cpp)
    target_include_directories(heic_validator_tests PRIVATE ${CPPUNIT_INCLUDE})
    target_link_libraries(heic_validator_tests PRIVATE heic_validator heif ${CPPUNIT_LIB})
    target_compile_definitions(heic_validator_tests PRIVATE
        HEIC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    add_test(NAME heic_validator_tests COMMAND heic_validator_tests)
else()
    message(STATUS "CppUnit not found — skipping heic_validator_tests")
endif()
