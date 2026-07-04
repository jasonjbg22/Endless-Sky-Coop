param(
	[string] $InstallDir = "$env:USERPROFILE\Documents\Endless Sky Co-op",
	[string] $Owner = "jasonjbg22",
	[string] $Repo = "Endless-Sky-Coop",
	[string] $AssetName = "EndlessSky-Coop-Windows.zip"
)

$ErrorActionPreference = "Stop"

function Get-InstalledVersion($folder) {
	$versionFile = Join-Path $folder "version.txt"
	if (!(Test-Path $versionFile)) {
		return ""
	}

	foreach ($line in Get-Content $versionFile) {
		if ($line -match "^Version:\s*(.+)$") {
			return $Matches[1].Trim()
		}
	}
	return ""
}

$installRoot = [IO.Path]::GetFullPath($InstallDir)
$localVersion = Get-InstalledVersion $installRoot

Write-Host "Checking latest Endless Sky Co-op release from $Owner/$Repo..."
$release = Invoke-RestMethod `
	-Headers @{ "User-Agent" = "EndlessSkyCoopInstaller" } `
	-Uri "https://api.github.com/repos/$Owner/$Repo/releases/latest"

if ($localVersion -and $release.tag_name -and $localVersion -eq $release.tag_name) {
	Write-Host "Already up to date: $localVersion"
	Write-Host "Game folder: $installRoot"
	exit 0
}

$asset = $release.assets | Where-Object { $_.name -eq $AssetName } | Select-Object -First 1
if (!$asset) {
	$asset = $release.assets | Where-Object { $_.name -like "EndlessSky-Coop-Windows*.zip" } | Select-Object -First 1
}
if (!$asset) {
	throw "Could not find $AssetName on the latest GitHub release."
}

if ($localVersion) {
	Write-Host "Installed version: $localVersion"
	Write-Host "Latest version:    $($release.tag_name)"
} else {
	Write-Host "Endless Sky Co-op is not installed at: $installRoot"
	Write-Host "Latest version: $($release.tag_name)"
}

$temp = Join-Path ([IO.Path]::GetTempPath()) ("EndlessSkyCoopInstall-" + [guid]::NewGuid())
$zip = Join-Path $temp $asset.name
$extract = Join-Path $temp "extract"
New-Item -ItemType Directory -Force -Path $temp, $extract, $installRoot | Out-Null

try {
	Write-Host "Downloading $($asset.name)..."
	Invoke-WebRequest `
		-Headers @{ "User-Agent" = "EndlessSkyCoopInstaller" } `
		-Uri $asset.browser_download_url `
		-OutFile $zip

	Write-Host "Extracting..."
	Expand-Archive -Force -Path $zip -DestinationPath $extract

	$source = $extract
	$children = Get-ChildItem -Force $extract
	if ($children.Count -eq 1 -and $children[0].PSIsContainer) {
		$source = $children[0].FullName
	}

	Write-Host "Installing to $installRoot..."
	robocopy $source $installRoot /E /NFL /NDL /NJH /NJS /NP /XF "update-config.txt" | Out-Null
	if ($LASTEXITCODE -gt 7) {
		throw "File copy failed with robocopy exit code $LASTEXITCODE."
	}

	@"
owner=$Owner
repo=$Repo
asset=$AssetName
"@ | Set-Content -Encoding ASCII (Join-Path $installRoot "update-config.txt")

	$installedVersion = Get-InstalledVersion $installRoot
	Write-Host "Installed: $installedVersion"
	Write-Host "Launch: $installRoot\Endless Sky.exe"
}
finally {
	Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
