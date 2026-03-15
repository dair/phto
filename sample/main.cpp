#include "jpeg_validator.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <file>\n";
        return EXIT_FAILURE;
    }

    const char* path = argv[1];

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << argv[0] << ": cannot open '" << path << "'\n";
        return EXIT_FAILURE;
    }

    const std::vector<unsigned char> data{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};

    const char* label = nullptr;
    switch (validateJpeg(data.data(), data.size())) {
        case VALID:   label = "VALID";   break;
        case INVALID: label = "INVALID"; break;
        case WRONG:   label = "WRONG";   break;
    }

    std::cout << path << ": " << label << "\n";
    return EXIT_SUCCESS;
}
