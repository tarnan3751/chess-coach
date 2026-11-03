# app: chess-coach, the Qt Widgets application (CLAUDE.md, **app/ is presentation**). The only
# module that finds or links Qt, which comes from the system through CMAKE_PREFIX_PATH, never
# from vcpkg.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Svg SvgWidgets Test)
set(CMAKE_AUTOMOC ON)

add_executable(app
  src/main.cpp
  src/main_window.cpp
  src/main_window.hpp)
set_target_properties(app PROPERTIES OUTPUT_NAME chess-coach)
target_link_libraries(app PRIVATE chesscoach::core Qt6::Widgets chesscoach::warnings)

if(BUILD_TESTING)
  chesscoach_add_test(app_main_window_test MODULE app KIND ui
                      SOURCES tests/main_window_test.cpp src/main_window.cpp src/main_window.hpp
                      LIBS chesscoach::core Qt6::Widgets)
  target_include_directories(app_main_window_test PRIVATE src)
endif()
