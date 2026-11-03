# chesscoach::warnings: the warning flags every target of ours links PRIVATE, warnings as
# errors on every compiler (CLAUDE.md, **Stack**). Third-party headers come in through imported
# targets as system includes, so these flags apply to our code only.
include_guard(GLOBAL)

add_library(chesscoach_warnings INTERFACE)
add_library(chesscoach::warnings ALIAS chesscoach_warnings)

target_compile_options(
  chesscoach_warnings
  INTERFACE -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Woverloaded-virtual
            # -Wnull-dereference on clang only: GCC's fires inside libstdc++'s <optional> at -O3
            # (a known false positive), which failed the release build on Ubuntu.
            "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wnull-dereference>"
            -Wdouble-promotion
            -Wformat=2
            -Werror
            # Not -Wuseless-cast: it rejects the casts between uint64_t and size_t that macOS
            # needs and Linux does not, which portable code must keep.
            # Not -Wdangling-reference (GCC 13 puts it in -Wextra): it flags `const auto&` bound
            # to the return of an accessor that returns a reference, a false positive GCC 14
            # narrowed.
            "$<$<CXX_COMPILER_ID:GNU>:-Wduplicated-cond;-Wlogical-op;-Wno-dangling-reference>")
