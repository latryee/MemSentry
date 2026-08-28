#include "memsentry/memsentry.hpp"
#include <vector>
#include <iostream>

void process_request(int req_id) {
    MEMSENTRY_SCOPE_TAG("NetworkRequestHandler");
    std::vector<int*> temp_buffers;
    for (int i = 0; i < 10; ++i) {
        temp_buffers.push_back(new int[128]);
    }

    for (size_t i = 0; i < temp_buffers.size(); ++i) {
        if (req_id == 2 && i == 0) {
            continue;
        }
        delete[] temp_buffers[i];
    }
}

int main() {
    memsentry::init();

    auto baseline = memsentry::take_snapshot("Baseline");

    for (int i = 1; i <= 3; ++i) {
        process_request(i);
    }

    auto post_workload = memsentry::take_snapshot("PostWorkload");

    auto diff = memsentry::profiler::compare_snapshots(baseline, post_workload);
    memsentry::reporter::ConsoleReporter::print_diff(std::cout, diff);

    return 0;
}
