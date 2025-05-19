#!/bin/bash

# This file will be placed in the install directory and can be run from there.

SSOURCE==$(readlink -- "${BASH_SOURCE[0]}")
SDIR=$(dirname "${SSOURCE}")

# check quarantine flag on macos and remove if present
uname -a | grep -q Darwin && {
    xattr -p com.apple.quarantine ${SDIR}/bin/rampart  2>/dev/null && { 
        xattr -r -d com.apple.quarantine ${SDIR}
    }
}


${SDIR}/bin/rampart ${SDIR}/install-helper.js

