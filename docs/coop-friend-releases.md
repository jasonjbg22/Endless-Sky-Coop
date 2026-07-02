# Co-op Friend Releases

This build is meant for friends who want to play, not build from source.

## Local Package

Build the game in Release, then create the portable friend zip:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\coop\package-coop-release.ps1 `
  -GitHubOwner YOUR_GITHUB_NAME `
  -GitHubRepo YOUR_REPO_NAME
```

The output is:

```text
dist/
  EndlessSky-Coop-Windows.zip
  Endless Sky Co-op/
```

Share the zip or attach it to a GitHub Release.

## Friend Update Flow

Friends run:

```text
Update Game.bat
```

The updater reads `update-config.txt`, downloads the newest GitHub Release zip, and copies the newer files over the current folder.

Friends do not need Git, GitHub accounts, Visual Studio, CMake, or vcpkg.

## GitHub Setup

Use your own repo, not the upstream Endless Sky repo.

Recommended repo settings:

- Public repo if you want friends to download without logging in.
- Private repo only if every friend has GitHub access.
- Releases enabled.

Each time you want friends to test a fix:

1. Commit and push the fix.
2. Create a release.
3. Attach `EndlessSky-Coop-Windows.zip`.
4. Tell friends to run `Update Game.bat`.
