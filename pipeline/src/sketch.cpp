// VisitSketch and default_sketch_bytes (sketch.hpp).
#include "sketch.hpp"

#include <fmt/format.h>

#include <array>
#include <atomic>
#include <bit>
#include <type_traits>
#include <variant>

namespace chesscoach::pipeline {

namespace {

// One odd multiplier per row (multiply-shift hashing) and a salt that separates the tiers.
constexpr std::array<std::uint64_t, VisitSketch::rows> multipliers{
    0x9E3779B97F4A7C15U,
    0xC2B2AE3D27D4EB4FU,
    0x165667B19E3779F9U,
    0xD6E8FEB86659FD93U,
};
constexpr std::uint64_t tier_salt = 0xFF51AFD7ED558CCDU;

constexpr std::size_t min_bytes = std::size_t{1} << 20U;
constexpr std::size_t max_bytes = std::size_t{1} << 30U;

// The bytes of one counter at a saturation.
constexpr std::size_t counter_size(std::uint16_t saturation) {
    return saturation <= VisitSketch::max_8bit_saturation ? sizeof(std::uint8_t)
                                                          : sizeof(std::uint16_t);
}

} // namespace

tl::expected<VisitSketch, std::string> VisitSketch::create(std::size_t total_bytes,
                                                           std::uint16_t saturation) {
    if (!std::has_single_bit(total_bytes) || total_bytes < 64 || total_bytes > max_sketch_bytes) {
        return tl::unexpected(fmt::format(
            "the sketch's size must be a power of two from 64 bytes to {} bytes, not {}",
            max_sketch_bytes, total_bytes));
    }
    if (saturation == 0) {
        return tl::unexpected(
            std::string("the sketch's saturation (min_games) must be at least 1"));
    }
    return VisitSketch(total_bytes, saturation);
}

VisitSketch::VisitSketch(std::size_t total_bytes, std::uint16_t saturation)
    : width_(total_bytes / (rows * counter_size(saturation))),
      shift_(64U - static_cast<unsigned>(std::countr_zero(width_))), saturation_(saturation) {
    if (counter_size(saturation) == sizeof(std::uint8_t)) {
        cells_.emplace<std::vector<std::atomic<std::uint8_t>>>(width_ * rows);
    } else {
        cells_.emplace<std::vector<std::atomic<std::uint16_t>>>(width_ * rows);
    }
}

std::size_t VisitSketch::counter_bytes() const {
    return std::visit(
        [](const auto& cells) {
            return sizeof(typename std::decay_t<decltype(cells)>::value_type);
        },
        cells_);
}

std::size_t VisitSketch::cell(std::size_t row, std::uint8_t tier, std::uint64_t key) const {
    const std::uint64_t mixed =
        (key ^ (tier_salt * (std::uint64_t{tier} + 1U))) * multipliers.at(row);
    return (row * width_) + static_cast<std::size_t>(mixed >> shift_);
}

// One body for both counter widths: the saturation fits the counter by construction.
void VisitSketch::add(std::uint8_t tier, std::uint64_t key) {
    std::visit(
        [&](auto& cells) {
            using Counter = std::decay_t<decltype(cells)>::value_type::value_type;
            const auto saturation = static_cast<Counter>(saturation_);
            for (std::size_t row = 0; row < rows; ++row) {
                auto& counter = cells.at(cell(row, tier, key));
                Counter value = counter.load(std::memory_order_relaxed);
                while (value < saturation &&
                       !counter.compare_exchange_weak(value, static_cast<Counter>(value + 1U),
                                                      std::memory_order_relaxed)) {
                }
            }
        },
        cells_);
}

std::uint16_t VisitSketch::estimate(std::uint8_t tier, std::uint64_t key) const {
    return std::visit(
        [&](const auto& cells) {
            std::uint16_t least = saturation_;
            for (std::size_t row = 0; row < rows; ++row) {
                const std::uint16_t value =
                    cells.at(cell(row, tier, key)).load(std::memory_order_relaxed);
                least = value < least ? value : least;
            }
            return least;
        },
        cells_);
}

std::size_t default_sketch_bytes(std::uint64_t compressed_bytes) {
    if (compressed_bytes >= max_bytes / 2) {
        return max_bytes;
    }
    const std::size_t wanted = std::bit_ceil(static_cast<std::size_t>(compressed_bytes) * 2U);
    return wanted < min_bytes ? min_bytes : wanted;
}

} // namespace chesscoach::pipeline
