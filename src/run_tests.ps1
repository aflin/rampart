# This file will be placed in the install directory and can be run from there.

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $scriptDir

# Check for dev/shm directory (required by Cygwin runtime)
$devShm = Join-Path (Split-Path -Parent $scriptDir) "dev\shm"
if (-not (Test-Path $devShm)) {
    Write-Host "The directory '$devShm' is required by the Cygwin runtime but does not exist."
    $resp = Read-Host "Create it? (Y/n)"
    if ($resp -eq '' -or $resp -match '^[Yy]') {
        New-Item -ItemType Directory -Path $devShm -Force | Out-Null
        Write-Host "Created '$devShm'"
    } else {
        Write-Host "Cannot run tests without '$devShm'. Exiting."
        exit 1
    }
}

$failed = 0
$passed = 0
$testFiles = Get-ChildItem -Path "test\*-test.js" | Sort-Object Name

foreach ($test in $testFiles) {
    Write-Host ""
    Write-Host $test.Name
    & "bin\rampart.exe" $test.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Test $($test.Name) failed" -ForegroundColor Red
        $failed++
    } else {
        $passed++
    }
}

# also run the rampart-url.js, which has its own tests
Write-Host ""
Write-Host "modules\rampart-url.js"
& "bin\rampart.exe" "modules\rampart-url.js"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Test rampart-url.js failed" -ForegroundColor Red
    $failed++
} else {
    $passed++
}

Write-Host ""
if ($failed -gt 0) {
    Write-Host "$passed passed, $failed failed." -ForegroundColor Red
    exit 1
} else {
    Write-Host "All $passed tests passed." -ForegroundColor Green
}
