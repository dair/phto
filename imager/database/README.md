# Database wrapper

This directory should contain the source for the simple SQLite-based database library

The language of development is C++ with C++23 standard. It is expected that clang compiler will be primarily used to compile the sources from this directory.
It is not expected to use anything besides the Standard library, including the STL, and the libraries explicitly stated in this README file: SQLite and CPPUnit.
Advice on other libraries before implementing anything.

CMake 4.2.3 should be used as the build system for the Library.

The database library should be multithread-ready: it is expected that multiple threads will read and write to the database simultaneously.

## Tables

The database should contain several tables:

### `file` table

Columns, all non-null:

* `id`, string, primary key
* `name`, string
* `size`, unsigned integer, capable to fit 64 bit
* `ext`, string

### `tag` table

Columns:

* `name`, string, primary key, not null

## `file_tag` table

Columns, all non-null:

* `file_id`: string, reference to `id` column in `file` table
* `tag_name`: string, reference to `name` column in `tag` table

## Public interface

The only public class this library should contain is `Database`;

It's only constructor is accepting the path to the database file; if it's not existing it tries to create a new empty database; if the creation fails, it should throw an exception with the specific error code and appropriate description.

If it's existing, it should try to open it for writing; if it fails, it should throw an exception with the specific error code (different from the creation exception) and appropriate description.

Class should provide the appropriate methods to work with the database:

* Add file
* Delete file
* Edit file name
* Add tag
* Delete tag
* Bind tag to file
* Unbind tag from file
* Get all tags for a specific file; possibly with pagination, advice the details on this
* Get all files for a specific tag/set of tags; possibly with pagination, advice the details on this

Advice more possible public methods before implementing.

This directory contains the SQLite library (in `sqlite/src` subdirectory) to work with specific database. 
This `sqlite/src` subdirectory is strictly readonly and should not be changed in the process of development.

It is expected that the Library is being linked with this specific version of the SQLite library.

The SQLite library should be compiled with the separate CMakeLists.txt file that should be placed inside `sqlite` directory. 

## Unit-testing

in the `test` subdirectory there should be tests, using the CPPUNIT testing library (expected to be linked from the outside) that validate the database workflow, including the multithreading work under heavy load and race conditions prevented.
