#!/bin/bash

cd "$(dirname "${BASH_SOURCE[0]}")" ; 

CURDIR=$(pwd -P)

cd - 2&>1 1>/dev/null

CURPY="$CURDIR/bin/python3"

rp=`which realpath`
if [ "$rp" == "" ]; then
	realpath(){
		$CURPY -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$1"
	}
fi

REX=$(which rex 2>/dev/null);
if [ "$REX" == "" ]; then
	if [ -e ${CURDIR}/../../bin/rex ]; then
		REX=$(realpath ${CURDIR}/../../bin/rex);
	elif [ -e ${CURDIR}/../../../bin/rex ]; then
		REX=$(realpath ${CURDIR}/../../../bin/rex);
	else
		echo "couldn't find rex executable"
		exit 1;
	fi
fi

for i in 2to3 idle3 pip3 pip3.11 pydoc3; do 
    $REX \
        -R"${CURPY}"\
        '>>#\!\P=!/bin/python*/bin/python=[^\n]*'\
        $CURDIR/bin/$i;
done
