#!/bin/bash

if [ -e ../bin/rampart ]; then
    RAMPART=`realpath ../bin/rampart`;
elif [ -e /usr/local/rampart/bin/rampart ]; then
    RAMPART="/usr/local/rampart/bin/rampart";
else
    RAMPART=`which rampart`;
fi

if [ "$RAMPART" == "" ]; then 
    echo "cannot locate rampart executable";
    exit 1;
fi

$RAMPART ./web_server_conf.js

