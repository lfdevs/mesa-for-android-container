# Mesa for Android Container
Forked From [Mesa - The 3D Graphics Library](https://gitlab.freedesktop.org/mesa/mesa)  

**English** | [简体中文](docs/zh-HanS/README.md) | [繁體中文](docs/zh-HanT/README.md)  

---  
A [Mesa](https://gitlab.freedesktop.org/mesa/mesa) build for containers on Android (Proot, Chroot, LXC, etc.), to support hardware acceleration with Adreno GPU.  

## Features
  - The Freedreno driver in Mesa 26.0.0 and later versions supports Adreno 840 GPU.  
  - Mesa drivers compiled in arm64 chroot containers across multiple popular Linux distributions offer better compatibility and can be used in Proot, chroot, and LXC containers hosted on Android.  
  - For some Adreno 6xx/7xx GPUs, the Freedreno driver can be used for OpenGL, OpenGL ES, and Vulkan, eliminating the need for Zink for graphics API call translation and significantly improving GPU utilization.  
  - Only drivers relevant to the vast majority of Android devices are compiled to reduce the package size.  
## Compatibility

|          GPU           |    OpenGL    |  OpenGL ES   | Vulkan (Zink) |     Vulkan      |
| :--------------------: | :----------: | :----------: | :-----------: | :-------------: |
|     **Adreno 660**     | ✔️ Supported | ✔️ Supported | ✔️ Supported  |  ✔️ Supported   |
| **Adreno 730/740/750** | ✔️ Supported | ✔️ Supported | ✔️ Supported  |  ✔️ Supported   |
|     **Adreno 840**     | ✔️ Supported | ✔️ Supported |  ❔ Untested   | ❌ Not supported |
## Installation
This project's Releases provide two types of installation packages: one installable via the Linux distribution's package manager, and another that must be installed by direct extraction. The first type is recommended; however, if you need the latest Mesa features (such as Adreno 840 support), use the second type.  
If the Turnip driver from the standard release (whose release title doesn't have the `turnip-` prefix) fails to work properly, you can use the **unpatched Turnip driver** (whose release title does have the `turnip-` prefix) by directly installing it over the standard one.  
### Using the Package Manager
Depending on your Linux distribution, go to [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) and download all corresponding packages for a specific release, then follow the installation instructions provided in the release notes. Below are the latest releases for some popular Linux distributions:  

| Linux Distribution |                                                            Latest Release                                                            |                                                           Unpatched Turnip driver                                                           |
| :----------------: | :----------------------------------------------------------------------------------------------------------------------------------: | :-----------------------------------------------------------------------------------------------------------------------------------------: |
|       Debian       |            [25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-adreno)            |            [turnip-25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-turnip)            |
|       Ubuntu       | [25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-adreno) | [turnip-25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-turnip) |
|     Arch Linux     |              [26.0.0-1.1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-26.0.0-devel-20251209)              |              [turnip-26.0.0-2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-26.0.0-devel-20251215)              |
### Direct Extraction  
ℹ️ **Note**: The `.tar.gz` installation packages in Releases can only overwrite the existing Mesa drivers. To uninstall, you must manually delete the extracted files. These packages are intended for testing purposes only.  

1. Go to [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) and download a `.tar.gz` installation package. Please note the Linux distribution suffix in the filename, such as `debian_trixie_arm64`. You can only install the package that matches your distribution.  
2.  Extract the installation package directly to the root directory.  
```shell
sudo tar -zxvf mesa-for-android-container_26.0.0-devel-20251209_debian_trixie_arm64.tar.gz -C /
```
3.  Refresh the dynamic linker cache.  
```shell
sudo ldconfig
```

Uninstallation can be performed by referring to the following commands:  
```shell
# Copy the file list output by this command
tar tf mesa-for-android-container_26.0.0-devel-20251209_debian_trixie_arm64.tar.gz | grep -v '/$' | tr '\n' ' ' ; echo
cd /
# Replace <file-list> with the actual file list
sudo rm <file-list>
# Reinstall the distribution-maintained Mesa drivers
# Debian or Ubuntu:
sudo apt update
sudo apt install --reinstall libegl-mesa0 libgbm1 libgl1-mesa-dri libglx-mesa0 mesa-libgallium mesa-vulkan-drivers
# Fedora:
sudo dnf reinstall mesa-filesystem mesa-libglapi mesa-libgbm mesa-libEGL mesa-libGL mesa-vulkan-drivers mesa-dri-drivers mesa-libOpenCL
# Arch Linux:
sudo pacman -S mesa mesa-docs opencl-mesa vulkan-freedreno vulkan-mesa-implicit-layers vulkan-mesa-layers
```
## Usage
Specify the environment variables `MESA_LOADER_DRIVER_OVERRIDE` and `TU_DEBUG` when running a specific program, as follows:  
```shell
MESA_LOADER_DRIVER_OVERRIDE=kgsl TU_DEBUG=noconform glmark2
```
Alternatively, add them to the `/etc/environment` file so they are loaded automatically when the container starts:  
```plaintext
MESA_LOADER_DRIVER_OVERRIDE=kgsl
TU_DEBUG=noconform
```
## Building
For detailed building procedures, please refer to the official Mesa documentation ([Compilation and Installation Using Meson — The Mesa 3D Graphics Library latest documentation](https://docs.mesa3d.org/meson.html)). Below are the key steps for building Mesa in a Debian 13 arm64 environment:  

1.  Check if the source code repositories are enabled. If you are using the traditional format for software sources (`/etc/apt/sources.list`), check for a configuration similar to the following.  
```plaintext
deb-src https://deb.debian.org/debian trixie main contrib non-free non-free-firmware
```
2.  Install the dependencies required to build Mesa.  
```shell
sudo apt build-dep mesa
```
3.  Clone this project's repository.  
```shell
git clone https://github.com/lfdevs/mesa-for-android-container.git
```
4.  Switch to the branch where the relevant patches have been applied, such as `adreno-main`.  
```shell
cd mesa-for-android-container
git checkout adreno-main
```
5.  Initialize the build directory. You can refer to the [meson.options](https://github.com/lfdevs/mesa-for-android-container/blob/main/meson.options) file to see all available build options.  
```shell
meson setup build/ \
    --prefix=/usr \
    -Dplatforms=x11,wayland \
    -Dgallium-drivers=freedreno,zink,virgl,llvmpipe \
    -Dvulkan-drivers=freedreno \
    -Degl=enabled \
    -Dgles2=enabled \
    -Dglvnd=enabled \
    -Dglx=dri \
    -Dlibunwind=disabled \
    -Dmicrosoft-clc=disabled \
    -Dvalgrind=disabled \
    -Dgles1=disabled \
    -Dfreedreno-kmds=kgsl \
    -Dbuildtype=release
```
6.  Start the build.  
```shell
ninja -C build/
```
7.  To install directly on the build device, run the following command:  
```shell
ninja -C build/ install
```
8.  If you need to package the built Mesa drivers for installation on other devices with the same distribution, refer to the following commands:  
```shell
export MESA_RELEASE_NAME_SUFFIX=26.0.0-devel-20251209_debian_trixie_arm64
sudo mkdir /tmp/mesa-install-tmp
sudo DESTDIR=/tmp/mesa-install-tmp meson install -C build/
sudo tar -zcvf mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz -C /tmp/mesa-install-tmp .
sudo chown $USER:$USER mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz
chmod 644 mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz
sudo rm -rf /tmp/mesa-install-tmp
```
## Benchmarks
Detailed test results: [benchmark-result](docs/common/benchmark-result.md)  

|      Device       |   Model    |                SoC                |    GPU     | glmark2 | glmark2-es2 |      vkmark |
| :---------------: | :--------: | :-------------------------------: | :--------: | ------: | ----------: | ----------: |
|   Redmi K40 Pro   | M2012K11G  |      Qualcomm Snapdragon 888      | Adreno 660 |     842 |         771 |        1170 |
| Xiaomi Pad 6 Pro  | 23046RP50C |   Qualcomm Snapdragon 8+ Gen 1    | Adreno 730 |    1169 |        1143 |         263 |
| Xiaomi 17 Pro Max | 2509FPN0BC | Qualcomm Snapdragon 8 Elite Gen 5 | Adreno 840 |    2947 |        2901 | Not working |
## Acknowledgements
  - [Lucas Fryzek](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21570): Author of the KGSL backend code for the Mesa Freedreno driver.  
  - [xMeM](https://github.com/xMeM/termux-packages/commit/401982b8d9eaef70669762bfff2a963341c65e52): For porting the Freedreno driver's KGSL backend to Termux:X11.  
  - [Robert Kirkman](https://github.com/robertkirkman/termux-packages/commit/06a959eeddf153cebd0d3ea1a6c2eb2921b5f786): For integrating and improving xMeM's patches.  
  - [Rob Clark](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/38450): For adding initial Freedreno support for Adreno Gen8 architecture (including Adreno 840).  