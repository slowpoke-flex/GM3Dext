# Keeps the standalone GML SDK example byte-identical to the copy shipped with 3DTEST.
#
#   .\tools\sync_gml_sdk.ps1          # copy 3DTEST SDK over gml_example
#   .\tools\sync_gml_sdk.ps1 -Check   # exit 1 if they differ (used by tools\check.ps1)
#
# The SDK is a single script file. Two copies exist because gml_example is the
# no-project reference drop for new users while 3DTEST is the live test project.
# They drift silently otherwise, and the example is the first thing a new user reads.

[CmdletBinding()]
param(
    [switch] $Check
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

$source = Join-Path $projectRoot '3DTEST\scripts\scr_gm3d\scr_gm3d.gml'
$target = Join-Path $projectRoot 'gml_example\scr_gm3d.gml'

# Get-FileHash is missing on some stripped-down hosts, so hash through .NET directly.
function Get-Sha256([string] $path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [System.BitConverter]::ToString($sha.ComputeHash([System.IO.File]::ReadAllBytes($path)))
    } finally {
        $sha.Dispose()
    }
}

foreach ($path in @($source, $target)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "GML SDK file not found: $path" }
}

$sourceHash = (Get-Sha256 $source)
$targetHash = (Get-Sha256 $target)

if ($sourceHash -eq $targetHash) {
    Write-Output "GML SDK is in sync: gml_example\scr_gm3d.gml"
    exit 0
}

if ($Check) {
    Write-Error "GML SDK is out of sync. Run .\tools\sync_gml_sdk.ps1 to refresh gml_example\scr_gm3d.gml from 3DTEST."
    exit 1
}

Copy-Item -LiteralPath $source -Destination $target -Force
Write-Output "Copied 3DTEST SDK to gml_example\scr_gm3d.gml"
exit 0
