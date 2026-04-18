cmake_minimum_required(VERSION 3.28)

add_library(libimager
    Hasher.cpp
    StreamHasher.cpp
    FileStorage.cpp
    FileTimestamp.cpp
    Validators.cpp
    JpegValidatorImpl.cpp
    PngValidatorImpl.cpp
    HeicValidatorImpl.cpp
    NefValidatorImpl.cpp
    MovValidatorImpl.cpp
    AaeValidatorImpl.cpp
    JpegStreamValidatorImpl.cpp
    PngStreamValidatorImpl.cpp
    HeicStreamValidatorImpl.cpp
    MovStreamValidatorImpl.cpp
    NefStreamValidatorImpl.cpp
    AaeStreamValidatorImpl.cpp
    Imager.cpp
    MultiDatabase.cpp
)

target_include_directories(libimager
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/.."
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(libimager
    PUBLIC
        blob_lib
        config_lib
        metrics_lib
    PRIVATE
        coro_lib
        database
        jpeg_validator
        png_validation
        heic_validator
        nef_validator
        mov_validator
        aae_validator
        OpenSSL::Crypto
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(libimager PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(sample)
add_subdirectory(test)
