#!/bin/bash

# This file will be placed in the install directory and can be run from there.

SSOURCE=$(readlink -f "${BASH_SOURCE[0]}")
SDIR=$(dirname "${SSOURCE}")

if [ -e ${SDIR}/babel-test.js ]; then
   TESTDIR="${SDIR}"
elif [ -e ${SDIR}/test ] ; then
   TESTDIR="${SDIR}/test"
elif [ -e "${SDIR}/../test" ]; then
   TESTDIR="${SDIR}/../test"
else
   echo "Error: cannot find the test directory"
fi

if [ -x "${SDIR}/rampart" ]; then
    RAMPART="${SDIR}/rampart"
elif [ -x "${SDIR}/../build/src/rampart" ]; then
    RAMPART="${SDIR}/../build/src/rampart"
elif [ -x "${SDIR}/bin/rampart" ]; then
    RAMPART="${SDIR}/bin/rampart"
elif command -v rampart >/dev/null 2>&1; then
    RAMPART=$(command -v rampart)
else
    echo "Error: cannot find the rampart executable."
    echo "Make sure 'bin/rampart' exists relative to this script or 'rampart' is in your PATH."
    exit 1
fi

if [ `whoami` == 'root' ]; then
    echo "Some tests may fail if run as root."
    read -p "Continue? " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]] ; then
        echo
    else
    	exit
    fi
fi

PASSED=0
FAILED=0

for i in `ls ${TESTDIR}/*-test.js`; do
	echo
	echo $RAMPART $i
	$RAMPART $i
	if [ "$?" != "0" ]; then
		echo "Test ${i} failed"
		FAILED=$((FAILED+1))
	else
		PASSED=$((PASSED+1))
	fi;
done

# also run the rampart-url.js, which has its own tests
MODPATH=$($RAMPART -c "console.log(process.modulesPath)")
URLJS="${MODPATH}/rampart-url.js"
echo
echo $RAMPART $URLJS
$RAMPART $URLJS

if [ "$?" != "0" ]; then
    echo "Test rampart-url.js failed"
    FAILED=$((FAILED+1))
else
    PASSED=$((PASSED+1))
fi;

rm -rf ${TESTDIR}/tmp-test

echo
if [ "$FAILED" -gt 0 ]; then
    echo "$PASSED passed, $FAILED failed."
    exit 1
else
    echo "All $PASSED tests passed."
fi
