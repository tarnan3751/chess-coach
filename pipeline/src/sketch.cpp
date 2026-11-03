// VisitSketch and default_sketch_bytes (sketch.hpp).
#include "sketch.hpp"

#include <array>
#include <bit>
#include <stdexcept>

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

} // namespace

VisitSketch::VisitSketch(std::size_t total_bytes, std::uint16_t saturation)
    : width_(total_bytes / (rows * sizeof(std::uint16_t))),
      shift_(64U - static_cast<unsigned>(std::countr_zero(width_))), saturation_(saturation) {
    if (!std::has_single_bit(total_bytes) || total_bytes < 64 || saturation == 0) {
        throw std::invalid_argument("VisitSketch: the size must be a power of two of at least 64 "
                                    "bytes and the saturation at least 1");
    }
    cells_ = std::vector<std::atomic<std::uint16_t>>(width_ * rows);
}

std::size_t VisitSketch::cell(std::size_t row, std::uint8_t tier, std::uint64_t key) const {
    const std::uint64_t mixed =
        (key ^ (tier_salt * (std::uint64_t{tier} + 1U))) * multipliers.at(row);
    return (row * width_) + static_cast<std::size_t>(mixed >> shift_);
}

void VisitSketch::add(std::uint8_t tier, std::uint64_t key) {
    for (std::size_t row = 0; row < rows; ++row) {
        std::atomic<std::uint16_t>& counter = cells_.at(cell(row, tier, key));
        std::uint16_t value = counter.load(std::memory_order_relaxed);
        while (value < saturation_ &&
               !counter.compare_exchange_weak(value, static_cast<std::uint16_t>(value + 1U),
                                              std::memory_order_relaxed)) {
        }
    }
}

std::uint16_t VisitSketch::estimate(std::uint8_t tier, std::uint64_t key) const {
    std::uint16_t least = saturation_;
    for (std::size_t row = 0; row < rows; ++row) {
        const std::uint16_t value = cells_.at(cell(row, tier, key)).load(std::memory_order_relaxed);
        least = value < least ? value : least;
    }
    return least;
}

std::size_t default_sketch_bytes(std::uint64_t compressed_bytes) {
    if (compressed_bytes >= max_bytes / 2) {
        return max_bytes;
    }
    const std::size_t wanted = std::bit_ceil(static_cast<std::size_t>(compressed_bytes) * 2U);
    return wanted < min_bytes ? min_bytes : wanted;
}

} // namespace chesscoach::pipeline
