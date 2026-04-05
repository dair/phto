find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(CPPUNIT cppunit)
endif()

if(NOT CPPUNIT_FOUND)
    find_path(CPPUNIT_INCLUDE_DIRS cppunit/TestCase.h)
    find_library(CPPUNIT_LIBRARIES NAMES cppunit)
    if(CPPUNIT_INCLUDE_DIRS AND CPPUNIT_LIBRARIES)
        set(CPPUNIT_FOUND TRUE)
    endif()
endif()

if(NOT CPPUNIT_FOUND)
    message(FATAL_ERROR "CppUnit not found")
endif()

add_executable(test_jpeg_validator test_jpeg_validator.cpp)

target_include_directories(test_jpeg_validator PRIVATE ${CPPUNIT_INCLUDE_DIRS})
target_link_libraries(test_jpeg_validator PRIVATE jpeg_validator ${CPPUNIT_LIBRARIES})

# Expose the path to the bundled test JPEG so the test can load it at runtime.
target_compile_definitions(test_jpeg_validator PRIVATE
    "TEST_JPEG_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/../libjpeg/src/testimg.jpg\""
)

include(CTest)
add_test(NAME jpeg_validator_tests COMMAND test_jpeg_validator)
