# Local preflight gate (Windows). Thin wrapper around tools/preflight.py.
# Usage:
#   tools\preflight.ps1
#   tools\preflight.ps1 -Full
#   tools\preflight.ps1 -NoBuild
#   tools\preflight.ps1 -Only vmdiff-bedslide,sweepcollide
param(
    [switch]$Full,
    [switch]$NoBuild,
    [switch]$SkipInputs,
    [string]$Only = "",
    [int]$Jobs = 4,
    [string]$Json = "build/preflight.json",
    [string]$Target = "ct"
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root
$py = "python"
if (-not (Get-Command python -ErrorAction SilentlyContinue)) { $py = "py" }
$argsList = @("tools/preflight.py", "--jobs", "$Jobs", "--json", $Json, "--target", $Target)
if ($Full) { $argsList += "--full" }
if ($NoBuild) { $argsList += "--no-build" }
if ($SkipInputs) { $argsList += "--skip-inputs" }
if ($Only) {
    $argsList += "--only"
    $argsList += ($Only -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}
& $py @argsList
exit $LASTEXITCODE
