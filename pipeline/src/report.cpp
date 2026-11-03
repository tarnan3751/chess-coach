// Report and peak_rss_bytes (report.hpp).
#include "report.hpp"

#include <sys/resource.h>

namespace chesscoach::pipeline {

void Report::add(std::string_view name, std::string value) {
    lines_.emplace_back(std::string(name), std::move(value));
}

void Report::add(std::string_view name, std::uint64_t value) { add(name, std::to_string(value)); }

std::string Report::value(std::string_view name) const {
    std::string found;
    for (const auto& [line_name, line_value] : lines_) {
        if (line_name == name) {
            found = line_value;
        }
    }
    return found;
}

void Report::write(std::ostream& out) const {
    for (const auto& [name, value] : lines_) {
        out << name << '\t' << value << '\n';
    }
}

std::uint64_t peak_rss_bytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
    // glibc declares struct rusage's fields inside anonymous unions, which clang-tidy's
    // union-access check flags on Linux; the field is a plain integer on every platform.
    const auto peak = static_cast<std::uint64_t>(
        usage.ru_maxrss); // NOLINT(cppcoreguidelines-pro-type-union-access)
#ifdef __APPLE__
    return peak; // bytes on macOS
#else
    return peak * 1024U; // kibibytes on Linux
#endif
}

} // namespace chesscoach::pipeline
