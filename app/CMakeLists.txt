# app: chess-coach, the Qt Widgets application (CLAUDE.md, **app/ is presentation**;
# docs/reference/app.md). The only module that finds or links Qt, which comes from the system
# through CMAKE_PREFIX_PATH, never from vcpkg. The window, the board and the screens are app_lib,
# which the executable and the UI tests link; main.cpp only parses the command line. The piece
# set is compiled in as a Qt resource, so the app needs no files beside it to draw the board.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Svg SvgWidgets Test)
set(CMAKE_AUTOMOC ON)

add_library(app_lib STATIC
  src/board_scene.cpp
  src/board_scene.hpp
  src/board_theme.cpp
  src/board_theme.hpp
  src/board_view.cpp
  src/board_view.hpp
  src/main_window.cpp
  src/main_window.hpp
  src/piece_set.cpp
  src/piece_set.hpp
  src/promotion_chooser.cpp
  src/promotion_chooser.hpp
  src/squares.cpp
  src/squares.hpp)
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

add_executable(app
  src/main.cpp)
set_target_properties(app PROPERTIES OUTPUT_NAME chess-coach)
target_link_libraries(app PRIVATE chesscoach::app_lib chesscoach::warnings)

if(BUILD_TESTING)
  # Each UI test saves its screenshots here, as <test>-<state>.png (tools/ui_tests.sh).
  set(chesscoach_screenshots "${PROJECT_BINARY_DIR}/ui-screenshots")
  chesscoach_add_test(app_main_window_test MODULE app KIND ui
                      SOURCES tests/main_window_test.cpp
                      LIBS chesscoach::app_lib)
  foreach(test IN ITEMS ui_board_test)
    chesscoach_add_test(app_${test} MODULE app KIND ui
                        SOURCES tests/${test}.cpp tests/ui_harness.hpp
                        LIBS chesscoach::app_lib)
    target_compile_definitions(app_${test}
                               PRIVATE "CHESSCOACH_UI_SCREENSHOT_DIR=\"${chesscoach_screenshots}\"")
  endforeach()
endif()
