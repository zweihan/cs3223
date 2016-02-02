#!/usr/bin/env bash

DBNAME=assign1

# check that server is running
if ! pg_ctl status > /dev/null; then
	echo "ERROR: postgres server is not running!"
	exit 1;
fi

# if database doesn't exists, create database
if ! psql -l | grep -q "$DBNAME"; then
	echo "Creating database ${DBNAME} ..."
	createdb "${DBNAME}"
fi

# check that number of shared buffer pages is configured to 16 pages
if ! psql -c "SHOW shared_buffers;" $DBNAME | grep -q "128kB" ; then
	echo "ERROR: restart server with 16 buffer pages!"
	exit 1;
fi

cd ~/assign1

# load data into movies relation if necessary
if ! psql -c "SELECT COUNT(*) FROM movies;" $DBNAME | grep "1681"; then
	psql -f ~/assign1/testdata/load-data.sql $DBNAME
fi


# create test_bufmgr extension if necessary
psql -c "CREATE EXTENSION IF NOT EXISTS test_bufmgr;" $DBNAME


# run tests
mkdir -p ~/assign1/testresults
for testno in  {0..9}
do
	resultfile="$HOME/assign1/testresults/result-$testno.txt"
	if [ -f ${resultfile} ]; then
		\rm ${resultfile}
	fi
	echo "Running test case ${testno} -> ${resultfile} ...."
	psql -c "SELECT test_bufmgr('movies', $testno);" $DBNAME 2> ${resultfile}
done

