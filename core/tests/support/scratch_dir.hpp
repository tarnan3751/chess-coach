// A scratch directory for one test case, shared by the tests of every module (core_test_support;
// tests may share their support across modules, the module boundary is about the products):
// <CHESSCOACH_TEST_SCRATCH_DIR>/<group>/<name>, the root defined by the test's target, emptied
// when it is made and removed with what it holds when the test case ends, so a test that fails
// leaves nothing for the next run. One per test case.
#pragma once

#include <filesystem>
#include <functional>
#include <string_view>
#include <system_error>

namespace chesscoach::testing {

class ScratchDir {
  public:
    // prepare, when given, runs once the directory exists, with its path: a test sets its
    // environment there (the fake engine's log in the directory, say).
    ScratchDir(std::string_view group, std::string_view name,
               const std::function<void(const std::filesystem::path&)>& prepare = {})
        : root_(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / group / name) {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        if (prepare) {
            prepare(root_);
        }
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    ScratchDir(ScratchDir&&) = delete;
    ScratchDir& operator=(ScratchDir&&) = delete;
    // Only the error_code overloads, which do not throw: a file a test made read-only is made
    // writable again (never through a symlink) so the directory can go.
    ~ScratchDir() {
        std::error_code walk;
        for (std::filesystem::recursive_directory_iterator entry(root_, walk), end;
             !walk && entry != end; entry.increment(walk)) {
            std::error_code ignored;
            if (entry->symlink_status(ignored).type() == std::filesystem::file_type::regular) {
                std::filesystem::permissions(entry->path(), std::filesystem::perms::owner_write,
                                             std::filesystem::perm_options::add, ignored);
            }
        }
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return root_; }
    [[nodiscard]] std::filesystem::path path(std::string_view name) const { return root_ / name; }

  private:
    std::filesystem::path root_;
};

} // namespace chesscoach::testing
