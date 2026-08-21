#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::filesystem::path executable{std::u8string{
        reinterpret_cast<const char8_t*>(argv[1]), std::strlen(argv[1])}};
    std::ifstream input(executable, std::ios::binary);
    CHECK(input.good());
    std::string image{std::istreambuf_iterator<char>{input},
                      std::istreambuf_iterator<char>{}};
    std::transform(image.begin(), image.end(), image.begin(), [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });

    CHECK(image.find("playsoundw") == std::string::npos);
    CHECK(image.find("messagebeep") == std::string::npos);
    CHECK(image.find("systemasterisk") == std::string::npos);
    CHECK(image.find("systemexclamation") == std::string::npos);
    return EXIT_SUCCESS;
}
