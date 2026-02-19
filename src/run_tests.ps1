# This file will be placed in the install directory and can be run from there.

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $scriptDir

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
