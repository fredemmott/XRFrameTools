# 1. cs file -> exe
# 2. exe file + build artifacts -> msi
#
$PSNativeCommandUseErrorActionPreference = $true
$ErrorActionPreference = 'Stop';
dotnet build `
  "$PSScriptRoot\XRFrameTools-Installer.csproj" `
  --output "$(Get-Location)" `
  --artifacts-path "$(Get-Location)\artifacts" `
  --framework net8.0-windows `
  --verbosity quiet
.\XRFrameTools-Installer.exe @args
