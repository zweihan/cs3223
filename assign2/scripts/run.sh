#!/usr/bin/env bash

# sample script for Question 1

DBNAME=assign2
for query in *.sql
do
	# reset statistics
	psql -c "SELECT pg_stat_reset();" ${DBNAME}
	# clear OS cache
	if  [ -e /opt/bin/dropcache ]; then
		# running on compute cluster
		/opt/bin/dropcache
	else
		# running on own machine
		#for osx: `sudo sh -c "sync && purge;"` "
		sudo sh -c "sync && purge;"
	fi
	psql -f $query ${DBNAME}
	# clear buffer pool
	psql -c "SELECT dropdbbuffers('assign2');" ${DBNAME}
	psql -c "SELECT pg_sleep(2);" ${DBNAME}
	# get run-time statistics
	psql -c "SELECT relname, heap_blks_read, heap_blks_hit, idx_blks_read, idx_blks_hit FROM pg_statio_all_tables WHERE relname = 'r';" ${DBNAME}
done
