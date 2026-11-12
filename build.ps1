# Compile Drake's Box SI4735.
# The boot line "ROM:" is the compiler __DATE__. Every compile refreshes it.
$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
$lib = Join-Path $root "third_party\Si4735"
$out = Join-Path $root "build"

if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
  Write-Error "arduino-cli was not found on PATH."
}
if (-not (Test-Path (Join-Path $lib "Si4735.h"))) {
  Write-Error "Missing $lib"
}

if (Test-Path $out) {
  Remove-Item $out -Recurse -Force
}
New-Item -ItemType Directory -Path $out | Out-Null

& arduino-cli compile --fqbn arduino:avr:uno --library $lib --output-dir $out $root
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$elf = Get-ChildItem -Path $out -Filter *.elf | Select-Object -First 1
if (-not $elf) {
  Write-Error "Compile finished without an ELF in $out"
}

$bytes = [System.IO.File]::ReadAllBytes($elf.FullName)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
$found = [regex]::Matches($text, "[A-Z][a-z]{2} [ \d]\d \d{4}") |
  ForEach-Object { $_.Value } |
  Select-Object -Unique
if (-not $found) {
  Write-Error "No compiler build date was found in $($elf.Name)."
}

$stamp = ($found -join ", ")
@(
  "Build date: $stamp"
  "ELF: $($elf.Name)"
  "This date is __DATE__ from the compiler. The LCD shows it after ROM:."
) | Set-Content -Path (Join-Path $out "BUILD.txt") -Encoding ascii

Write-Host "Build date stamped in firmware: $stamp"
Write-Host "Noted in $(Join-Path $out 'BUILD.txt')"
