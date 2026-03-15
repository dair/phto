find_path(CPPUNIT_INCLUDE_DIR cppunit/TestCase.h)
find_library(CPPUNIT_LIBRARY NAMES cppunit)

if(NOT CPPUNIT_INCLUDE_DIR OR NOT CPPUNIT_LIBRARY)
    message(FATAL_ERROR "CppUnit not found. Install it (e.g. brew install cppunit).")
endif()

add_executable(test_validate_png test_validate_png.cpp)
target_include_directories(test_validate_png PRIVATE ${CPPUNIT_INCLUDE_DIR})
target_link_libraries(test_validate_png
    PRIVATE
        png_validation
        png_static
        ${CPPUNIT_LIBRARY})

add_test(NAME test_validate_png COMMAND test_validate_png)
