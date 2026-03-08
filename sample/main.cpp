#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "validate_png.h"

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <file>\n";
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Error: cannot open '" << argv[1] << "'\n";
        return 1;
    }

    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    ValidationResult result = validatePng(data.data(), data.size());

    const char* word = (result == VALID)   ? "VALID"
                     : (result == INVALID) ? "INVALID"
                                           : "WRONG";

    std::cout << argv[1] << ": " << word << "\n";
    return 0;
}
