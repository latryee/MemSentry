#include "memsentry/memsentry.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct TextureResource {
    int width = 1920;
    int height = 1080;
    uint8_t* pixel_buffer = nullptr;

    TextureResource() { pixel_buffer = new uint8_t[width * height * 4]; }

    ~TextureResource() { delete[] pixel_buffer; }
};

void simulate_leak() {
    auto* leaked_resource = new TextureResource();
    int* leaked_array = new int[256];
    (void)leaked_resource;
    (void)leaked_array;
}

int main() {
    memsentry::Config config;
    config.auto_report_on_exit = true;
    memsentry::init(config);

    int* properly_freed = new int(42);
    delete properly_freed;

    simulate_leak();

    memsentry::export_html("report_01.html");
    memsentry::export_json("report_01.json");

    return 0;
}
