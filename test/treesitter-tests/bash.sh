#!/usr/bin/env bash
# bash sample for rampart-treesitter tests.
# Covers: function_definition in both forms ('function foo' and 'foo()').

# Form 1: posix-style "foo()" syntax
add() {
    echo $(( $1 + $2 ))
}

no_args() {
    echo 42
}

# Form 2: "function foo" syntax (bash-specific)
function greet {
    echo "hi $1"
}

function farewell() {
    echo "bye $1"
}

# Local helper function
do_setup() {
    local tmp="/tmp/test"
    mkdir -p "$tmp"
}

# Last function in file
cleanup() {
    rm -rf /tmp/test
}

# Top-level commands (not functions, should not be in symbol output)
echo "starting..."
do_setup
greet "world"
cleanup
echo "done."
