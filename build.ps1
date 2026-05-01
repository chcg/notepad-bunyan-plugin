param(
    [string]$Configuration = "Release",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$BuildDir = "build"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Push-Location $PSScriptRoot
try {
    $cmake = Get-Command cmake -ErrorAction Stop

    & $cmake.Source -S . -B $BuildDir -G $Generator
    & $cmake.Source --build $BuildDir --config $Configuration
}
finally {
    Pop-Location
}