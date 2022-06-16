#!/usr/bin/env bash

PC_BENCH="$1/bench-prod-cons"
ED_BENCH="$1/bench-enq-deq"

if [[ -z "$2" ]]; then
  QUEUE_FILTER='.*'
else
    QUEUE_FILTER="$2"
fi

{
  "$PC_BENCH" -f cpp-res-rs-p1c1.csv -t 8 8 24 24 -w 8 -r 16 64 256 512 1024 2048 4096 8192 16384 -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-rs-p2c1.csv -t 10 5 32 16 -w 8 -r 16 64 256 512 1024 2048 4096 8192 16384 -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-rs-p1c2.csv -t 5 10 16 32 -w 8 -r 16 64 256 512 1024 2048 4096 8192 16384 -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-rs-p2c1b.csv -t 10 5 32 16 -w 8 -r 16 64 256 512 1024 2048 4096 8192 16384 -b -q "$QUEUE_FILTER"
  "$PC_BENCH" -f cpp-res-rs-p1c2b.csv -t 5 10 16 32 -w 8 -r 16 64 256 512 1024 2048 4096 8192 16384 -b -q "$QUEUE_FILTER"
  "$ED_BENCH" -f cpp-res-rs-enq-deq.csv -t 16 48 -w 8 -r 16 64 256 512 1024 2048 4096 8192 16384 -q "$QUEUE_FILTER"
} > cpp-res-rs.txt 2>&1
