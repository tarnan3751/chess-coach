#!/bin/sh
# The fake engine standing in for Stockfish in cli_spar_test: execs core's fake-uci, which
# CHESSCOACH_FAKE_UCI names, with its legal script, which answers each search with the first legal
# move of the position it was given, logging every line it receives to CHESSCOACH_FAKE_UCI_LOG
# when that is set (core/tests/fake_uci.cpp).
exec "$CHESSCOACH_FAKE_UCI" legal 0 ${CHESSCOACH_FAKE_UCI_LOG:+"$CHESSCOACH_FAKE_UCI_LOG"}
