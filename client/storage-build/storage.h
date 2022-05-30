#ifndef STORAGE_BUILD_H
    #define STORAGE_H

    uint64_t crc64(const uint8_t *data, size_t length);

    #define MB(x)   (x / (1024 * 1024.0))
    #define GB(x)   (x / (1024 * 1024 * 1024.0))

    #define COLOR_RED    "\033[31;1m"
    #define COLOR_YELLOW "\033[33;1m"
    #define COLOR_BLUE   "\033[34;1m"
    #define COLOR_GREEN  "\033[32;1m"
    #define COLOR_CYAN   "\033[36;1m"
    #define COLOR_RESET  "\033[0m"
#endif

