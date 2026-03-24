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
TOTAL_FAIL_COUNT=0
FAIL_SUMMARY=""
TMPOUT=$(mktemp)

run_test() {
    local testfile="$1"
    local testname=$(basename "$testfile")
    echo
    echo $RAMPART $testfile
    if echo "$testname" | grep -q '^transpile'; then
        $RAMPART "$testfile" 2>/dev/null | tee "$TMPOUT"
    else
        $RAMPART "$testfile" 2>&1 | tee "$TMPOUT"
    fi
    local exit_code=${PIPESTATUS[0]}

    # Count FAILED lines in output
    local fail_count=$(grep -c '>>>>> FAILED <<<<<' "$TMPOUT")

    if [ "$exit_code" != "0" ] || [ "$fail_count" -gt 0 ]; then
        FAILED=$((FAILED+1))
        TOTAL_FAIL_COUNT=$((TOTAL_FAIL_COUNT+fail_count))
        # Collect the specific failed test names
        local fail_lines=$(grep '>>>>> FAILED <<<<<' "$TMPOUT" | sed 's/ *- *>>>>> FAILED <<<<<.*//')
        while IFS= read -r line; do
            [ -z "$line" ] && continue
            FAIL_SUMMARY="${FAIL_SUMMARY}  ${testname}: ${line}\n"
        done <<< "$fail_lines"
        # If exit was non-zero but no FAILED lines, report the last few lines as context
        if [ "$fail_count" -eq 0 ]; then
            local errmsg=$(tail -3 "$TMPOUT" | head -1)
            FAIL_SUMMARY="${FAIL_SUMMARY}  ${testname}: (exit code ${exit_code}) ${errmsg}\n"
            TOTAL_FAIL_COUNT=$((TOTAL_FAIL_COUNT+1))
        fi
    else
        PASSED=$((PASSED+1))
    fi
}

for i in $(ls ${TESTDIR}/*-test.js); do
    run_test "$i"
done

# also run the rampart-url.js, which has its own tests
MODPATH=$($RAMPART -c "console.log(process.modulesPath)")
URLJS="${MODPATH}/rampart-url.js"
run_test "$URLJS"

rm -rf ${TESTDIR}/tmp-test
rm -f "$TMPOUT"

echo
if [ "$FAILED" -gt 0 ]; then
    echo "$PASSED passed, $FAILED failed ($TOTAL_FAIL_COUNT individual test failures)."
    echo
    echo "Failed tests:"
    echo -e "$FAIL_SUMMARY"
    exit 1
else
    echo "All $PASSED tests passed."
fi
