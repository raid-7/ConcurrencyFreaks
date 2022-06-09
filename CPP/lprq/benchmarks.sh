#!/usr/bin/env bash

PC_BENCH="$1/bench-prod-cons"
ED_BENCH="$1/bench-enq-deq"

if [[ -z "$2" ]]; then
  QUEUE_FILTER='.*'
else
    QUEUE_FILTER="$2"
fi

{
  "$PC_BENCH" -f cpp-res-p1c1.csv -t 1 1 2 2 4 4 8 8 12 12 16 16 20 20 24 24 32 32 -w 8 -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-p2c1.csv -t 2 1 4 2 8 4 12 6 16 8 20 10 28 14 40 20 -w 8 -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-p1c2.csv -t 1 2 2 4 4 8 6 12 8 16 10 20 14 28 20 40 -w 8 -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-p2c1b.csv -t 2 1 4 2 8 4 12 6 16 8 20 10 28 14 40 20 -w 8 -b -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-p1c2b.csv -t 1 2 2 4 4 8 6 12 8 16 10 20 14 28 20 40 -w 8 -b -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-mpsc.csv -t 1 1 2 1 4 1 8 1 16 1 24 1 32 1 42 1 52 1 62 1 -w 8 24 -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-spmc.csv -t 1 1 1 2 1 4 1 8 1 16 1 24 1 32 1 42 1 52 1 62 -w 8 24 -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-mpscb.csv -t 1 1 2 1 4 1 8 1 16 1 24 1 32 1 42 1 52 1 62 1 -w 8 24 -b -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-spmcb.csv -t 1 1 1 2 1 4 1 8 1 16 1 24 1 32 1 42 1 52 1 62 -w 8 24 -b -q "$QUEUE_FILTER"
  "$ED_BENCH" -f cpp-res-enq-deq.csv -t 1 2 4 8 16 24 32 40 48 64 -w 8 -q "$QUEUE_FILTER"
} > cpp-res.txt 2>&1
