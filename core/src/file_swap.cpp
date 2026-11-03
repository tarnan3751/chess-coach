// The swap both imports end with (chesscoach/file_swap.hpp).
#include <chesscoach/file_swap.hpp>

#include <system_error>

namespace chesscoach {

bool replace_keeping_previous(const std::filesystem::path& staged,
                              const std::filesystem::path& current,
                              const std::filesystem::path& previous, KeepPrevious keep) {
    auto spare = previous;
    spare += ".importing";
    const auto fail = [&] {
        std::error_code ignored;
        std::filesystem::remove(staged, ignored);
        std::filesystem::remove(spare, ignored);
        return false;
    };
    std::error_code error;
    if (std::filesystem::is_regular_file(current, error)) {
        std::filesystem::remove(previous, error);
        if (error) {
            return fail();
        }
        bool linked = false;
        if (keep == KeepPrevious::link_else_copy) {
            std::filesystem::create_hard_link(current, previous, error);
            linked = !error;
        }
        if (!linked) {
            std::filesystem::remove(spare, error);
            std::filesystem::copy_file(current, spare, error);
            if (error) {
                return fail();
            }
            std::filesystem::rename(spare, previous, error);
            if (error) {
                return fail();
            }
        }
    }
    std::filesystem::rename(staged, current, error);
    if (error) {
        return fail();
    }
    return true;
}

} // namespace chesscoach
