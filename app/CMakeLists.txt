# app: chess-coach, the Qt Widgets application (CLAUDE.md, **app/ is presentation**;
# docs/reference/app.md). The only module that finds or links Qt, which comes from the system
# through CMAKE_PREFIX_PATH, never from vcpkg. The window, the board and the screens are app_lib,
# which the executable and the UI tests link; main.cpp only parses the command line. The piece
# set is compiled in as a Qt resource, so the app needs no files beside it to draw the board.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Svg SvgWidgets Test)
set(CMAKE_AUTOMOC ON)

add_library(app_lib STATIC
  src/about_dialog.cpp
  src/about_dialog.hpp
  src/board_scene.cpp
  src/board_scene.hpp
  src/board_theme.cpp
  src/board_theme.hpp
  src/board_view.cpp
  src/board_view.hpp
  src/db_worker.cpp
  src/db_worker.hpp
  src/drill_screen.cpp
  src/drill_screen.hpp
  src/default_profile.cpp
  src/default_profile.hpp
  src/engine_worker.cpp
  src/engine_worker.hpp
  src/eval_bar.cpp
  src/eval_bar.hpp
  src/explore_data.hpp
  src/explore_model.cpp
  src/explore_model.hpp
  src/explore_screen.cpp
  src/explore_screen.hpp
  src/file_chooser.cpp
  src/file_chooser.hpp
  src/main_window.cpp
  src/main_window.hpp
  src/main_window_drill.cpp
  src/main_window_repertoire.cpp
  src/piece_set.cpp
  src/piece_set.hpp
  src/profile_data.hpp
  src/profile_worker.cpp
  src/profile_worker.hpp
  src/promotion_chooser.cpp
  src/promotion_chooser.hpp
  src/repertoire_screen.cpp
  src/repertoire_screen.hpp
  src/result_delegate.cpp
  src/result_delegate.hpp
  src/settings.cpp
  src/settings.hpp
  src/settings_dialog.cpp
  src/settings_dialog.hpp
  src/spar_data.hpp
  src/spar_screen.cpp
  src/spar_screen.hpp
  src/spar_worker.cpp
  src/spar_worker.hpp
  src/squares.cpp
  src/squares.hpp
  src/tables.cpp
  src/tables.hpp)
add_library(chesscoach::app_lib ALIAS app_lib)
target_include_directories(app_lib PUBLIC src)
target_link_libraries(app_lib PUBLIC chesscoach::core Qt6::Widgets Qt6::Svg Qt6::SvgWidgets
                              PRIVATE chesscoach::warnings)

set(chesscoach_pieces wK wQ wR wB wN wP bK bQ bR bB bN bP)
list(TRANSFORM chesscoach_pieces PREPEND "${PROJECT_SOURCE_DIR}/data/pieces/cburnett/")
list(TRANSFORM chesscoach_pieces APPEND ".svg")
qt_add_resources(app_lib "pieces"
  PREFIX "/pieces"
  BASE "${PROJECT_SOURCE_DIR}/data/pieces"
  FILES ${chesscoach_pieces})
# The two themes, one stylesheet each (docs/reference/app.md, **Themes**).
qt_add_resources(app_lib "themes"
  PREFIX "/themes"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
  FILES resources/light.qss resources/dark.qss)

add_executable(app
  src/main.cpp)
set_target_properties(app PROPERTIES OUTPUT_NAME chess-coach)
target_link_libraries(app PRIVATE chesscoach::app_lib chesscoach::warnings)

if(BUILD_TESTING)
  # The fixture opening database the Explore tests open, written into core's schema with
  # SQLiteCpp, as the CLI's tests write theirs.
  add_library(app_test_fixture STATIC
    tests/fixture_db.cpp
    tests/fixture_db.hpp)
  target_include_directories(app_test_fixture PUBLIC tests)
  target_link_libraries(app_test_fixture PUBLIC chesscoach::core PRIVATE SQLiteCpp
                                                                          chesscoach::warnings)

  # Each UI test saves its screenshots here, as <test>-<state>.png (tools/ui_tests.sh).
  set(chesscoach_screenshots "${PROJECT_BINARY_DIR}/ui-screenshots")
  foreach(test IN ITEMS ui_board_test ui_main_window_test ui_explore_test ui_engine_test
                       ui_spar_test ui_repertoire_test ui_drill_test ui_file_chooser_test)
    chesscoach_add_test(app_${test} MODULE app KIND ui
                        SOURCES tests/${test}.cpp tests/ui_harness.hpp
                        LIBS chesscoach::app_lib app_test_fixture)
    target_compile_definitions(app_${test}
                               PRIVATE "CHESSCOACH_UI_SCREENSHOT_DIR=\"${chesscoach_screenshots}\"")
  endforeach()
  # The engine tests drive core's fake engine through tests/fake-stockfish.sh, set as the binary.
  foreach(test IN ITEMS ui_engine_test ui_spar_test)
    add_dependencies(app_${test} fake-uci)
    target_compile_definitions(app_${test}
                               PRIVATE CHESSCOACH_FAKE_UCI="$<TARGET_FILE:fake-uci>"
                                       CHESSCOACH_FAKE_STOCKFISH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fake-stockfish.sh")
  endforeach()
endif()
