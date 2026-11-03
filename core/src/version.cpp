// The project version: parsing, printing and the version of this build (chesscoach/version.hpp).
#include <chesscoach/version.hpp>
#include <chesscoach/version_info.hpp>

#include <fmt/format.h>

#include <limits>
#include <optional>

namespace chesscoach {

namespace {

// One version component: decimal digits only, no leading zero unless the component is 0, and a
// value that fits an int; nullopt otherwise.
std::optional<int> parse_component(std::string_view text) {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const int digit = c - '0';
        if (value > (std::numeric_limits<int>::max() - digit) / 10) {
            return std::nullopt;
        }
        value = (value * 10) + digit;
    }
    return value;
}

} // namespace

tl::expected<Version, VersionError> parse_version(std::string_view text) {
    if (text.empty()) {
        return tl::unexpected(VersionError::empty);
    }
    const auto first_dot = text.find('.');
    if (first_dot == std::string_view::npos) {
        return tl::unexpected(VersionError::malformed);
    }
    const auto second_dot = text.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) {
        return tl::unexpected(VersionError::malformed);
    }
    // A third dot leaves a non-digit in the patch component, which parse_component rejects.
    const auto major_part = parse_component(text.substr(0, first_dot));
    const auto minor_part = parse_component(text.substr(first_dot + 1, second_dot - first_dot - 1));
    const auto patch_part = parse_component(text.substr(second_dot + 1));
    if (!major_part || !minor_part || !patch_part) {
        return tl::unexpected(VersionError::malformed);
    }
    return Version{.major = *major_part, .minor = *minor_part, .patch = *patch_part};
}

std::string to_string(const Version& version) {
    return fmt::format("{}.{}.{}", version.major, version.minor, version.patch);
}

Version current_version() {
    return Version{
        .major = CHESSCOACH_VERSION_MAJOR,
        .minor = CHESSCOACH_VERSION_MINOR,
        .patch = CHESSCOACH_VERSION_PATCH,
    };
}

} // namespace chesscoach
