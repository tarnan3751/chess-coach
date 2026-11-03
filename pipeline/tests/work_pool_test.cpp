// Unit tests for run_pool: every item is consumed once, a worker's exception reaches the caller
// after every thread joined, and so does the producer's (pipeline; unit).
#include "work_pool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

using chesscoach::pipeline::run_pool;

TEST_CASE("run_pool consumes every item once", "[pool]") {
    for (const std::size_t threads : {std::size_t{1}, std::size_t{4}}) {
        std::atomic<std::uint64_t> sum{0};
        std::vector<std::uint64_t> per_worker(threads, 0);
        run_pool<std::uint64_t>(
            threads,
            [](const std::function<bool(std::uint64_t)>& push) {
                for (std::uint64_t i = 1; i <= 1000; ++i) {
                    if (!push(i)) {
                        return;
                    }
                }
            },
            [&](std::size_t worker, std::uint64_t& item) {
                sum += item;
                per_worker.at(worker) += 1;
            });
        CHECK(sum == 500500);
        std::uint64_t consumed = 0;
        for (const auto count : per_worker) {
            consumed += count;
        }
        CHECK(consumed == 1000);
    }
}

TEST_CASE("run_pool rethrows a worker's exception", "[pool]") {
    CHECK_THROWS_AS(run_pool<int>(
                        3,
                        [](const std::function<bool(int)>& push) {
                            for (int i = 0; i < 100000; ++i) {
                                if (!push(i)) {
                                    return;
                                }
                            }
                        },
                        [](std::size_t, int& item) {
                            if (item == 50) {
                                throw std::runtime_error("worker failed");
                            }
                        }),
                    std::runtime_error);
}

TEST_CASE("run_pool rethrows the producer's exception", "[pool]") {
    CHECK_THROWS_AS(run_pool<int>(
                        2,
                        [](const std::function<bool(int)>& push) {
                            static_cast<void>(push(1));
                            throw std::runtime_error("producer failed");
                        },
                        [](std::size_t, int&) {}),
                    std::runtime_error);
}
