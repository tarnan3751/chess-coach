# app: chess-coach, the Qt Widgets application (CLAUDE.md, **app/ is presentation**;
# docs/reference/app.md). The only module that finds or links Qt, which comes from the system
# through CMAKE_PREFIX_PATH, never from vcpkg. The window, the board and the screens are app_lib,
# which the executable and the UI tests link; main.cpp only parses the command line. The piece
# set, the stylesheets, the fonts, the icons and the sounds are compiled in as Qt resources, so the
# app needs no files beside it, nor any font on the system, to draw itself and play its sounds.
# The sounds play through NSSound on macOS, an Objective-C++ source linking the AppKit system
# framework, and through a null backend elsewhere (src/sounds.hpp); never Qt Multimedia, which
# links QtNetwork.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Svg SvgWidgets Test)
set(CMAKE_AUTOMOC ON)
if(APPLE)
  # The C++ compiler for the Objective-C++ too, so that a sanitized build (Homebrew's clang on
  # macOS, the root CMakeLists.txt) instruments and links both alike.
  if(NOT DEFINED CMAKE_OBJCXX_COMPILER)
    set(CMAKE_OBJCXX_COMPILER "${CMAKE_CXX_COMPILER}")
  endif()
  enable_language(OBJCXX)
endif()

add_library(app_lib STATIC
  src/about_dialog.cpp
  src/about_dialog.hpp
  src/application.cpp
  src/application.hpp
  src/board_row.cpp
  src/board_row.hpp
  src/board_scene.cpp
  src/board_scene.hpp
  src/board_theme.cpp
  src/board_theme.hpp
  src/board_view.cpp
  src/board_view.hpp
  src/columns.cpp
  src/columns.hpp
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
  src/fonts.cpp
  src/fonts.hpp
  src/help_dialog.cpp
  src/help_dialog.hpp
  src/icons.cpp
  src/icons.hpp
  src/locations.cpp
  src/locations.hpp
  src/main_window.cpp
  src/main_window.hpp
  src/main_window_drill.cpp
  src/main_window_repertoire.cpp
  src/move_list_delegate.cpp
  src/move_list_delegate.hpp
  src/piece_set.cpp
  src/piece_set.hpp
  src/profile_data.hpp
  src/profile_worker.cpp
  src/profile_worker.hpp
  src/promotion_chooser.cpp
  src/promotion_chooser.hpp
  src/rating_chooser.cpp
  src/rating_chooser.hpp
  src/repertoire_screen.cpp
  src/repertoire_screen.hpp
  src/result_delegate.cpp
  src/result_delegate.hpp
  src/settings.cpp
  src/settings.hpp
  src/settings_dialog.cpp
  src/settings_dialog.hpp
  src/sounds.cpp
  src/sounds.hpp
  src/spar_data.hpp
  src/spar_screen.cpp
  src/spar_screen.hpp
  src/spar_worker.cpp
  src/spar_worker.hpp
  src/squares.cpp
  src/squares.hpp
  src/study_palette.cpp
  src/study_palette.hpp
  src/tables.cpp
  src/tables.hpp
  src/wording.cpp
  src/wording.hpp)
add_library(chesscoach::app_lib ALIAS app_lib)
target_include_directories(app_lib PUBLIC src)
target_link_libraries(app_lib PUBLIC chesscoach::core Qt6::Widgets Qt6::Svg Qt6::SvgWidgets
                              PRIVATE chesscoach::warnings)
if(APPLE)
  target_sources(app_lib PRIVATE src/sounds_mac.mm)
  target_compile_options(app_lib PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
  target_link_libraries(app_lib PRIVATE "-framework AppKit")
endif()

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
# The Study look's fonts, static TTFs from Adobe's releases under the OFL (NOTICE), and its icons,
# one SVG each drawn in #000000, which icons.cpp recolours per theme.
qt_add_resources(app_lib "fonts"
  PREFIX "/fonts"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
  FILES resources/fonts/SourceSerif4-Semibold.ttf resources/fonts/SourceSans3-Regular.ttf
        resources/fonts/SourceSans3-Semibold.ttf resources/fonts/SourceSans3-Bold.ttf)
set(chesscoach_icons plus back flip chevron-down gear sound-on sound-off explore spar repertoire
                     drill)
list(TRANSFORM chesscoach_icons PREPEND "resources/icons/")
list(TRANSFORM chesscoach_icons APPEND ".svg")
qt_add_resources(app_lib "icons"
  PREFIX "/icons"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
  FILES ${chesscoach_icons})
# The five sounds, 16-bit 44.1 kHz mono WAVs written by tools/make_sounds.py (docs/reference/app.md,
# **Sounds**).
qt_add_resources(app_lib "sounds"
  PREFIX "/sounds"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/sounds"
  FILES resources/sounds/move.wav resources/sounds/capture.wav resources/sounds/check.wav
        resources/sounds/wrong.wav resources/sounds/interruption.wav)

add_executable(app
  src/main.cpp)
set_target_properties(app PROPERTIES OUTPUT_NAME chess-coach)
target_link_libraries(app PRIVATE chesscoach::app_lib chesscoach::warnings)

if(BUILD_TESTING)
  # The fixture opening database the Explore tests open, written into core's schema by the writer
  # it shares with the CLI's tests (core_test_fixture); its helpers edit files with SQLiteCpp.
  add_library(app_test_fixture STATIC
    tests/fixture_db.cpp
    tests/fixture_db.hpp)
  target_include_directories(app_test_fixture PUBLIC tests)
  target_link_libraries(app_test_fixture PUBLIC chesscoach::core core_test_fixture
                                         PRIVATE SQLiteCpp chesscoach::warnings)

  # The bundled fonts, the stylesheets and the themed icons; the window's column split; each on
  # an offscreen application of the test's own (tests/offscreen_application.hpp).
  chesscoach_add_test(app_resources_test MODULE app KIND unit
                      SOURCES tests/resources_test.cpp tests/offscreen_application.hpp
                      LIBS chesscoach::app_lib)
  chesscoach_add_test(app_layout_test MODULE app KIND unit
                      SOURCES tests/layout_test.cpp tests/offscreen_application.hpp
                      LIBS chesscoach::app_lib)
  # The wording of numbers, ratings, losses, days and months (wording.hpp).
  chesscoach_add_test(app_wording_test MODULE app KIND unit
                      SOURCES tests/wording_test.cpp
                      LIBS chesscoach::app_lib)
  # The sounds' unit tests, on app_lib without a window.
  chesscoach_add_test(app_sounds_test MODULE app KIND unit
                      SOURCES tests/sounds_test.cpp
                      LIBS chesscoach::app_lib)

  # Each UI test saves its screenshots here, as <test>-<state>.png (tools/ui_tests.sh).
  set(chesscoach_screenshots "${PROJECT_BINARY_DIR}/ui-screenshots")
  foreach(test IN ITEMS ui_board_test ui_main_window_test ui_explore_test ui_engine_test
                       ui_spar_test ui_repertoire_test ui_drill_test ui_file_chooser_test
                       ui_lookup_test)
    chesscoach_add_test(app_${test} MODULE app KIND ui
                        SOURCES tests/${test}.cpp tests/ui_harness.hpp
                        LIBS chesscoach::app_lib app_test_fixture)
    target_compile_definitions(app_${test}
                               PRIVATE "CHESSCOACH_UI_SCREENSHOT_DIR=\"${chesscoach_screenshots}\"")
  endforeach()
  # The engine tests set core's fake engine as the binary, through its wrapper in core's tests.
  foreach(test IN ITEMS ui_engine_test ui_spar_test ui_lookup_test)
    add_dependencies(app_${test} fake-uci)
    target_compile_definitions(app_${test}
                               PRIVATE CHESSCOACH_FAKE_UCI="$<TARGET_FILE:fake-uci>"
                                       CHESSCOACH_FAKE_STOCKFISH="${PROJECT_SOURCE_DIR}/core/tests/support/fake-stockfish.sh")
  endforeach()

  # The app's measurements (tests/measure.cpp; docs/reference/testing-tools.md, **Measuring**): a
  # program with its own command line, which tools/measure.sh drives on any database, so it is
  # declared here rather than through chesscoach_add_test, whose bench kind is Catch2's. ctest runs
  # every scenario once on the fixture database with the fake engine, a smoke run that fails only
  # when a step never completes; labelled bench, it runs in the release preset's bench step. It
  # uses the UI harness, so it is given the harness's screenshot directory like the UI tests.
  add_executable(app_measure tests/measure.cpp tests/ui_harness.hpp)
  target_link_libraries(app_measure PRIVATE chesscoach::warnings chesscoach::app_lib
                                            app_test_fixture Qt6::Test)
  target_compile_definitions(app_measure
                             PRIVATE "CHESSCOACH_UI_SCREENSHOT_DIR=\"${chesscoach_screenshots}\"")
  add_dependencies(app_measure fake-uci)
  add_test(NAME app_measure
           COMMAND app_measure all --fixture --runs 1 --tier 4
                   --engine "${PROJECT_SOURCE_DIR}/core/tests/support/fake-stockfish.sh")
  set_tests_properties(app_measure PROPERTIES
                       LABELS "app;bench"
                       ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CHESSCOACH_FAKE_UCI=$<TARGET_FILE:fake-uci>;CHESSCOACH_FAKE_UCI_SCRIPT=legal")
endif()
