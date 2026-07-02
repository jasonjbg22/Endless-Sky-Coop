param(
	[string] $BuildDir = "build\vanilla-msvc\Release",
	[string] $InstallDir = "",
	[string] $OutputDir = "dist",
	[string] $Version = "",
	[string] $GitHubOwner = "CHANGE_ME",
	[string] $GitHubRepo = "CHANGE_ME",
	[string] $AssetName = "EndlessSky-Coop-Windows.zip"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $repoRoot

if (!$Version) {
	$date = Get-Date -Format "yyyy.MM.dd-HHmm"
	$commit = ""
	try {
		$commit = (git rev-parse --short HEAD 2>$null).Trim()
	} catch {
		$commit = "local"
	}
	$Version = "$date-$commit"
}

$sourceRuntime = if ($InstallDir) { Resolve-Path $InstallDir } else { Resolve-Path $BuildDir }
$dist = Join-Path $repoRoot $OutputDir
$stage = Join-Path $dist "Endless Sky Co-op"
$zipPath = Join-Path $dist $AssetName

Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage, $dist | Out-Null

function Copy-RequiredPath($relativePath) {
	$source = Join-Path $repoRoot $relativePath
	if (Test-Path $source) {
		Copy-Item -Recurse -Force $source (Join-Path $stage $relativePath)
	}
}

if (!(Test-Path (Join-Path $sourceRuntime "Endless Sky.exe"))) {
	throw "Endless Sky.exe was not found in $sourceRuntime. Build Release first or pass -InstallDir."
}

Copy-Item -Force (Join-Path $sourceRuntime "Endless Sky.exe") $stage
Copy-Item -Force (Join-Path $sourceRuntime "*.dll") $stage

foreach ($dir in @("data", "images", "sounds", "shaders", "resources", "icons")) {
	Copy-RequiredPath $dir
}

foreach ($file in @("keys.txt", "credits.txt", "copyright", "license.txt", "changelog", "README.md")) {
	Copy-RequiredPath $file
}

$friendFiles = Join-Path $repoRoot "packaging\coop\friend-package"
Copy-Item -Force (Join-Path $friendFiles "*") $stage

@"
owner=$GitHubOwner
repo=$GitHubRepo
asset=$AssetName
"@ | Set-Content -Encoding ASCII (Join-Path $stage "update-config.txt")

@"
Endless Sky Co-op
Version: $Version
Built: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Repo: $GitHubOwner/$GitHubRepo
"@ | Set-Content -Encoding ASCII (Join-Path $stage "version.txt")

Remove-Item -Force $zipPath -ErrorAction SilentlyContinue
Compress-Archive -Path $stage -DestinationPath $zipPath -Force

Write-Host "Created $zipPath"
Write-Host "Staged folder: $stage"
