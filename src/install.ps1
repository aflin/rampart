# This file will be placed in the install directory and can be run from there.

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
& "$scriptDir\bin\rampart.exe" "$scriptDir\install-helper.js"
