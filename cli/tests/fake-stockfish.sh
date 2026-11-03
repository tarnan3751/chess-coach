#!/bin/sh
# The fake engine standing in for Stockfish in cli_engine_commands_test: execs core's fake-uci,
# which CHESSCOACH_FAKE_UCI names, with its normal script, logging every line it receives to
# CHESSCOACH_FAKE_UCI_LOG when that is set (core/tests/fake_uci.cpp).
exec "$CHESSCOACH_FAKE_UCI" normal 0 ${CHESSCOACH_FAKE_UCI_LOG:+"$CHESSCOACH_FAKE_UCI_LOG"}
