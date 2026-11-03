# Run by CPack on the staged chess-coach.app before it makes the disk image
# (CPACK_PRE_BUILD_SCRIPTS, set by cmake/packaging.cmake, which passes the CPACK_CHESSCOACH_
# variables; docs/reference/testing-tools.md, **Packaging**). Four steps, the signature last
# because each step before it changes the bundle. Any failure stops the package.
#
# The engine: CHESS_COACH_STOCKFISH from the environment, else stockfish on the PATH, copied
# unmodified to Contents/Resources/engine/stockfish, with the GPL-3.0 text beside it as COPYING
# and SOURCE.txt naming the exact version (the `id name` of its `uci` answer), the source of that
# release and how the binary was obtained (from Homebrew's install receipt when it is a Homebrew
# keg's). No binary, or one that is not a numbered release, fails the package.
#
# Qt: macdeployqt with -no-plugins, given the three plugins the app needs as executables to
# deploy for: the cocoa platform, the macOS style, and the offscreen platform for headless runs
# (tools/package.sh launches the bundled app with it). Homebrew's qt installs every module's
# plugins in one directory, and macdeployqt's own plugin set would deploy the virtual keyboard
# and through it QtQuick, QtQml and QtNetwork. qt.conf then points Qt at the bundle's PlugIns,
# as macdeployqt writes it when it deploys plugins itself, so a Homebrew Qt on the machine is
# never loaded beside the bundle's; tools/package.sh checks that the bundled app loads no
# library from outside the bundle but the system's.
#
# The licences: every library macdeployqt copied in beside the Qt frameworks comes from a
# Homebrew formula, the keg <brew prefix>/opt/*/lib/<library> resolves to, whose licence text
# must be legal/<formula>.txt (the name without an @<version>), so that no dependency Homebrew's
# Qt gains can ship without its licence; NOTICE names each.
#
# The signature: an ad-hoc signature over the whole bundle, which Apple silicon requires once
# macdeployqt has rewritten the binaries; releases are otherwise unsigned (CLAUDE.md,
# **Release notes**).
cmake_minimum_required(VERSION 3.28)

set(app "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/${CPACK_CHESSCOACH_BUNDLE}")
set(contents "${app}/Contents")

# The engine.
if(NOT "$ENV{CHESS_COACH_STOCKFISH}" STREQUAL "")
  set(stockfish "$ENV{CHESS_COACH_STOCKFISH}")
else()
  # The PATH alone: not CMake's own prefixes, and not an app bundle such as a Stockfish.app GUI
  # in /Applications, which find_program would otherwise match on a case-insensitive disk.
  set(CMAKE_FIND_APPBUNDLE NEVER)
  find_program(stockfish stockfish NO_CACHE NO_PACKAGE_ROOT_PATH NO_CMAKE_PATH
               NO_CMAKE_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)
endif()
if(NOT stockfish OR NOT EXISTS "${stockfish}" OR IS_DIRECTORY "${stockfish}")
  message(FATAL_ERROR "package: no Stockfish to bundle: set CHESS_COACH_STOCKFISH to a Stockfish "
                      "binary or put stockfish on the PATH (brew install stockfish)")
endif()
file(REAL_PATH "${stockfish}" stockfish)
set(uci_input "${CPACK_TOPLEVEL_DIRECTORY}/stockfish-uci.txt")
file(WRITE "${uci_input}" "uci\nquit\n")
execute_process(
  COMMAND "${stockfish}"
  INPUT_FILE "${uci_input}"
  OUTPUT_VARIABLE uci_output
  RESULT_VARIABLE uci_result
  TIMEOUT 30)
if(NOT uci_result EQUAL 0 OR NOT uci_output MATCHES "\nid name Stockfish ([^\r\n]+)")
  message(FATAL_ERROR "package: ${stockfish} did not answer uci with `id name Stockfish "
                      "<version>` (exit ${uci_result})")
endif()
set(version "${CMAKE_MATCH_1}")
if(NOT version MATCHES "^[0-9]+(\\.[0-9]+)*$")
  message(FATAL_ERROR "package: ${stockfish} is Stockfish ${version}, not a numbered release, so "
                      "SOURCE.txt cannot name its source: bundle a release build")
endif()
set(obtained "Copied unmodified from ${stockfish} on the build machine.")
if(stockfish MATCHES "^(.*/Cellar/([^/]+)/[^/]+)/")
  set(keg "${CMAKE_MATCH_1}")
  set(formula "${CMAKE_MATCH_2}")
  if(EXISTS "${keg}/INSTALL_RECEIPT.json")
    file(READ "${keg}/INSTALL_RECEIPT.json" receipt)
    string(JSON bottle ERROR_VARIABLE json_error GET "${receipt}" poured_from_bottle)
    string(JSON formula_version ERROR_VARIABLE json_error GET "${receipt}" source versions stable)
    if(bottle)
      set(how "the Homebrew bottle of the ${formula} formula, version ${formula_version}")
    else()
      set(how "built from source by Homebrew's ${formula} formula, version ${formula_version}")
    endif()
    set(obtained "${how} (https://formulae.brew.sh/formula/${formula}), copied unmodified.")
  endif()
endif()
set(engine "${contents}/Resources/engine")
file(MAKE_DIRECTORY "${engine}")
file(COPY_FILE "${stockfish}" "${engine}/stockfish")
file(CHMOD "${engine}/stockfish" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
           GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(COPY_FILE "${CPACK_CHESSCOACH_LEGAL}/stockfish.txt" "${engine}/COPYING")
file(WRITE "${engine}/SOURCE.txt"
     "Stockfish ${version}, a separate program that chess-coach runs over the UCI protocol.\n"
     "Licence: the GNU General Public License version 3, in COPYING beside this file.\n"
     "Source: https://github.com/official-stockfish/Stockfish/releases/tag/sf_${version}\n"
     "Binary: ${obtained}\n")
message(STATUS "package: bundled Stockfish ${version} from ${stockfish}")

# Qt.
if(NOT CPACK_CHESSCOACH_MACDEPLOYQT)
  message(FATAL_ERROR "package: macdeployqt not found: install Homebrew's qt (brew install qt) "
                      "and configure again")
endif()
set(plugin_args "")
foreach(plugin IN ITEMS platforms/libqcocoa.dylib platforms/libqoffscreen.dylib
                        styles/libqmacstyle.dylib)
  cmake_path(GET plugin PARENT_PATH plugin_dir)
  file(MAKE_DIRECTORY "${contents}/PlugIns/${plugin_dir}")
  file(COPY_FILE "${CPACK_CHESSCOACH_QT_PLUGINS}/${plugin}" "${contents}/PlugIns/${plugin}")
  list(APPEND plugin_args "-executable=${contents}/PlugIns/${plugin}")
endforeach()
execute_process(
  COMMAND "${CPACK_CHESSCOACH_MACDEPLOYQT}" "${app}" -no-plugins -no-codesign ${plugin_args}
  RESULT_VARIABLE deploy_result)
if(NOT deploy_result EQUAL 0)
  message(FATAL_ERROR "package: macdeployqt failed (exit ${deploy_result})")
endif()
file(WRITE "${contents}/Resources/qt.conf" "[Paths]\nPlugins = PlugIns\n")

# The licences.
file(GLOB libraries RELATIVE "${contents}/Frameworks" "${contents}/Frameworks/*.dylib")
set(unlicensed "")
foreach(library IN LISTS libraries)
  file(GLOB found "${CPACK_CHESSCOACH_BREW_PREFIX}/opt/*/lib/${library}")
  set(formulae "")
  foreach(path IN LISTS found)
    # opt/ also links a formula's aliases (freetype2, pzstd): the keg in Cellar/ names it.
    file(REAL_PATH "${path}" path)
    string(REGEX REPLACE "^.*/Cellar/([^/@]+)(@[^/]*)?/.*$" "\\1" formula "${path}")
    list(APPEND formulae "${formula}")
  endforeach()
  list(REMOVE_DUPLICATES formulae)
  list(LENGTH formulae count)
  if(NOT count EQUAL 1 OR NOT EXISTS "${CPACK_CHESSCOACH_LEGAL}/${formulae}.txt")
    list(APPEND unlicensed "${library} (formula: ${formulae})")
  endif()
endforeach()
if(unlicensed)
  list(JOIN unlicensed ", " unlicensed)
  message(FATAL_ERROR "package: no licence text in legal/ for ${unlicensed}: add "
                      "legal/<formula>.txt and its entry in NOTICE")
endif()

# The signature.
execute_process(COMMAND codesign --force --deep --sign - "${app}" RESULT_VARIABLE sign_result)
if(NOT sign_result EQUAL 0)
  message(FATAL_ERROR "package: codesign failed (exit ${sign_result})")
endif()
