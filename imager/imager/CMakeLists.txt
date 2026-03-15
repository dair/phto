cmake_minimum_required(VERSION 3.28)

add_library(libimager
    src/Hasher.cpp
    src/FileStorage.cpp
    src/JpegValidatorImpl.cpp
    src/PngValidatorImpl.cpp
    src/Imager.cpp
)

target_include_directories(libimager
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(libimager
    PUBLIC
        config_lib
    PRIVATE
        database
        jpeg_validator
        png_validation
        OpenSSL::Crypto
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(libimager PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(sample)
add_subdirectory(test)
