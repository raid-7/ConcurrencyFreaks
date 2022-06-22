#!/usr/bin/env bash

PC_BENCH="$1/bench-prod-cons"
ED_BENCH="$1/bench-enq-deq"

shift

{
  "$PC_BENCH" -f cpp-res-p1c1.csv -t 1 1 2 2 4 4 8 8 12 12 16 16 20 20 24 24 32 32 -w 8 "$@"
  "$PC_BENCH" -f cpp-res-p2c1.csv -t 2 1 4 2 8 4 12 6 16 8 20 10 28 14 40 20 -w 8 "$@"
  "$PC_BENCH" -f cpp-res-p1c2.csv -t 1 2 2 4 4 8 6 12 8 16 10 20 14 28 20 40 -w 8 "$@"
  "$PC_BENCH" -f cpp-res-p2c1b.csv -t 2 1 4 2 8 4 12 6 16 8 20 10 28 14 40 20 -w 8 -b "$@"
  "$PC_BENCH" -f cpp-res-p1c2b.csv -t 1 2 2 4 4 8 6 12 8 16 10 20 14 28 20 40 -w 8 -b "$@"
  "$PC_BENCH" -f cpp-res-pNcN+2.csv -t 1 3 2 4 4 6 8 10 16 18 24 26 30 32 -w 8 "$@"
  "$PC_BENCH" -f cpp-res-mpsc.csv -t 1 1 2 1 4 1 8 1 16 1 24 1 32 1 42 1 52 1 62 1 -w 8 24 "$@"
  "$PC_BENCH" -f cpp-res-spmc.csv -t 1 1 1 2 1 4 1 8 1 16 1 24 1 32 1 42 1 52 1 62 -w 8 24 "$@"
  "$PC_BENCH" -f cpp-res-mpscb.csv -t 1 1 2 1 4 1 8 1 16 1 24 1 32 1 42 1 52 1 62 1 -w 36 -b "$@"
  "$PC_BENCH" -f cpp-res-spmcb.csv -t 1 1 1 2 1 4 1 8 1 16 1 24 1 32 1 42 1 52 1 62 -w 36 -b "$@"
  "$ED_BENCH" -f cpp-res-enq-deq.csv -t 1 2 4 8 16 24 32 40 48 64 -w 8 "$@"
} > cpp-res.txt 2>&1
