# ============================================================================
# OCTAVOX R17-P2: pinned VST3 SDK acquisition + verification (Windows/PowerShell)
# ============================================================================
# PowerShell counterpart of get_vst3_sdk.sh. Fetches EXACTLY the VST3 SDK
# components recorded in native/vst3/VST3_WRAPPER.md (tag v3.7.12_build_20
# + the four component gitlink SHAs) and verifies every fetched revision
# against the pin.
#
# The SDK is written OUTSIDE the evidence tree (default: a sibling directory
# of Octavox_RE_evidence\) because the Steinberg VST3 dual license does not
# allow redistribution with this package.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File get_vst3_sdk.ps1 [-TargetDir <path>] [-VerifyOnly]
#
# Fails clearly (nonzero exit) when: git is missing, the network is
# unreachable, a tag/SHA does not resolve, or a fetched revision does not
# match its pin. NEVER silently switches to a newer SDK revision.
#
# STATUS: authored for the round-17 Windows build path; the equivalent POSIX
# script was executed end-to-end in the development environment (fetch,
# verify and fresh-install modes). This PowerShell variant follows the same
# git commands but was NOT executed here (no Windows runtime available).
# ============================================================================
[CmdletBinding()]
param(
    [string]$TargetDir = "",
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'

$Tag       = 'v3.7.12_build_20'
$MainSha   = 'cc2adc90382dded9e347caf74e4532f1458715db'
$MainUrl   = 'https://github.com/steinbergmedia/vst3sdk.git'

# component name | repository | pinned gitlink SHA at the tag
$Components = @(
    @{ Name = 'base';           Url = 'https://github.com/steinbergmedia/vst3_base.git';           Sha = 'f0998e7b8424b32ba275cf4218aa56beef29821c' },
    @{ Name = 'cmake';          Url = 'https://github.com/steinbergmedia/vst3_cmake.git';          Sha = '2022b265cdbcd90b37294d3fb397f9766dbe4c06' },
    @{ Name = 'pluginterfaces'; Url = 'https://github.com/steinbergmedia/vst3_pluginterfaces.git'; Sha = '151ecde4d6ee1c457dcce848342b162461944fe6' },
    @{ Name = 'public.sdk';     Url = 'https://github.com/steinbergmedia/vst3_public_sdk.git';     Sha = '3fce096d6ee575479753f1aab23033d2e2ffdc6e' }
)

function Fail([string]$Message) {
    Write-Host "get_vst3_sdk.ps1: FAILED" -ForegroundColor Red
    Write-Host "  $Message" -ForegroundColor Red
    Write-Host "  Required: network access to github.com, git >= 2.x, and the exact pinned"
    Write-Host "  revisions recorded in native/vst3/VST3_WRAPPER.md. No newer SDK revision"
    Write-Host "  is ever used silently."
    exit 1
}

# ---- resolve the target directory -------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$EvidenceRoot = (Resolve-Path (Join-Path $ScriptDir '..\..')).Path
if (-not $TargetDir) { $TargetDir = Join-Path (Split-Path -Parent $EvidenceRoot) 'vst3sdk' }
$TargetAbs = [System.IO.Path]::GetFullPath($TargetDir)

# ---- tool checks -------------------------------------------------------------
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Fail 'git is not installed or not on PATH.'
}
# Windows 10+ ships tar.exe; needed to unpack `git archive` output.
if (-not (Get-Command tar -ErrorAction SilentlyContinue)) {
    Fail 'tar.exe not found on PATH (included with Windows 10 1803+ / recent git installs).'
}

# ---- refuse to write inside the evidence archive -----------------------------
if ($TargetAbs.StartsWith($EvidenceRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    Fail "refusing to place the SDK inside the evidence tree ($EvidenceRoot). The VST3 SDK is not redistributable with the evidence package; pass -TargetDir outside it (default: a sibling of Octavox_RE_evidence)."
}

Write-Host 'OCTAVOX pinned VST3 SDK acquisition (Windows)'
Write-Host "  tag:        $Tag"
Write-Host "  main SHA:   $MainSha"
Write-Host "  target dir: $TargetAbs"
Write-Host ''

# ---- helper: fetch a repo at an exact SHA into a work directory ---------------
function Fetch-AtSha([string]$Url, [string]$Sha, [string]$Dir) {
    if (Test-Path $Dir) { Remove-Item -Recurse -Force $Dir }
    New-Item -ItemType Directory -Path $Dir | Out-Null
    git -C $Dir init --quiet 2>$null
    if ($LASTEXITCODE -ne 0) { Fail "git init failed in $Dir" }
    git -C $Dir remote add origin $Url
    if ($LASTEXITCODE -ne 0) { Fail "git remote add failed for $Url" }
    # GitHub enables uploadpack.allowReachableSHA1InWant: a depth-1 fetch of
    # an exact SHA fetches ONLY that revision's objects.
    git -C $Dir fetch --quiet --depth 1 origin $Sha
    if ($LASTEXITCODE -ne 0) { Fail "git fetch failed for $Url @ $Sha (network reachable? revision public?)" }
    git -C $Dir checkout --quiet --detach FETCH_HEAD
    if ($LASTEXITCODE -ne 0) { Fail "git checkout failed for $Url @ $Sha" }
    $Got = (git -C $Dir rev-parse HEAD).Trim()
    if ($Got -ne $Sha) { Fail "fetched revision mismatch for ${Url}: got $Got, want $Sha" }
}

$Work = Join-Path ([System.IO.Path]::GetTempPath()) ("octavox_vst3sdk_" + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $Work | Out-Null

# ---- [1/4] fetch the main repo at the tag ------------------------------------
Write-Host "[1/4] fetching main repo $MainUrl at tag $Tag ..."
$MainWork = Join-Path $Work 'main'
New-Item -ItemType Directory -Path $MainWork | Out-Null
git -C $MainWork init --quiet 2>$null
git -C $MainWork remote add origin $MainUrl
git -C $MainWork fetch --quiet --depth 1 origin "refs/tags/$Tag"
if ($LASTEXITCODE -ne 0) { Fail "git fetch failed for the tag $Tag (network reachable?)" }
git -C $MainWork checkout --quiet --detach FETCH_HEAD
$MainGot = (git -C $MainWork rev-parse HEAD).Trim()
if ($MainGot -ne $MainSha) {
    Fail "tag $Tag resolved to $MainGot but the recorded pin is $MainSha (the tag moved or the wrong tag was fetched)."
}
Write-Host "      main OK: $MainGot"

# ---- [2/4] cross-check the tag's gitlinks against the recorded pins -----------
Write-Host '[2/4] cross-checking the tag''s gitlinks against the recorded pins ...'
foreach ($Comp in $Components) {
    $Line = git -C $MainWork ls-tree HEAD -- $Comp.Name
    $Got = ($Line -split '\s+')[2]
    if ($Got -ne $Comp.Sha) {
        Fail ("the tag's gitlink for '{0}' is {1} but the recorded pin is {2} -- refusing." -f $Comp.Name, $Got, $Comp.Sha)
    }
}
Write-Host '      all four component pins match the tag''s gitlinks.'

# ---- [3/4] fetch the components at the pinned SHAs ----------------------------
Write-Host '[3/4] fetching the four components at their pinned SHAs ...'
foreach ($Comp in $Components) {
    Write-Host ("      {0} @ {1}..." -f $Comp.Name, $Comp.Sha.Substring(0, 12))
    Fetch-AtSha -Url $Comp.Url -Sha $Comp.Sha -Dir (Join-Path $Work $Comp.Name)
}

# ---- [4/4] install / verify ----------------------------------------------------
Write-Host "[4/4] installing/verifying the tree at $TargetAbs ..."
if ($VerifyOnly) {
    if (-not (Test-Path $TargetAbs)) { Fail "--VerifyOnly requested but $TargetAbs does not exist." }
    $AnyFail = $false
    if (Test-Path (Join-Path $TargetAbs '.git')) {
        $Got = (git -C $TargetAbs rev-parse HEAD 2>$null)
        if ("$Got".Trim() -eq $MainSha) { Write-Host "      main: $MainSha OK" }
        else { Write-Host "      main: MISMATCH ($Got != $MainSha)"; $AnyFail = $true }
    } else {
        Write-Host '      main: no git metadata (archive install?) -- content comparison only'
    }
    foreach ($Comp in $Components) {
        $FreshDir = Join-Path $Work $Comp.Name
        $HaveDir = Join-Path $TargetAbs $Comp.Name
        # content comparison via per-file SHA256 of both trees (ignoring .git)
        $Same = $true
        $FreshFiles = Get-ChildItem -Recurse -File $FreshDir | Where-Object { $_.FullName -notmatch '\\\.git\\' }
        $HaveFiles = Get-ChildItem -Recurse -File $HaveDir -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notmatch '\\\.git\\' }
        if ($FreshFiles.Count -ne $HaveFiles.Count) { $Same = $false }
        if ($Same) {
            $FreshMap = @{}
            foreach ($f in $FreshFiles) { $FreshMap[$f.FullName.Substring($FreshDir.Length)] = (Get-FileHash -Algorithm SHA256 $f.FullName).Hash }
            foreach ($f in $HaveFiles) {
                $Rel = $f.FullName.Substring($HaveDir.Length)
                if (-not $FreshMap.ContainsKey($Rel) -or $FreshMap[$Rel] -ne (Get-FileHash -Algorithm SHA256 $f.FullName).Hash) { $Same = $false; break }
            }
        }
        if ($Same) { Write-Host ("      {0}: content matches the pinned revision OK" -f $Comp.Name) }
        else { Write-Host ("      {0}: content DIFFERS from the pinned revision" -f $Comp.Name); $AnyFail = $true }
    }
    Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue
    if ($AnyFail) { Fail 'the existing tree does not match the pinned revision set.' }
    Write-Host "VERIFIED: $TargetAbs matches the pinned SDK revision set."
    exit 0
}

# normal install mode
if ((Test-Path $TargetAbs) -and -not (Test-Path (Join-Path $TargetAbs '.git'))) {
    Fail "$TargetAbs exists but is not the main SDK git checkout. Move it away or pass -TargetDir with a new path."
}
if (Test-Path (Join-Path $TargetAbs '.git')) {
    $Got = (git -C $TargetAbs rev-parse HEAD).Trim()
    if ($Got -ne $MainSha) {
        Fail "existing checkout at $TargetAbs is $Got, not the pinned $MainSha. Refusing to mix revisions; remove the directory and re-run."
    }
    Write-Host '      main checkout already at the pinned revision.'
} else {
    New-Item -ItemType Directory -Path $TargetAbs | Out-Null
    # copy the fetched main worktree INCLUDING .git (future -VerifyOnly stays exact)
    Copy-Item -Recurse -Force (Join-Path $MainWork '*') $TargetAbs
    Write-Host '      main installed.'
}
foreach ($Comp in $Components) {
    $Dest = Join-Path $TargetAbs $Comp.Name
    New-Item -ItemType Directory -Path $Dest | Out-Null
    # git archive -> tar: the component lands as plain files (no .git)
    $Tar = Join-Path $Work ($Comp.Name + '.tar')
    git -C (Join-Path $Work $Comp.Name) archive --format=tar -o $Tar HEAD
    if ($LASTEXITCODE -ne 0) { Fail "git archive failed for $($Comp.Name)" }
    tar -xf $Tar -C $Dest
    if ($LASTEXITCODE -ne 0) { Fail "tar -xf failed for $($Comp.Name)" }
    Write-Host ("      {0} installed @ {1}" -f $Comp.Name, $Comp.Sha)
}

Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue
Write-Host ''
Write-Host "DONE: VST3 SDK $Tag verified at the pinned revisions:"
Write-Host ("  {0,-14} {1}" -f 'main', $MainSha)
foreach ($Comp in $Components) {
    Write-Host ("  {0,-14} {1}" -f $Comp.Name, $Comp.Sha)
}
Write-Host ''
Write-Host 'Next: build the wrapper --'
Write-Host '  cd native\vst3'
Write-Host '  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release [-DVST3_SDK_ROOT=<sdk>]'
Write-Host '  cmake --build build --config Release'
Write-Host 'or use native\tools\build_and_test.ps1 for the full chain.'
exit 0
