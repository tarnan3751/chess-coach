# Packaging, macOS only: `cmake --build --preset release --target package` makes
# chess-coach-<version>-macos-<arch>.dmg with CPack's DragNDrop generator, holding chess-coach.app
# and a link to /Applications (CLAUDE.md, **Release notes**; docs/reference/testing-tools.md,
# **Packaging**). Linux and Windows packages come after 1.0.
#
# The app target stays a plain executable, so the dev and gate builds are untouched: the install
# rules below assemble the bundle in CPack's staging directory (the executable, the Info.plist,
# NOTICE, LICENSE and legal/, and the opening database when CHESS_COACH_BUNDLE_DB names one),
# then cmake/package_bundle.cmake, run on the staged bundle before the image is made, adds the
# Stockfish binary with its licence and source, deploys Qt with macdeployqt, checks that every
# library deployed with it has its licence in legal/, and signs the bundle ad hoc. Nothing here
# runs in a build; only `install` and `package` use it.

set(CHESS_COACH_BUNDLE_DB "" CACHE FILEPATH
    "An opening database to bundle as Contents/Resources/openings.db (empty: none)")

# macdeployqt from Homebrew's qt, the Qt the app is built against.
execute_process(
  COMMAND brew --prefix
  OUTPUT_VARIABLE chesscoach_brew_prefix
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
set(chesscoach_qt_keg "${chesscoach_brew_prefix}/opt/qt")
find_program(CHESS_COACH_MACDEPLOYQT macdeployqt HINTS "${chesscoach_qt_keg}/bin"
             DOC "macdeployqt, which deploys Qt into the bundle")

set(chesscoach_contents "chess-coach.app/Contents")
configure_file("${CMAKE_CURRENT_LIST_DIR}/Info.plist.in" "${PROJECT_BINARY_DIR}/Info.plist" @ONLY)
install(TARGETS app RUNTIME DESTINATION "${chesscoach_contents}/MacOS")
install(FILES "${PROJECT_BINARY_DIR}/Info.plist" DESTINATION "${chesscoach_contents}")
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/NOTICE"
        DESTINATION "${chesscoach_contents}/Resources")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/legal/"
        DESTINATION "${chesscoach_contents}/Resources/legal")
if(CHESS_COACH_BUNDLE_DB)
  install(FILES "${CHESS_COACH_BUNDLE_DB}" DESTINATION "${chesscoach_contents}/Resources"
          RENAME openings.db)
endif()

if(CMAKE_OSX_ARCHITECTURES)
  set(chesscoach_arch "${CMAKE_OSX_ARCHITECTURES}")
else()
  set(chesscoach_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_GENERATOR DragNDrop)
set(CPACK_PACKAGE_NAME chess-coach)
set(CPACK_PACKAGE_FILE_NAME "chess-coach-${PROJECT_VERSION}-macos-${chesscoach_arch}")
set(CPACK_DMG_VOLUME_NAME "chess-coach ${PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}")
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/package_bundle.cmake")
# For package_bundle.cmake, which CPack runs with the CPACK_ variables only.
set(CPACK_CHESSCOACH_BUNDLE "chess-coach.app")
set(CPACK_CHESSCOACH_MACDEPLOYQT "${CHESS_COACH_MACDEPLOYQT}")
set(CPACK_CHESSCOACH_QT_PLUGINS "${chesscoach_qt_keg}/share/qt/plugins")
set(CPACK_CHESSCOACH_BREW_PREFIX "${chesscoach_brew_prefix}")
set(CPACK_CHESSCOACH_LEGAL "${PROJECT_SOURCE_DIR}/legal")
include(CPack)
