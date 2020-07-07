#!/bin/bash

srcdir=`dirname $0`
dbranch=""
commitsecs=`(cd $srcdir && git log -1 --format=%cd --date=raw) | cut -d" " -f 1`
# Try and get the date of the commit.
# If that fails, use the current time.
if ! [[ "$commitsecs" =~ ^[0-9]+$ ]]; then
	commitsecs=`date +%s`;
	dbranch="-compiled";
fi
if ! [[ "$commitsecs" =~ ^[0-9]+$ ]]; then
	echo "Could not get commit time or current time"
	exit 1;
fi
branch=`(cd $srcdir && git rev-parse --abbrev-ref HEAD)`
dirty=`[[ -z $(cd $srcdir && git status -s) ]] || echo '-dirty'`
if [[ $branch = master ]]; then
	/bin/true ;
else
	dbranch=`echo "$dbranch-$branch"`
fi

cat << EOF > texver.c
#include "txcoreconfig.h"
const long TxSeconds = $commitsecs;

char *
TXtexisver()
{
	static char txver[] = "$1.$2.$commitsecs$dbranch$dirty";
	return txver;
}

EOF
