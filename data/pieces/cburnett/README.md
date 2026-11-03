# The cburnett piece set

The twelve SVG pieces here are Colin M.L. Burnett's
([Cburnett](https://en.wikipedia.org/wiki/User:Cburnett)) standard transparent chess pieces from
Wikimedia Commons, fetched 2026-11-03 and committed unchanged except for their names and their
line ends (LF, as `.gitattributes` keeps every text file): `wK.svg` is
[`Chess_klt45.svg`](https://commons.wikimedia.org/wiki/File:Chess_klt45.svg), `bK.svg` is
[`Chess_kdt45.svg`](https://commons.wikimedia.org/wiki/File:Chess_kdt45.svg), and likewise for
Q, R, B, N and P (`l` light, `d` dark).

The author publishes each file under several licences (GFDL, BSD, GPL, and through the GFDL's
licence migration the Creative Commons Attribution-ShareAlike 3.0 Unported licence); chess-coach
uses them under [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/), with the
attribution in `NOTICE` and the About dialog. The set is the one lichess shows as cburnett, but
these copies come from Commons rather than from lila, which distributes its copy under GPLv2+.

The app bundles them through a Qt resource (`app/CMakeLists.txt`), under `:/pieces/cburnett/`.
