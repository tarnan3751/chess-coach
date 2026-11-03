# CHESS_COACH_SANITIZE: AddressSanitizer and UndefinedBehaviorSanitizer over every target of
# ours, with no recovery, so the first report fails the test that triggered it. The gate preset
# turns it on (docs/reference/testing-tools.md, **Presets**); on macOS the root CMakeLists.txt then
# compiles with Homebrew's LLVM clang. The vcpkg ports are built without it; on macOS ASan has no
# LeakSanitizer, so a leak shows only in the Linux job.
include_guard(GLOBAL)

option(CHESS_COACH_SANITIZE "Build with ASan and UBSan, no recovery" OFF)

if(CHESS_COACH_SANITIZE)
  set(chesscoach_sanitize_flags -fsanitize=address,undefined -fno-sanitize-recover=all
                                -fno-omit-frame-pointer)
  add_compile_options(${chesscoach_sanitize_flags})
  add_link_options(${chesscoach_sanitize_flags})
endif()
