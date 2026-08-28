#include "memsentry/memsentry.hpp"

#include <cstring>

int main() {
    memsentry::Config config;
    config.enable_canary = true;
    memsentry::init(config);

    char* buffer = new char[16];

    std::memset(buffer + 16, 0xFF, 4);

    delete[] buffer;

    return 0;
}
