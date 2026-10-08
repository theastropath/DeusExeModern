<#
.SYNOPSIS
    Builds the Deus Exe launcher.

.DESCRIPTION
    Locates MSBuild from any installed Visual Studio / Build Tools instance
    (via vswhere) and builds DeusExe.sln.

    Needs Visual Studio 2022 or newer with the C++ workload

.PARAMETER Configuration
    Solution configuration(s) to build, e.g. "Release".
    Defaults to every configuration in the solution.

.PARAMETER Rebuild
    Perform a full rebuild instead of an incremental build.

.PARAMETER Clean
    Clean the selected configurations instead of building them.

.PARAMETER Verbose
    Show compiler warnings. Suppressed by default because the engine headers emit a great many;
    the cost of that default is that warnings in this project's own sources are hidden too.

.EXAMPLE
    .\build.ps1 -Configuration 'Release' -Rebuild

.EXAMPLE
    .\build.ps1 -Configuration 'Release' -Verbose
#>
[CmdletBinding()]
param(
    [string[]]$Configuration,
    [switch]$Rebuild,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$solution = Join-Path $PSScriptRoot 'DeusExe.sln'

function Get-MSBuildPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -prerelease -products * `
            -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if ($found) { return $found }
    }
    $onPath = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw "MSBuild was not found. Install Visual Studio (or the Visual Studio Build Tools) with the C++ workload."
}

function Get-SolutionConfigurations {
    # Configuration names live in the solution's SolutionConfigurationPlatforms section.
    $section = $false
    Get-Content $solution | ForEach-Object {
        if ($_ -match 'GlobalSection\(SolutionConfigurationPlatforms\)') { $section = $true; return }
        if ($section -and $_ -match 'EndGlobalSection') { $section = $false; return }
        if ($section -and $_ -match '^\s*(.+?)\|Win32\s*=') { $matches[1] }
    }
}

function Invoke-NativeBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments
    )

    # A native command's stderr arrives as an error record, and under the $ErrorActionPreference of
    # 'Stop' above that record is terminating as soon as the caller redirects streams - which is what
    # any log capture does, deploy_renderers.ps1 among them. A stray line on stderr would then abort
    # the whole run instead of letting the loop below record the configuration that actually failed.
    # The exit code is the contract, so stderr is passed through as output. Local to this function,
    # so the rest of the script keeps 'Stop'.
    $ErrorActionPreference = 'Continue'
    & $Path @Arguments 2>&1 | ForEach-Object { "$_" }
}

$msbuild = Get-MSBuildPath
Write-Host "MSBuild: $msbuild"

$configurations = if ($Configuration) { $Configuration } else { Get-SolutionConfigurations }
if (-not $configurations) { throw "No configurations found in $solution" }

$target = if ($Clean) { 'Clean' } elseif ($Rebuild) { 'Rebuild' } else { 'Build' }
$failed = @()

# Warnings are hidden by default because the engine headers predate most modern diagnostics and
# emit a page of them per translation unit. -Verbose drops that filter, which is the only way to
# see diagnostics from the renderer's own sources.
$loggerParams = if ($VerbosePreference -ne 'SilentlyContinue') { 'Summary' } else { 'ErrorsOnly;Summary' }

foreach ($config in $configurations) {
    Write-Host ""
    Write-Host "=== $target : $config|Win32 ===" -ForegroundColor Cyan
    Invoke-NativeBuild -Path $msbuild -Arguments @(
        $solution
        '/nologo'
        '/m'
        "/t:$target"
        "/p:Configuration=$config"
        '/p:Platform=Win32'
        '/v:minimal'
        "/clp:$loggerParams"
    )
    if ($LASTEXITCODE -ne 0) { $failed += $config }
}

Write-Host ""
if ($failed) {
    Write-Host "FAILED: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}

Write-Host "All configurations succeeded ($target)." -ForegroundColor Green
if (-not $Clean) {
    Write-Host "Output: $(Join-Path $PSScriptRoot '_work\bin')"
    Write-Host "Packages: $(Join-Path $PSScriptRoot 'packages')"
}
