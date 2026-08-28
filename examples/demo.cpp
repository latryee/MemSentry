#include "memsentry/memsentry.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

void simulate_asset_pipeline() {
    MEMSENTRY_SCOPE_TAG("AssetManager");
    // Allocate texture buffer (intentional persistent leak for demonstration)
    char* texture_cache = new char[1024 * 1024]; // 1.00 MB
    (void)texture_cache;
}

void simulate_network_subsystem() {
    MEMSENTRY_SCOPE_TAG("NetworkSync");
    // Transient allocation that gets freed
    char* packet_buffer = new char[2048];
    delete[] packet_buffer;
}

void run_workload_and_diff() {
    std::cout << "[*] Capturing baseline heap snapshot...\n";
    auto snap_baseline = memsentry::take_snapshot("Baseline");

    std::cout << "[*] Executing workload across subsystems (AssetManager, NetworkSync)...\n";
    simulate_asset_pipeline();
    simulate_network_subsystem();

    std::cout << "[*] Capturing post-workload snapshot...\n";
    auto snap_post = memsentry::take_snapshot("PostWorkload");

    auto diff = memsentry::profiler::compare_snapshots(snap_baseline, snap_post);
    memsentry::reporter::ConsoleReporter::print_diff(std::cout, diff);
}

int main() {
    std::cout << "\033[1;36m================================================================================\033[0m\n";
    std::cout << "\033[1;37m                       MEMSENTRY LIVE RUNTIME AUDIT DEMO                        \033[0m\n";
    std::cout << "\033[1;36m================================================================================\033[0m\n";

    memsentry::Config config;
    config.enable_canary = true;
    config.enable_stacktrace = true;
    config.auto_report_on_exit = false;
    memsentry::init(config);

    std::cout << "[*] Initialized MemSentry runtime (64 Shards, Canary Active, Native DbgHelp)\n";

    // Run workload and snapshot diffing
    run_workload_and_diff();

    // Print live heap leak report
    std::cout << "\n[*] Printing live heap leak report...\n";
    memsentry::dump_leaks(std::cout);

    // Export reports
    memsentry::export_html("report_demo.html");
    memsentry::export_json("report_demo.json");
    std::cout << "\033[1;32m[+] Interactive HTML dashboard written to -> report_demo.html\033[0m\n";
    std::cout << "\033[1;32m[+] Machine-readable JSON report written to -> report_demo.json\033[0m\n";
    std::cout << "\033[1;36m================================================================================\033[0m\n";

    return 0;
}
