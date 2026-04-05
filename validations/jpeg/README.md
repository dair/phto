# JPEG image validation

This directory should contain the source for JPEG image validation library (Library).

The language of development is C++ with C++23 standard. It is expected that clang compiler will be primarily used to compile the sources from this directory.

CMake 4.2.3 should be used as the build system for the Library.

The only function that this directory should contain is the following:

```
enum ValidationResult {
    INVALID = -1, // the input is the JPEG data that is actually broken
    VALID = 0, // the input is the valid JPEG data
    WRONG = 1, // the input is not JPEG data at all
};

ValidationResult validateJpeg(const void* data, size_t dataSize);
```

This directory contains the LibJPEG library (in `libjpeg/src` subdirectory) to handle the JPEG image data.
This `libjpeg/src` subdirectory is strictly readonly and should not be changed in the process of development.

It is expected that the Library is being linked with this specific version of the LibJPEG library.

The LibJPEG library should be compiled with the separate CMakeLists.txt file that should be placed inside libjpeg directory. 

The LIbrary should have the .h header file that is ready to be included from outer software that would use the Library.


## Unit-testing

in the `test` subdirectory there should be tests, using the CPPUNIT testing library (expected to be linked from the outside) that validate the vlidation function with different input, ensuring the correctness of the function work.
