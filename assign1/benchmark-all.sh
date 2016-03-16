#!/bin/bash

# benchmark all three replacement policies

cd ~/cs3223/assign1
for p in clock lru lru2
do
	make $p
	pg_ctl stop
	postgres -B 32 &
	sleep 10
	echo "Benchmarking $p ..."
	bash ./benchmark-l.sh benchl-$p.txt
done
pg_ctl stop
