$ErrorActionPreference = "Stop"
Invoke-RestMethod "https://raw.githubusercontent.com/jasonjbg22/Endless-Sky-Coop/main/packaging/coop/install-or-update-coop.ps1" | Invoke-Expression
