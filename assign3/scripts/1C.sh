#!/usr/bin/env bash

# Assignment 3
# Sample script to run experiment 1A for indexed nested loop join algorithm

DBNAME=assign3

# check that server is running
if ! pg_ctl status > /dev/null; then
	echo "ERROR: postgres server is not running!"
	exit 1;
fi


# set PORTOPTION appropriately depending on whether server is running with non-default port number
PORTNUMBER=$(pg_ctl status | grep "\-p" | awk '{print $3}')
PORTNUMBER=${PORTNUMBER//\"/}
if [ "${PORTNUMBER}" ]; then
	PORTOPTION=" -p $PORTNUMBER"
else
	PORTOPTION=""
fi


####################################### load data
./loaddata.sh 100000 10

####################################### vary selectivity of selection predicate
for algo in inlj smj-i smj hj
do
	IFS=$'\n' read -d '' -r -a lines < $algo
	echo "ALGOCHANGE: " $algo
	for i in 12500 25000 37500 50000 75000 100000
	do
		if  [ -e /opt/bin/dropcache ]; then
			# running on compute cluster node
			/opt/bin/dropcache
		elif [ -e /proc/sys/vm/drop_caches ]; then
			# running on own machine
			#for osx: `sudo sh -c "sync && purge;"` "
			sudo sh -c "sync && purge;"
			# sudo sh -c "sync; echo 3 > /proc/sys/vm/drop_caches"
		fi
		echo 'PARAMS: ' $i
		psql -e -v v=$i -v nl=${lines[0]} -v is=${lines[1]} -v mj=${lines[2]} -v hj=${lines[3]} ${PORTOPTION} $DBNAME <<EOF
		SET work_mem TO 4096;
		SET enable_nestloop = :nl;
		SET enable_indexscan = :is;
		SET enable_mergejoin = :mj;
		SET enable_hashjoin = :hj;
		SELECT pg_stat_reset();
		SELECT dropdbbuffers('assign3');
		EXPLAIN (ANALYZE, BUFFERS) SELECT z FROM S WHERE NOT EXISTS (SELECT * FROM r WHERE r.a=s.y AND r.b <= :v);
		SELECT pg_sleep(2);
		SELECT relname, heap_blks_read, heap_blks_hit, idx_blks_read, idx_blks_hit FROM pg_statio_all_tables WHERE relname IN ('r', 's');
		\q
EOF
done
done
echo 'ENDOFFILE__: '
