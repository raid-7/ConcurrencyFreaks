#!/usr/bin/env bash

PC_BENCH="$1/bench-prod-cons"
ED_BENCH="$1/bench-enq-deq"

{
  "$PC_BENCH" -f cpp-res-p1c1-h.csv -t 24 24 32 32 -w 8
  "$PC_BENCH" -f cpp-res-p2c1-h.csv -t 40 20 -w 8
  "$PC_BENCH" -f cpp-res-p1c2-h.csv -t 20 40 -w 8
  "$PC_BENCH" -f cpp-res-p2c1b-h.csv -t 40 20 -w 8 -b
  "$PC_BENCH" -f cpp-res-p1c2b-h.csv -t 20 40 -w 8 -b
  "$PC_BENCH" -f cpp-res-mpsc-h.csv -t 52 1 62 1 -w 8 24
  "$PC_BENCH" -f cpp-res-spmc-h.csv -t 1 52 1 62 -w 8 24
  "$PC_BENCH" -f cpp-res-mpscb-h.csv -t 52 1 62 1 -w 8 24 -b
  "$PC_BENCH" -f cpp-res-spmcb-h.csv -t 1 52 1 62 -w 8 24 -b
} > cpp-res.txt 2>&1
