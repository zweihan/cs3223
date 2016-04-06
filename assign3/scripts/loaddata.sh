#!/usr/bin/env bash

# Assignment 3 script to load data into relations R and S given two parameters: ||R|| and join factor 
# parameter 1 = number of tuples in relation R
# parameter 2 = join factor (ie number of tuples in relation S that join with each R-tuple)

DBNAME=assign3

# check that server is running
if ! pg_ctl status > /dev/null; then
	echo "ERROR: postgres server is not running!"
	exit 1;
fi


if [ $# -ne 2 ]; then
	echo "ERROR: missing two parameters: (1) number of R-tuples and (2) join factor"
	return 1;
fi
echo "Number of tuples in relation R = $1"
echo "Join factor = $2"
echo "Running psql to load data ..." 



# set PORTOPTION appropriately depending on whether server is running with non-default port number
PORTNUMBER=$(pg_ctl status | grep "\-p" | awk '{print $3}')
PORTNUMBER=${PORTNUMBER//\"/}
if [ "${PORTNUMBER}" ]; then
	PORTOPTION=" -p $PORTNUMBER"
else
	PORTOPTION=""
fi

# load data and create indexes
psql -e -v nr=$1 -v jf=$2 ${PORTOPTION} $DBNAME <<EOF
DROP INDEX IF EXISTS ra_idx;
DROP INDEX IF EXISTS sy_idx;
DROP TABLE IF EXISTS r;
DROP TABLE IF EXISTS s;
-- Relation r(a,b,c)
SELECT a, a AS b, trunc(random() * 1000000) as c  INTO r FROM (SELECT * FROM generate_series(1,:nr)) AS temp(a);
-- Relation s(x,y,z)
SELECT x, (x % :nr) + 1 AS y, trunc(random() * 1000000) as z INTO s FROM (SELECT * FROM generate_series(1, :nr * :jf)) AS temp(x);
CREATE INDEX ra_idx ON r (a);
CREATE INDEX sy_idx ON s (y);
VACUUM ANALYZE;
\q
EOF

