#!/usr/bin/env bash

for testno in {0..9}
do
    cmp ./testresults/result-$testno.txt ./testresults-lru-solution/result-$testno.txt
done