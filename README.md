# PNG image validation

This directory should contain the source for PNG image validation library (Library).

The language of development is C++ with C++23 standard. It is expected that clang compiler will be primarily used to compile the sources from this directory.

CMake 4.2.3 should be used as the build system for the Library.

The only function that this directory should contain is the following:

```
enum ValidationResult {
    INVALID = -1, // the input is the PNG data that is actually broken
    VALID = 0, // the input is the valid PNG data
    WRONG = 1, // the input is not PNG data at all
};

ValidationResult validatePng(const void* data, size_t dataSize);
```

This directory contains the LibPNG library (in `libpng/src` subdirectory) to handle the PNG image data.
This `libpng/src` subdirectory is strictly readonly and should not be changed in the process of development.

It is expected that the Library is being linked with this specific version of the LibPNG library.

The LibPNG library should be compiled with the already existing CMakeLists.txt file that is placed inside libpng/src directory. 

The Library should have the .h header file that is ready to be included from outer software that would use the Library.


## Unit-testing

in the `test` subdirectory there should be tests, using the CPPUNIT testing library (expected to be linked from the outside) that validate the vlidation function with different input, ensuring the correctness of the function work.

## Sample utility

The `sample` subdirectory contains a command-line utility `png_validate` that demonstrates use of the Library.

### Usage

```
png_validate <file>
```

The utility accepts a single file path argument, reads the file, validates its contents with `validatePng`, and prints the result to stdout in the form:

```
<file>: VALID
```

The result word is one of `VALID`, `INVALID`, or `WRONG`, matching the `ValidationResult` enum.
