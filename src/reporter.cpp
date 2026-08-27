#include "memsentry/reporter/console_reporter.hpp"
#include "memsentry/reporter/json_reporter.hpp"
#include "memsentry/reporter/html_reporter.hpp"
#include "memsentry/stacktrace/stacktrace.hpp"
#include "memsentry/profiler/histogram.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace memsentry::reporter {

static std::string format_bytes(size_t bytes) {
    std::ostringstream oss;
    if (bytes < 1024) {
        oss << bytes << " B";
    } else if (bytes < 1024 * 1024) {
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / 1024.0) << " KB";
    } else if (bytes < 1024ULL * 1024 * 1024) {
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    } else {
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << " GB";
    }
    return oss.str();
}

static std::string json_escape(const std::string& input) {
    std::string output;
    output.reserve(input.size() + 16);
    for (char c : input) {
        switch (c) {
            case '\"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    output += buf;
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

void ConsoleReporter::print_summary(std::ostream& os, const MemoryStatsSnapshot& stats, size_t leak_count, size_t leak_bytes) {
    os << "\n\033[1;36m================================================================================\033[0m\n";
    os << "\033[1;37m                             MEMSENTRY AUDIT SUMMARY                            \033[0m\n";
    os << "\033[1;36m================================================================================\033[0m\n";

    if (leak_count == 0) {
        os << "\033[1;32m [STATUS] ALL HEAP BLOCKS FREED. ZERO LEAKS DETECTED.\033[0m\n";
    } else {
        os << "\033[1;31m [STATUS] MEMORY LEAKS DETECTED: " << leak_count << " block(s) unreleased (" << format_bytes(leak_bytes) << ")\033[0m\n";
    }

    os << "--------------------------------------------------------------------------------\n";
    os << " Total Allocations   : " << stats.total_allocation_count << " (" << format_bytes(stats.total_allocated_bytes) << ")\n";
    os << " Total Deallocations : " << stats.total_free_count << " (" << format_bytes(stats.total_freed_bytes) << ")\n";
    os << " Peak Heap Usage     : " << format_bytes(stats.peak_allocated_bytes) << "\n";
    os << " Active Heap Usage   : " << format_bytes(stats.current_allocated_bytes) << "\n";
    os << "\033[1;36m================================================================================\033[0m\n\n";
}

void ConsoleReporter::print_leaks(std::ostream& os, const std::vector<AllocationRecord>& leaks) {
    if (leaks.empty()) return;

    os << "\033[1;31mDETAILED LEAK REPORT:\033[0m\n";
    os << "--------------------------------------------------------------------------------\n";

    auto& provider = stacktrace::StackTraceProvider::instance();

    for (size_t i = 0; i < leaks.size(); ++i) {
        const auto& rec = leaks[i];
        os << "\033[1;33m[Leak #" << (i + 1) << "]\033[0m ID: " << rec.allocation_id
           << " | Size: \033[1;37m" << rec.requested_size << " bytes\033[0m"
           << " | Addr: 0x" << std::hex << reinterpret_cast<uintptr_t>(rec.user_ptr) << std::dec
           << " | Tag: [" << (rec.tag ? rec.tag : "General") << "]"
           << " | Thread: " << rec.thread_id << "\n";

        if (rec.frame_count > 0) {
            auto frames = provider.resolve(rec.callstack.data(), rec.frame_count);
            for (size_t f = 0; f < frames.size(); ++f) {
                const auto& frame = frames[f];
                os << "    #" << std::setw(2) << f << " " << frame.symbol_name;
                if (!frame.file_name.empty()) {
                    os << " (" << frame.file_name << ":" << frame.line_number << ")";
                }
                os << "\n";
            }
        }
        os << "--------------------------------------------------------------------------------\n";
    }
}

void ConsoleReporter::print_diff(std::ostream& os, const profiler::SnapshotDiff& diff) {
    os << "\n\033[1;36m================================================================================\033[0m\n";
    os << "\033[1;37m                     SNAPSHOT DIFF: [" << diff.before_label << "] -> [" << diff.after_label << "]\033[0m\n";
    os << "\033[1;36m================================================================================\033[0m\n";
    os << " Net Memory Delta    : " << (diff.net_bytes_delta >= 0 ? "+" : "") << diff.net_bytes_delta << " bytes\n";
    os << " Net Alloc Delta     : " << (diff.net_allocations_delta >= 0 ? "+" : "") << diff.net_allocations_delta << " blocks\n";
    os << " Newly Leaked Blocks : " << diff.new_allocations.size() << " (" << format_bytes(diff.new_leaked_bytes()) << ")\n";
    os << " Persistent Blocks   : " << diff.persistent_allocations.size() << "\n";
    os << " Freed During Window : " << diff.freed_allocations.size() << "\n";
    os << "\033[1;36m================================================================================\033[0m\n";
}

void ConsoleReporter::print_corruption_alert(std::ostream& os, const void* ptr, CorruptionType type) {
    os << "\n\033[1;41;37m [FATAL ERROR] MEMORY CORRUPTION DETECTED! \033[0m\n";
    os << " Pointer : 0x" << std::hex << reinterpret_cast<uintptr_t>(ptr) << std::dec << "\n";
    os << " Reason  : ";
    switch (type) {
        case CorruptionType::HEADER_CORRUPTED: os << "Buffer underrun / Block header magic overwritten\n"; break;
        case CorruptionType::FOOTER_CORRUPTED: os << "Buffer overrun / Red-zone canary overwritten\n"; break;
        case CorruptionType::DOUBLE_FREE: os << "Double free detected\n"; break;
        case CorruptionType::UNTRACKED_POINTER: os << "Freeing pointer never allocated by allocator\n"; break;
        default: os << "Unknown corruption\n"; break;
    }
    os << "\033[1;31m================================================================================\033[0m\n";
}

std::string JsonReporter::serialize(const MemoryStatsSnapshot& stats, const std::vector<AllocationRecord>& records) {
    std::ostringstream ss;
    auto& provider = stacktrace::StackTraceProvider::instance();

    size_t total_leak_bytes = 0;
    for (const auto& rec : records) total_leak_bytes += rec.requested_size;

    ss << "{\n";
    ss << "  \"summary\": {\n";
    ss << "    \"total_allocated_bytes\": " << stats.total_allocated_bytes << ",\n";
    ss << "    \"total_freed_bytes\": " << stats.total_freed_bytes << ",\n";
    ss << "    \"current_allocated_bytes\": " << stats.current_allocated_bytes << ",\n";
    ss << "    \"peak_allocated_bytes\": " << stats.peak_allocated_bytes << ",\n";
    ss << "    \"total_allocations\": " << stats.total_allocation_count << ",\n";
    ss << "    \"total_frees\": " << stats.total_free_count << ",\n";
    ss << "    \"leak_count\": " << records.size() << ",\n";
    ss << "    \"leak_bytes\": " << total_leak_bytes << "\n";
    ss << "  },\n";

    ss << "  \"leaks\": [\n";
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& rec = records[i];
        ss << "    {\n";
        ss << "      \"id\": " << rec.allocation_id << ",\n";
        ss << "      \"address\": \"" << rec.user_ptr << "\",\n";
        ss << "      \"requested_size\": " << rec.requested_size << ",\n";
        ss << "      \"total_size\": " << rec.total_size << ",\n";
        ss << "      \"thread_id\": " << rec.thread_id << ",\n";
        ss << "      \"tag\": \"" << json_escape(rec.tag ? rec.tag : "General") << "\",\n";
        ss << "      \"stacktrace\": [\n";

        if (rec.frame_count > 0) {
            auto frames = provider.resolve(rec.callstack.data(), rec.frame_count);
            for (size_t f = 0; f < frames.size(); ++f) {
                const auto& frame = frames[f];
                ss << "        {\n";
                ss << "          \"frame\": " << f << ",\n";
                ss << "          \"symbol\": \"" << json_escape(frame.symbol_name) << "\",\n";
                ss << "          \"file\": \"" << json_escape(frame.file_name) << "\",\n";
                ss << "          \"line\": " << frame.line_number << "\n";
                ss << "        }" << (f + 1 < frames.size() ? "," : "") << "\n";
            }
        }

        ss << "      ]\n";
        ss << "    }" << (i + 1 < records.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

bool JsonReporter::write_file(const std::string& filepath, const MemoryStatsSnapshot& stats, const std::vector<AllocationRecord>& records) {
    std::ofstream out(filepath);
    if (!out.is_open()) return false;
    out << serialize(stats, records);
    return true;
}

std::string HtmlReporter::generate(const MemoryStatsSnapshot& stats, const std::vector<AllocationRecord>& records) {
    std::ostringstream ss;
    auto& provider = stacktrace::StackTraceProvider::instance();

    size_t total_leak_bytes = 0;
    for (const auto& rec : records) total_leak_bytes += rec.requested_size;

    profiler::AllocationHistogram hist;
    hist.feed(records);

    ss << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    ss << "<meta charset=\"UTF-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    ss << "<title>MemSentry - Heap Profile & Leak Report</title>\n";
    ss << "<style>\n";
    ss << "* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; }\n";
    ss << "body { background-color: #0d1117; color: #c9d1d9; padding: 24px; }\n";
    ss << "header { margin-bottom: 24px; display: flex; align-items: center; justify-content: space-between; border-bottom: 1px solid #30363d; padding-bottom: 16px; }\n";
    ss << "h1 { font-size: 24px; color: #58a6ff; display: flex; align-items: center; gap: 8px; }\n";
    ss << ".badge { padding: 4px 10px; border-radius: 12px; font-size: 12px; font-weight: 600; }\n";
    ss << ".badge-clean { background-color: #238636; color: #ffffff; }\n";
    ss << ".badge-leak { background-color: #da3633; color: #ffffff; }\n";
    ss << ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; margin-bottom: 24px; }\n";
    ss << ".card { background-color: #161b22; border: 1px solid #30363d; border-radius: 8px; padding: 16px; }\n";
    ss << ".card-title { font-size: 13px; color: #8b949e; text-transform: uppercase; margin-bottom: 6px; }\n";
    ss << ".card-value { font-size: 22px; font-weight: 700; color: #f0f6fc; }\n";
    ss << ".section { background-color: #161b22; border: 1px solid #30363d; border-radius: 8px; padding: 20px; margin-bottom: 24px; }\n";
    ss << ".section h2 { font-size: 16px; color: #f0f6fc; margin-bottom: 16px; border-bottom: 1px solid #21262d; padding-bottom: 8px; }\n";
    ss << ".histogram-bar { display: flex; align-items: center; margin-bottom: 8px; font-size: 13px; }\n";
    ss << ".histogram-label { width: 140px; color: #8b949e; }\n";
    ss << ".histogram-track { flex-grow: 1; background-color: #21262d; height: 16px; border-radius: 4px; overflow: hidden; margin: 0 12px; }\n";
    ss << ".histogram-fill { height: 100%; background: linear-gradient(90deg, #1f6feb, #58a6ff); border-radius: 4px; }\n";
    ss << ".histogram-val { width: 80px; text-align: right; color: #c9d1d9; }\n";
    ss << "table { width: 100%; border-collapse: collapse; margin-top: 12px; font-size: 13px; }\n";
    ss << "th { background-color: #21262d; color: #8b949e; text-align: left; padding: 10px 12px; border-bottom: 1px solid #30363d; }\n";
    ss << "td { padding: 10px 12px; border-bottom: 1px solid #21262d; }\n";
    ss << "tr:hover { background-color: #1c2128; cursor: pointer; }\n";
    ss << ".tag-pill { background-color: #388bfd26; color: #58a6ff; padding: 2px 8px; border-radius: 6px; font-size: 11px; }\n";
    ss << ".stacktrace { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; font-size: 12px; color: #8b949e; padding: 8px 16px; background-color: #0d1117; display: none; }\n";
    ss << ".stack-line { margin: 3px 0; }\n";
    ss << ".stack-sym { color: #f0f6fc; }\n";
    ss << ".stack-loc { color: #7ee787; }\n";
    ss << "input[type=\"text\"] { background-color: #0d1117; border: 1px solid #30363d; border-radius: 6px; color: #c9d1d9; padding: 8px 12px; width: 300px; margin-bottom: 12px; }\n";
    ss << "</style>\n</head>\n<body>\n";

    ss << "<header>\n";
    ss << "  <h1>MemSentry Heap Audit</h1>\n";
    ss << "  <div>" << (records.empty() ? "<span class=\"badge badge-clean\">CLEAN: NO LEAKS</span>" : "<span class=\"badge badge-leak\">LEAKS DETECTED</span>") << "</div>\n";
    ss << "</header>\n";

    ss << "<div class=\"grid\">\n";
    ss << "  <div class=\"card\"><div class=\"card-title\">Total Leaked</div><div class=\"card-value\" style=\"color:#f85149;\">" << format_bytes(total_leak_bytes) << "</div></div>\n";
    ss << "  <div class=\"card\"><div class=\"card-title\">Active Leaks</div><div class=\"card-value\" style=\"color:#f85149;\">" << records.size() << "</div></div>\n";
    ss << "  <div class=\"card\"><div class=\"card-title\">Peak Heap</div><div class=\"card-value\">" << format_bytes(stats.peak_allocated_bytes) << "</div></div>\n";
    ss << "  <div class=\"card\"><div class=\"card-title\">Lifetime Allocations</div><div class=\"card-value\">" << stats.total_allocation_count << "</div></div>\n";
    ss << "  <div class=\"card\"><div class=\"card-title\">Lifetime Frees</div><div class=\"card-value\">" << stats.total_free_count << "</div></div>\n";
    ss << "</div>\n";

    ss << "<div class=\"section\">\n";
    ss << "  <h2>Allocation Size Distribution</h2>\n";
    uint64_t max_bucket_count = 1;
    for (const auto& b : hist.buckets()) {
        if (b.count > max_bucket_count) max_bucket_count = b.count;
    }
    for (const auto& b : hist.buckets()) {
        double pct = (static_cast<double>(b.count) / static_cast<double>(max_bucket_count)) * 100.0;
        ss << "  <div class=\"histogram-bar\">\n";
        ss << "    <div class=\"histogram-label\">" << b.label << "</div>\n";
        ss << "    <div class=\"histogram-track\"><div class=\"histogram-fill\" style=\"width:" << pct << "%\"></div></div>\n";
        ss << "    <div class=\"histogram-val\">" << b.count << " (" << format_bytes(b.total_bytes) << ")</div>\n";
        ss << "  </div>\n";
    }
    ss << "</div>\n";

    ss << "<div class=\"section\">\n";
    ss << "  <h2>Unreleased Memory Blocks</h2>\n";
    ss << "  <input type=\"text\" id=\"searchBox\" placeholder=\"Filter by tag, address or size...\" onkeyup=\"filterTable()\">\n";
    ss << "  <table id=\"leaksTable\">\n";
    ss << "    <thead><tr><th>ID</th><th>Address</th><th>Size</th><th>Tag</th><th>Thread</th><th>Frames</th></tr></thead>\n";
    ss << "    <tbody>\n";

    for (size_t i = 0; i < records.size(); ++i) {
        const auto& rec = records[i];
        ss << "    <tr onclick=\"toggleTrace(" << i << ")\">\n";
        ss << "      <td>#" << rec.allocation_id << "</td>\n";
        ss << "      <td><code>0x" << std::hex << reinterpret_cast<uintptr_t>(rec.user_ptr) << std::dec << "</code></td>\n";
        ss << "      <td><strong>" << rec.requested_size << " B</strong></td>\n";
        ss << "      <td><span class=\"tag-pill\">" << (rec.tag ? rec.tag : "General") << "</span></td>\n";
        ss << "      <td>" << rec.thread_id << "</td>\n";
        ss << "      <td>" << rec.frame_count << " frames (click to expand)</td>\n";
        ss << "    </tr>\n";
        ss << "    <tr id=\"trace-" << i << "\" class=\"stacktrace\"><td colspan=\"6\">\n";

        if (rec.frame_count > 0) {
            auto frames = provider.resolve(rec.callstack.data(), rec.frame_count);
            for (size_t f = 0; f < frames.size(); ++f) {
                const auto& frame = frames[f];
                ss << "      <div class=\"stack-line\">#" << f << " <span class=\"stack-sym\">" << frame.symbol_name << "</span>";
                if (!frame.file_name.empty()) {
                    ss << " at <span class=\"stack-loc\">" << frame.file_name << ":" << frame.line_number << "</span>";
                }
                ss << "</div>\n";
            }
        } else {
            ss << "      <div>(No stack frames captured)</div>\n";
        }
        ss << "    </td></tr>\n";
    }

    ss << "    </tbody>\n  </table>\n</div>\n";

    ss << "<script>\n";
    ss << "function toggleTrace(id) {\n";
    ss << "  var row = document.getElementById('trace-' + id);\n";
    ss << "  row.style.display = (row.style.display === 'table-row') ? 'none' : 'table-row';\n";
    ss << "}\n";
    ss << "function filterTable() {\n";
    ss << "  var query = document.getElementById('searchBox').value.toLowerCase();\n";
    ss << "  var rows = document.querySelectorAll('#leaksTable tbody tr:not(.stacktrace)');\n";
    ss << "  rows.forEach(function(row) {\n";
    ss << "    row.style.display = row.innerText.toLowerCase().includes(query) ? '' : 'none';\n";
    ss << "  });\n";
    ss << "}\n";
    ss << "</script>\n</body>\n</html>\n";

    return ss.str();
}

bool HtmlReporter::write_file(const std::string& filepath, const MemoryStatsSnapshot& stats, const std::vector<AllocationRecord>& records) {
    std::ofstream out(filepath);
    if (!out.is_open()) return false;
    out << generate(stats, records);
    return true;
}

}
