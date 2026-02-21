# This file will be placed in the install directory and can be run from there.

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
& "$scriptDir\bin\rampart.exe" "$scriptDir\install-helper.js"

# Refresh PATH in the current session so the user doesn't need to restart
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
