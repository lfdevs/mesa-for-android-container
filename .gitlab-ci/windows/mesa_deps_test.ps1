Get-Date
Write-Host "Cloning Waffle"

$MyPath = $MyInvocation.MyCommand.Path | Split-Path -Parent
. "$MyPath\mesa_vs_init.ps1"

git clone --no-progress --single-branch --no-checkout https://gitlab.freedesktop.org/mesa/waffle.git 'C:\src\waffle'
if (!$?) {
  Write-Host "Failed to clone Waffle repository"
  Exit 1
}

Push-Location -Path C:\src\waffle
git checkout 950a1f35a718bc2a8e1dda75845e52651bb331a7
Pop-Location

Get-Date
$waffle_build = New-Item -ItemType Directory -Path "C:\src\waffle" -Name "build"
Push-Location -Path $waffle_build.FullName
Write-Host "Compiling Waffle"
meson setup `
--buildtype=release `
--default-library=static `
--prefix="C:\Waffle" && `
ninja -j32 install
$buildstatus = $?
Pop-Location
Remove-Item -Recurse -Path $waffle_build
if (!$buildstatus) {
  Write-Host "Failed to compile or install Waffle"
  Exit 1
}

Get-Date
Write-Host "Downloading glext.h"
New-Item -ItemType Directory -Path ".\glext" -Name "GL"
$ProgressPreference = "SilentlyContinue"
Invoke-WebRequest -Uri 'https://www.khronos.org/registry/OpenGL/api/GL/glext.h' -OutFile '.\glext\GL\glext.h' | Out-Null

Get-Date
Write-Host "Cloning Piglit"
git clone --no-progress --single-branch --no-checkout https://gitlab.freedesktop.org/mesa/piglit.git 'C:\piglit'
if (!$?) {
  Write-Host "Failed to clone Piglit repository"
  Exit 1
}
Push-Location -Path C:\piglit
git checkout b41accc83689966f91217fc5b57dbe06202b8c8c

Get-Date
Write-Host "Compiling Piglit"
cmake -S . -B . `
-GNinja `
-DCMAKE_BUILD_TYPE=Release `
-DPIGLIT_USE_WAFFLE=ON `
-DWaffle_INCLUDE_DIRS=C:\Waffle\include\waffle-1 `
-DWaffle_LDFLAGS=C:\Waffle\lib\libwaffle-1.a `
-DGLEXT_INCLUDE_DIR=.\glext && `
ninja -j32
$buildstatus = $?
Pop-Location
if (!$buildstatus) {
  Write-Host "Failed to compile Piglit"
  Exit 1
}

Get-Date
Write-Host "Cloning spirv-samples"
git clone --no-progress --single-branch --no-checkout https://github.com/dneto0/spirv-samples.git  C:\spirv-samples\
Push-Location -Path C:\spirv-samples\
git checkout 36372636df06a24c4e2de1551beee055db01b91d
Pop-Location

Get-Date
Write-Host "Cloning Vulkan and GL Conformance Tests"
$deqp_source = "C:\src\VK-GL-CTS\"
git clone --no-progress --single-branch https://github.com/KhronosGroup/VK-GL-CTS.git -b vulkan-cts-1.3.4 $deqp_source
if (!$?) {
  Write-Host "Failed to clone deqp repository"
  Exit 1
}

Push-Location -Path $deqp_source
# --insecure is due to SSL cert failures hitting sourceforge for zlib and
# libpng (sigh).  The archives get their checksums checked anyway, and git
# always goes through ssh or https.
py .\external\fetch_sources.py --insecure
Pop-Location

Get-Date
$deqp_build = New-Item -ItemType Directory -Path "C:\deqp"
Push-Location -Path $deqp_build.FullName
Write-Host "Compiling deqp"
cmake -S $($deqp_source) `
-B . `
-GNinja `
-DCMAKE_BUILD_TYPE=Release `
-DDEQP_TARGET=default && `
ninja -j32
$buildstatus = $?
Pop-Location
if (!$buildstatus) {
  Write-Host "Failed to compile deqp"
  Exit 1
}

# Copy test result templates
Copy-Item -Path "$($deqp_source)\doc\testlog-stylesheet\testlog.css" -Destination $deqp_build
Copy-Item -Path "$($deqp_source)\doc\testlog-stylesheet\testlog.xsl" -Destination $deqp_build

# Copy Vulkan must-pass list
$deqp_mustpass = New-Item -ItemType Directory -Path $deqp_build -Name "mustpass"
$root_mustpass = Join-Path -Path $deqp_source -ChildPath "external\vulkancts\mustpass\main"
$files = Get-Content "$($root_mustpass)\vk-default.txt"
foreach($file in $files) {
  Get-Content "$($root_mustpass)\$($file)" | Add-Content -Path "$($deqp_mustpass)\vk-main.txt"
}
Remove-Item -Force -Recurse $deqp_source

Get-Date
$url = 'https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe';
Write-Host ('Downloading {0} ...' -f $url);
Invoke-WebRequest -Uri $url -OutFile 'rustup-init.exe';
Write-Host "Installing rust toolchain"
C:\rustup-init.exe -y;
Remove-Item C:\rustup-init.exe;

Get-Date
Write-Host "Installing deqp-runner"
$env:Path += ";$($env:USERPROFILE)\.cargo\bin"
cargo install --git https://gitlab.freedesktop.org/anholt/deqp-runner.git

Get-Date
Write-Host "Downloading DirectX 12 Agility SDK"
Invoke-WebRequest -Uri https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.610.2 -OutFile 'agility.zip'
Expand-Archive -Path 'agility.zip' -DestinationPath 'C:\agility'
Remove-Item 'agility.zip'

$piglit_bin = 'C:\Piglit\bin'
$vk_cts_bin = "$deqp_build\external\vulkancts\modules\vulkan"

# Copy Agility SDK into subfolder of piglit and Vulkan CTS
$agility_dest = New-Item -ItemType Directory -Path $piglit_bin -Name 'D3D12'
Copy-Item 'C:\agility\build\native\bin\x64\*.dll' -Destination $agility_dest
$agility_dest = New-Item -ItemType Directory -Path $vk_cts_bin -Name 'D3D12'
Copy-Item 'C:\agility\build\native\bin\x64\*.dll' -Destination $agility_dest
Remove-Item -Recurse 'C:\agility'

Get-Date
Write-Host "Downloading Updated WARP"
Invoke-WebRequest -Uri https://www.nuget.org/api/v2/package/Microsoft.Direct3D.WARP/1.0.7.1 -OutFile 'warp.zip'
Expand-Archive -Path 'warp.zip' -DestinationPath 'C:\warp'
Remove-Item 'warp.zip'

# Copy WARP next to piglit and Vulkan CTS
Copy-Item 'C:\warp\build\native\amd64\d3d10warp.dll' -Destination $piglit_bin
Copy-Item 'C:\warp\build\native\amd64\d3d10warp.dll' -Destination $vk_cts_bin
Remove-Item -Recurse 'C:\warp'

Get-Date
Write-Host "Downloading DirectXShaderCompiler release"
Invoke-WebRequest -Uri https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2207/dxc_2022_07_18.zip -OutFile 'DXC.zip'
Expand-Archive -Path 'DXC.zip' -DestinationPath 'C:\DXC'
# No more need to get dxil.dll from the VS install
Copy-Item 'C:\DXC\bin\x64\*.dll' -Destination 'C:\Windows\System32'

Get-Date
Write-Host "Enabling developer mode"
# Create AppModelUnlock if it doesn't exist, required for enabling Developer Mode
$RegistryKeyPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock"
if (-not(Test-Path -Path $RegistryKeyPath)) {
    New-Item -Path $RegistryKeyPath -ItemType Directory -Force
}

# Add registry value to enable Developer Mode
New-ItemProperty -Path $RegistryKeyPath -Name AllowDevelopmentWithoutDevLicense -PropertyType DWORD -Value 1 -Force

Get-Date
Write-Host "Complete"
