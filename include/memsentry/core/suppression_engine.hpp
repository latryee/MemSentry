#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <regex>
#include <algorithm>
#include "memsentry/types.hpp"
#include "memsentry/stacktrace/stacktrace.hpp"

namespace memsentry::core {

class SuppressionEngine {
public:
    static SuppressionEngine& instance() noexcept {
        static SuppressionEngine inst;
        return inst;
    }

    void add_rule(const std::string& pattern) {
        core::RecursionGuard guard;
        std::lock_guard<std::mutex> lock(mutex_);
        if (pattern.empty()) return;
        rules_.push_back(pattern);
    }

    void clear() noexcept {
        core::RecursionGuard guard;
        std::lock_guard<std::mutex> lock(mutex_);
        rules_.clear();
    }

    [[nodiscard]] bool is_suppressed(const AllocationRecord& record) {
        core::RecursionGuard guard;
        std::lock_guard<std::mutex> lock(mutex_);
        if (rules_.empty()) return false;

        // 1. Check tag
        if (record.tag) {
            std::string tag_str(record.tag);
            for (const auto& rule : rules_) {
                if (tag_str.find(rule) != std::string::npos) {
                    return true;
                }
            }
        }

        // 2. Check callstack symbols and files
        if (record.frame_count > 0) {
            auto frames = stacktrace::StackTraceProvider::instance().resolve(record.callstack.data(), record.frame_count);
            for (const auto& frame : frames) {
                for (const auto& rule : rules_) {
                    if (!frame.symbol_name.empty() && frame.symbol_name.find(rule) != std::string::npos) {
                        return true;
                    }
                    if (!frame.file_name.empty() && frame.file_name.find(rule) != std::string::npos) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    [[nodiscard]] size_t rule_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return rules_.size();
    }

private:
    SuppressionEngine() = default;
    ~SuppressionEngine() = default;

    SuppressionEngine(const SuppressionEngine&) = delete;
    SuppressionEngine& operator=(const SuppressionEngine&) = delete;

    mutable std::mutex mutex_;
    std::vector<std::string> rules_;
};

}
