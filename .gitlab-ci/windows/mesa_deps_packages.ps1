# Download new TLS certs from Windows Update
Write-Host "Updating TLS certificate store at:"
Get-Date
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue "_tlscerts" | Out-Null
$certdir = (New-Item -ItemType Directory -Name "_tlscerts")
certutil -syncwithWU "$certdir"
Foreach ($file in (Get-ChildItem -Path "$certdir\*" -Include "*.crt")) {
  Import-Certificate -FilePath $file -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
}
Remove-Item -Recurse -Path $certdir

Write-Host "Installing graphics tools (DirectX debug layer) at:"
Get-Date
Set-Service -Name wuauserv -StartupType Manual
if (!$?) {
  Write-Host "Failed to enable Windows Update"
  Exit 1
}

For ($i = 0; $i -lt 5; $i++) {
  Dism /online /quiet /add-capability /capabilityname:Tools.Graphics.DirectX~~~~0.0.1.0
  $graphics_tools_installed = $?
  if ($graphics_tools_installed) {
    Break
  }
}

if (!$graphics_tools_installed) {
  Write-Host "Failed to install graphics tools"
  Get-Content C:\Windows\Logs\DISM\dism.log
  Exit 1
}

$USER_PATH=[System.Environment]::GetEnvironmentVariable('PATH', [System.EnvironmentVariableTarget]::User)
$MACHINE_PATH=[System.Environment]::GetEnvironmentVariable('PATH', [System.EnvironmentVariableTarget]::Machine)
Write-Output "Before winget install USER_PATH:$USER_PATH MACHINE_PATH:$MACHINE_PATH"

$Packages = @(
  'Microsoft.WindowsWDK.10.0.26100,10.1.26100.6584',
  'Python.Python.3.13,3.13.9',
  'Ninja-build.Ninja,1.13.1',
  'Kitware.CMake,4.1.3',
  'Git.Git,2.52.0',
  'WinFlexBison.win_flex_bison,2.5.24',
  'bloodrock.pkg-config-lite,0.28-1'
)

$ProgressPreference = "SilentlyContinue"
New-Item -Force -ItemType 'directory' -Name 'flexbison' -Path 'C:\temp'
foreach ($package in $Packages)
{
  $package_id, $package_version = $package -split ',', 2
  Write-Output "Installing $package_id with version $package_version by winget"
  For ($i = 0; $i -lt 5; $i++) {
    winget install --verbose --silent --accept-package-agreements --source winget --exact --id $package_id --version $package_version --log C:\temp\wdk-install.log
    $packages_installed = $?
    if ($packages_installed) {
      Break
    }
  }
  if (!$packages_installed) {
    Write-Host "Couldn't install $package with winget"
    Exit 1
  } else {
    Write-Output "Installed $package with winget"
  }
}

# The win_flex.exe should be directly accessed than use symbolic link https://github.com/lexxmark/winflexbison/issues/97
$win_flex_target=((Get-ChildItem -Path (Get-Command win_flex).Path -Force | Select-Object Target).Target | Split-Path -Parent)
$NEW_USER_PATH="$win_flex_target;"
[Environment]::SetEnvironmentVariable('PATH', $NEW_USER_PATH + [Environment]::GetEnvironmentVariable('PATH', "User"), 'User')

$USER_PATH=[System.Environment]::GetEnvironmentVariable('PATH', [System.EnvironmentVariableTarget]::User)
$MACHINE_PATH=[System.Environment]::GetEnvironmentVariable('PATH', [System.EnvironmentVariableTarget]::Machine)
Write-Output "After winget install USER_PATH:$USER_PATH MACHINE_PATH:$MACHINE_PATH"

# Setup tmp path for git and python
$env:PATH="$env:LOCALAPPDATA\Programs\Python\Python313\Scripts\;$env:LOCALAPPDATA\Programs\Python\Python313\;$env:ProgramFiles\Git\cmd;$env:PATH"

Start-Process -NoNewWindow -Wait git -ArgumentList 'config --global core.autocrlf false'

New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled" -Value 1 -PropertyType DWORD -Force

Write-Host "Upgrading pip at:"
Get-Date
python -m pip install --upgrade pip --progress-bar off
Write-Host "Installing python packages at:"
Get-Date
pip3 install packaging meson mako "numpy < 2.0" pyyaml --progress-bar off
if (!$?) {
  Write-Host "Failed to install dependencies from pip"
  Exit 1
}
Write-Host "Installing python packages finished at:"
Get-Date
