# chess-library has no releases: this overlay port pins a master commit by its SHA512
# (docs/reference/testing-tools.md, **Dependencies**). Header-only: the single chess.hpp is
# installed as include/chess-library/chess.hpp.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Disservin/chess-library
    REF 3af102c53752472c208d48921ece705b1f0d1626
    SHA512 e543f67edc1c60504712c372011f4f3a0a741c4fc57f0d06c657cba55d39e891434e9cb215d114127072b3328141d1edcf7cf59b51158cad1207818aacf5db32
    HEAD_REF master
)

file(INSTALL "${SOURCE_PATH}/include/chess.hpp" DESTINATION "${CURRENT_PACKAGES_DIR}/include/chess-library")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
