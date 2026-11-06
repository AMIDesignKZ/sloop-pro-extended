# System 37 Windows entry point. Keep the established build implementation as the compatibility backend.
# This wrapper preserves existing WSL/toolchain configuration and output handling.
param([switch]$NoServe, [switch]$Diag, [switch]$SiteOnly, [switch]$Publish)
$ErrorActionPreference = 'Stop'
$legacy = Join-Path $PSScriptRoot 'build-sloop-pro.ps1'
if (-not (Test-Path $legacy)) { throw "Missing build backend: $legacy" }
& $legacy @PSBoundParameters
exit $LASTEXITCODE
