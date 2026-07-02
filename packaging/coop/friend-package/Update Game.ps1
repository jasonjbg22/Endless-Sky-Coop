$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$configPath = Join-Path $root "update-config.txt"

if (!(Test-Path $configPath)) {
	@"
# GitHub repository that publishes the friend build.
# Change this to your repo before sharing with friends, for example:
# owner=JasonYourGitHubName
# repo=Endless-Sky-Coop
owner=CHANGE_ME
repo=CHANGE_ME
asset=EndlessSky-Coop-Windows.zip
"@ | Set-Content -Encoding ASCII $configPath
	Write-Host "Created update-config.txt. Ask the host for the GitHub owner/repo, then run this again."
	exit 1
}

$settings = @{}
Get-Content $configPath | ForEach-Object {
	$line = $_.Trim()
	if (!$line -or $line.StartsWith("#") -or !$line.Contains("=")) {
		return
	}
	$key, $value = $line.Split("=", 2)
	$settings[$key.Trim()] = $value.Trim()
}

$owner = $settings["owner"]
$repo = $settings["repo"]
$assetName = $settings["asset"]

if (!$owner -or !$repo -or $owner -eq "CHANGE_ME" -or $repo -eq "CHANGE_ME") {
	Write-Host "update-config.txt still needs the GitHub owner and repo."
	exit 1
}
if (!$assetName) {
	$assetName = "EndlessSky-Coop-Windows.zip"
}

Write-Host "Checking latest release from $owner/$repo..."
$releaseUrl = "https://api.github.com/repos/$owner/$repo/releases/latest"
$release = Invoke-RestMethod -Headers @{ "User-Agent" = "EndlessSkyCoopUpdater" } -Uri $releaseUrl
$asset = $release.assets | Where-Object { $_.name -eq $assetName } | Select-Object -First 1
if (!$asset) {
	$asset = $release.assets | Where-Object { $_.name -like "EndlessSky-Coop-Windows*.zip" } | Select-Object -First 1
}
if (!$asset) {
	throw "Could not find a Windows co-op zip on the latest GitHub release."
}

$temp = Join-Path ([IO.Path]::GetTempPath()) ("EndlessSkyCoopUpdate-" + [guid]::NewGuid())
$zip = Join-Path $temp $asset.name
$extract = Join-Path $temp "extract"
New-Item -ItemType Directory -Force -Path $temp, $extract | Out-Null

try {
	Write-Host "Downloading $($asset.name)..."
	Invoke-WebRequest -Headers @{ "User-Agent" = "EndlessSkyCoopUpdater" } -Uri $asset.browser_download_url -OutFile $zip
	Write-Host "Extracting update..."
	Expand-Archive -Force -Path $zip -DestinationPath $extract

	$source = $extract
	$children = Get-ChildItem -Force $extract
	if ($children.Count -eq 1 -and $children[0].PSIsContainer) {
		$source = $children[0].FullName
	}

	Write-Host "Installing update..."
	robocopy $source $root /E /NFL /NDL /NJH /NJS /NP /XF "update-config.txt" | Out-Null
	if ($LASTEXITCODE -gt 7) {
		throw "File copy failed with robocopy exit code $LASTEXITCODE."
	}

	$versionFile = Join-Path $root "version.txt"
	if (Test-Path $versionFile) {
		Write-Host (Get-Content $versionFile -Raw)
	}
}
finally {
	Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
