#!/usr/bin/env bash

DBNAME=assign3
DIR=~/assign3

cd $DIR
chmod u+x *.sh

# install dropdbbuffers extension
if [ -d ~/postgresql-9.4.5/contrib/dropdbbuffers ]; then
	rm -rf ~/postgresql-9.4.5/contrib/dropdbbuffers
fi
cp -r ${DIR}/dropdbbuffers ~/postgresql-9.4.5/contrib
cd ~/postgresql-9.4.5/contrib/dropdbbuffers
make && make install 

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


# if database doesn't exists, create database
if ! psql -l ${PORTOPTION} | grep -q "$DBNAME"; then
	echo "Creating database ${DBNAME} ..."
	createdb "${DBNAME}"
fi

# create dropdbbuffers extension
psql ${PORTOPTION} $DBNAME <<EOF
CREATE EXTENSION IF NOT EXISTS dropdbbuffers;
\q
EOF

