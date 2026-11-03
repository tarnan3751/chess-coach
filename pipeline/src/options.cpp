// The month-file checks (options.hpp).
#include "options.hpp"

#include <fmt/format.h>
#include <fmt/std.h>

#include <algorithm>
#include <string_view>

namespace chesscoach::pipeline {

namespace {

constexpr std::string_view month_prefix = "lichess_db_standard_rated_";
constexpr std::string_view month_suffix = ".pgn.zst";

bool is_digit(char c) { return c >= '0' && c <= '9'; }

} // namespace

std::optional<std::string> month_of(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    if (name.size() != month_prefix.size() + 7 + month_suffix.size() ||
        !name.starts_with(month_prefix) || !name.ends_with(month_suffix)) {
        return std::nullopt;
    }
    std::string month = name.substr(month_prefix.size(), 7);
    const bool shaped = is_digit(month.at(0)) && is_digit(month.at(1)) && is_digit(month.at(2)) &&
                        is_digit(month.at(3)) && month.at(4) == '-' && is_digit(month.at(5)) &&
                        is_digit(month.at(6));
    const int month_number = shaped ? ((month.at(5) - '0') * 10) + (month.at(6) - '0') : 0;
    if (month_number < 1 || month_number > 12) {
        return std::nullopt;
    }
    return month;
}

tl::expected<std::vector<MonthInput>, std::string>
month_inputs(const std::vector<std::filesystem::path>& inputs) {
    std::vector<MonthInput> months;
    months.reserve(inputs.size());
    for (const std::filesystem::path& input : inputs) {
        auto month = month_of(input);
        if (!month) {
            return tl::unexpected(fmt::format(
                "{}: an input must be named lichess_db_standard_rated_YYYY-MM.pgn.zst", input));
        }
        months.push_back(MonthInput{.month = std::move(*month), .path = input});
    }
    std::ranges::sort(months, {}, &MonthInput::month);
    const auto duplicate = std::ranges::adjacent_find(months, {}, &MonthInput::month);
    if (duplicate != months.end()) {
        return tl::unexpected(fmt::format("two inputs of the month {}: {} and {}", duplicate->month,
                                          duplicate->path, std::next(duplicate)->path));
    }
    return months;
}

std::filesystem::path partial_path(const std::filesystem::path& output) {
    std::filesystem::path partial = output;
    partial += ".partial";
    return partial;
}

} // namespace chesscoach::pipeline
