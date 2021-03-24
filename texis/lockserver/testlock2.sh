#!/bin/bash


ncat --output /dev/tty --sh-exec ./locks010.sh localhost 40713
