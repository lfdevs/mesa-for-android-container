# According to https://github.com/microsoft/winget-cli/issues/3037#issuecomment-2155167046
Write-Host "Install winget"
New-Item -Force -ItemType 'directory' -Name 'winget-cli' -Path 'C:/temp'
Invoke-WebRequest 'https://aka.ms/vs/16/release/vc_redist.x64.exe' -OutFile 'C:/temp/vc_redist.x64.exe' -UseBasicParsing
powershell -c C:/temp/vc_redist.x64.exe /install /quiet /norestart
Invoke-WebRequest 'https://aka.ms/getwinget' -OutFile 'C:/temp/winget-cli/winget.zip' -UseBasicParsing
Expand-Archive -LiteralPath 'C:/temp/winget-cli/winget.zip' -DestinationPath 'C:/temp/winget-cli' -Force
Move-Item -Path 'C:/temp/winget-cli/AppInstaller_x64.msix' -Destination 'C:/temp/winget-cli/AppInstaller_x64.zip'
Expand-Archive -LiteralPath 'C:/temp/winget-cli/AppInstaller_x64.zip' -DestinationPath 'C:/temp/winget-cli' -Force
New-Item -Force -ItemType 'directory' -Name 'winget-cli' -Path 'C:/'
Move-Item -Path 'C:/temp/winget-cli/winget.exe' -Destination 'C:/winget-cli/winget.exe'
Move-Item -Path 'C:/temp/winget-cli/WindowsPackageManager.dll' -Destination 'C:/winget-cli/WindowsPackageManager.dll'
Move-Item -Path 'C:/temp/winget-cli/resources.pri' -Destination 'C:/winget-cli'

$env:PATH="C:/winget-cli;$env:PATH"
winget settings export

$NEW_USER_PATH="$env:LOCALAPPDATA\Microsoft\WinGet\Links;C:\winget-cli;"
[Environment]::SetEnvironmentVariable('PATH', $NEW_USER_PATH + [Environment]::GetEnvironmentVariable('PATH', "User"), 'User')
Remove-Item -Recurse -Path "C:/temp"
Write-Host "Install winget done"
