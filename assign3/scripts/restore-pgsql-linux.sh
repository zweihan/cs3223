#!/usr/bin/env bash

# Script to restore PostgreSQL on Linux machine

TARFILE=postgresql-9.4.5.tar
FILE=${TARFILE}.gz
SRCDIR=$HOME/postgresql-9.4.5
INSTALLDIR=$HOME/pgsql
DATADIR=/temp/pgdata

# setup PGDATA 
cat >> ~/.bash_profile <<EOF
export PGDATA=/temp/pgdata
EOF

if pg_ctl status > /dev/null; then
	# server is still running, stop it
	pg_ctl stop
fi

# remove any existing directories
if [ -d $SRCDIR ]; then
	rm -rf $SRCDIR
fi
if [ -d $INSTALLDIR ]; then
	rm -rf $INSTALLDIR
fi
if [ -d $DATADIR ]; then
	rm -rf $DATADIR
fi

# create new directories
mkdir -p ${INSTALLDIR}
mkdir -p ${DATADIR}


cd $HOME
if [ -e ${TARFILE} ]; then
        tar xvf ${TARFILE}
elif [ -e ${FILE} ]; then
	tar xvfz ${FILE}
else
	wget http://www.comp.nus.edu.sg/~cs3223/${FILE}
	# You could also download from https://ftp.postgresql.org/pub/source/v9.4.5/postgresql-9.4.5.tar.gz
	tar xvfz ${FILE}
fi
cd ${SRCDIR}


#configure for debugging
export CFLAGS="-O0 -g"
./configure --prefix=${INSTALLDIR} --enable-debug  --enable-cassert


make clean
make
make install
make install-docs

# initialize database
${INSTALLDIR}/bin/initdb -D ${DATADIR}

