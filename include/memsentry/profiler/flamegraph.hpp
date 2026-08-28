#pragma once

#include "memsentry/core/recursion_guard.hpp"
#include "memsentry/stacktrace/stacktrace.hpp"
#include "memsentry/types.hpp"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace memsentry::profiler {

struct FlameNode {
    std::string name{"root"};
    std::string file{""};
    uint32_t line{0};
    uint64_t self_bytes{0};
    uint64_t total_bytes{0};
    uint64_t self_count{0};
    uint64_t total_count{0};
    std::vector<std::shared_ptr<FlameNode>> children;

    FlameNode* find_child(const std::string& child_name) {
        for (auto& c : children) {
            if (c->name == child_name)
                return c.get();
        }
        return nullptr;
    }

    FlameNode* get_or_create_child(const std::string& child_name, const std::string& f = "", uint32_t l = 0) {
        FlameNode* existing = find_child(child_name);
        if (existing)
            return existing;
        auto node = std::make_shared<FlameNode>();
        node->name = child_name;
        node->file = f;
        node->line = l;
        children.push_back(node);
        return node.get();
    }
};

class FlamegraphGenerator {
public:
    static std::shared_ptr<FlameNode> build_tree(const std::vector<AllocationRecord>& records) {
        core::RecursionGuard guard;
        auto root = std::make_shared<FlameNode>();
        root->name = "all";
        auto& provider = stacktrace::StackTraceProvider::instance();

        for (const auto& rec : records) {
            root->total_bytes += rec.requested_size;
            root->total_count += 1;

            if (rec.frame_count == 0) {
                FlameNode* unknown = root->get_or_create_child("[unknown]");
                unknown->self_bytes += rec.requested_size;
                unknown->total_bytes += rec.requested_size;
                unknown->self_count += 1;
                unknown->total_count += 1;
                continue;
            }

            auto frames = provider.resolve(rec.callstack.data(), rec.frame_count);
            FlameNode* curr = root.get();

            // Trace from outermost frame (bottom) to innermost frame (top)
            for (int f = static_cast<int>(frames.size()) - 1; f >= 0; --f) {
                const auto& frame = frames[f];
                std::string sym = frame.symbol_name.empty() ? "[unknown]" : frame.symbol_name;
                curr = curr->get_or_create_child(sym, frame.file_name, frame.line_number);
                curr->total_bytes += rec.requested_size;
                curr->total_count += 1;
            }
            curr->self_bytes += rec.requested_size;
            curr->self_count += 1;
        }

        return root;
    }

    static std::string generate_folded_stacks(const std::vector<AllocationRecord>& records) {
        core::RecursionGuard guard;
        std::ostringstream ss;
        auto& provider = stacktrace::StackTraceProvider::instance();

        for (const auto& rec : records) {
            if (rec.frame_count == 0) {
                ss << "[unknown] " << rec.requested_size << "\n";
                continue;
            }

            auto frames = provider.resolve(rec.callstack.data(), rec.frame_count);
            bool first = true;
            for (int f = static_cast<int>(frames.size()) - 1; f >= 0; --f) {
                if (!first)
                    ss << ";";
                first = false;
                ss << (frames[f].symbol_name.empty() ? "[unknown]" : frames[f].symbol_name);
            }
            ss << " " << rec.requested_size << "\n";
        }

        return ss.str();
    }

    static std::string generate_svg(const std::shared_ptr<FlameNode>& root, double width = 1000.0,
                                    double row_height = 22.0) {
        core::RecursionGuard guard;
        if (!root || root->total_bytes == 0) {
            return "<svg width=\"100%\" height=\"60\"><text x=\"20\" y=\"35\" fill=\"#8b949e\">No memory "
                   "allocations to display in flamegraph</text></svg>";
        }

        int max_depth = get_max_depth(root.get(), 0);
        double svg_height = (max_depth + 1) * (row_height + 2.0) + 40.0;

        std::ostringstream ss;
        ss << "<svg class=\"flamegraph-svg\" viewBox=\"0 0 " << width << " " << svg_height << "\" width=\"100%\" "
           << "xmlns=\"http://www.w3.org/2000/svg\">\n";
        ss << "<style>\n"
           << "  .node rect { stroke: #0d1117; stroke-width: 1px; rx: 3px; cursor: pointer; transition: opacity 0.15s; }\n"
           << "  .node rect:hover { stroke: #58a6ff; stroke-width: 2px; opacity: 0.9; }\n"
           << "  .node text { fill: #ffffff; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', monospace; font-size: 11px; pointer-events: none; }\n"
           << "</style>\n";

        render_node_svg(ss, root.get(), 0.0, width, 0, max_depth, row_height, root->total_bytes);
        ss << "</svg>\n";
        return ss.str();
    }

private:
    static int get_max_depth(const FlameNode* node, int depth) {
        int d = depth;
        for (const auto& child : node->children) {
            d = std::max(d, get_max_depth(child.get(), depth + 1));
        }
        return d;
    }

    static void render_node_svg(std::ostringstream& ss, const FlameNode* node, double x, double w, int depth,
                                int max_depth, double row_height, uint64_t total_root_bytes) {
        if (w < 1.0)
            return;

        double y = (max_depth - depth) * (row_height + 2.0) + 10.0;

        // Color palette based on depth & hash for visual hierarchy
        uint32_t hash = 0x811c9dc5;
        for (char c : node->name) {
            hash = (hash ^ static_cast<uint8_t>(c)) * 0x01000193;
        }
        int hue = 200 + (hash % 50);      // Cool blues / teals
        int sat = 60 + ((depth * 7) % 30);
        int light = 40 + ((depth * 5) % 25);

        double pct = (static_cast<double>(node->total_bytes) / static_cast<double>(total_root_bytes)) * 100.0;

        std::ostringstream tooltip;
        tooltip << node->name << " &#10;Bytes: " << node->total_bytes << " (" << std::fixed << std::setprecision(1)
                << pct << "%) &#10;Allocations: " << node->total_count;
        if (!node->file.empty()) {
            tooltip << " &#10;Location: " << node->file << ":" << node->line;
        }

        ss << "<g class=\"node\" data-name=\"" << escape_xml(node->name) << "\">\n"
           << "  <title>" << tooltip.str() << "</title>\n"
           << "  <rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << w << "\" height=\"" << row_height << "\" "
           << "fill=\"hsl(" << hue << ", " << sat << "%, " << light << "%)\"/>\n";

        if (w > 35.0) {
            std::string label = node->name;
            size_t max_chars = static_cast<size_t>(w / 7.0);
            if (label.size() > max_chars && max_chars > 3) {
                label = label.substr(0, max_chars - 3) + "...";
            }
            ss << "  <text x=\"" << (x + 4.0) << "\" y=\"" << (y + row_height - 6.0) << "\">" << escape_xml(label)
               << "</text>\n";
        }
        ss << "</g>\n";

        double child_x = x;
        for (const auto& child : node->children) {
            if (node->total_bytes == 0)
                continue;
            double child_w = (static_cast<double>(child->total_bytes) / static_cast<double>(node->total_bytes)) * w;
            render_node_svg(ss, child.get(), child_x, child_w, depth + 1, max_depth, row_height, total_root_bytes);
            child_x += child_w;
        }
    }

    static std::string escape_xml(const std::string& input) {
        std::string out;
        out.reserve(input.size() + 8);
        for (char c : input) {
            switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '\"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }
};

}  // namespace memsentry::profiler
