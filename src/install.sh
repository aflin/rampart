#!/usr/bin/env bash

# This file will be placed in the install directory and can be run from there.

# resolve symlinks portably (macOS readlink lacks -f before 12.3)
SSOURCE="${BASH_SOURCE[0]}"
while [ -L "$SSOURCE" ]; do
    SDIR=$(cd -P "$(dirname "$SSOURCE")" >/dev/null 2>&1 && pwd)
    SSOURCE=$(readlink "$SSOURCE")
    [[ $SSOURCE != /* ]] && SSOURCE="${SDIR}/${SSOURCE}"
done
SDIR=$(cd -P "$(dirname "$SSOURCE")" >/dev/null 2>&1 && pwd)

# check quarantine flag on macos and remove if present
uname -a | grep -q Darwin && {
    xattr -p com.apple.quarantine ${SDIR}/bin/rampart  2>/dev/null && { 
        xattr -r -d com.apple.quarantine ${SDIR}
    }
}


${SDIR}/bin/rampart ${SDIR}/install-helper.js

