#!/bin/sh
# The fake engine standing in for Stockfish in the CLI's and the app's engine tests, which set it
# as the binary: execs core's fake-uci, which CHESSCOACH_FAKE_UCI names, with the script
# CHESSCOACH_FAKE_UCI_SCRIPT (normal when unset) and the delay CHESSCOACH_FAKE_UCI_DELAY (0 when
# unset), logging every line it receives to CHESSCOACH_FAKE_UCI_LOG when that is set
# (core/tests/fake_uci.cpp).
exec "$CHESSCOACH_FAKE_UCI" "${CHESSCOACH_FAKE_UCI_SCRIPT:-normal}" "${CHESSCOACH_FAKE_UCI_DELAY:-0}" \
  ${CHESSCOACH_FAKE_UCI_LOG:+"$CHESSCOACH_FAKE_UCI_LOG"}
