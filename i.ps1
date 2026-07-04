$ErrorActionPreference = "Stop"
Invoke-RestMethod "https://raw.githubusercontent.com/jasonjbg22/Endless-Sky-Coop/c9fbb56986288f11700c85592d8b94afd21ce8dd/packaging/coop/install-or-update-coop.ps1" | Invoke-Expression
