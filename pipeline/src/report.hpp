// Report: what a build did, as name<TAB>value lines in the order they were added, written to
// stdout or to the file --report names (docs/reference/pipeline.md, **The report**); and the
// process's peak resident memory, which the report and the benchmark state.
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chesscoach::pipeline {

class Report {
  public:
    void add(std::string_view name, std::string value);
    void add(std::string_view name, std::uint64_t value);

    // The value of the last line with this name, or empty.
    [[nodiscard]] std::string value(std::string_view name) const;

    void write(std::ostream& out) const;

  private:
    std::vector<std::pair<std::string, std::string>> lines_;
};

// The peak resident set size of this process so far, in bytes (getrusage).
[[nodiscard]] std::uint64_t peak_rss_bytes();

} // namespace chesscoach::pipeline
