#!/bin/bash

# This file will be placed in the install directory and can be run from there.

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

for i in `ls test/*-test.js`; do
	echo
	echo $i
	bin/rampart $i
	if [ "$?" != "0" ]; then
		echo "Test ${i} failed"
		exit 1;
	fi;
done

# also run the rampart-url.js, which has its own tests
echo
echo bin/rampart modules/rampart-url.js

bin/rampart modules/rampart-url.js
if [ "$?" != "0" ]; then
    echo "Test rampart-url.js failed"
    exit 1;
fi;
