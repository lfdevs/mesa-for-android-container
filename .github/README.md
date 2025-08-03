# Mesa for Android Container
Forked From [Mesa - The 3D Graphics Library](https://gitlab.freedesktop.org/mesa/mesa)  

**English** | [简体中文](docs/zh-HanS/README.md) | [繁體中文](docs/zh-HanT/README.md)  

---  
A [Mesa](https://gitlab.freedesktop.org/mesa/mesa) build for containers on Android (Proot, Chroot, LXC, etc.), to support hardware acceleration with Adreno or Mali GPU.  

## Features
  - Directly compiled from the modified official Mesa repository source code in a Debian 13 Linux arm64 Chroot container, it is backward compatible with Debian 12. The Mesa drivers compiled by this project can be used in Proot, Chroot, and LXC containers hosted on Android.  
  - For some Adreno 6xx/7xx GPUs, the Freedreno driver can be used for OpenGL, OpenGL ES, and Vulkan, eliminating the need for Zink for graphics API call translation and significantly improving GPU utilization.  
  - Only drivers relevant to the vast majority of Android devices are compiled to reduce the package size.  
## Compatibility

| GPU | OpenGL | OpenGL ES | Vulkan |
| :------------: | :----: | :-------: | :----: |
| **Adreno GPU** | ✅Supported | ✅Supported | ✅Supported |
| **Mali GPU** | ❔Untested | ❔Untested | ❔Untested |
## Installation
ℹ️**Note**: Currently, the releases for this project only provide `.tar.gz` installation packages. These can only overwrite existing Mesa drivers and cannot be uninstalled directly. They are intended for testing purposes only.  

1.  Go to the [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) page to download an installation package. Please note the Linux distribution suffix in the filename, such as `debian_arm64`. You can only install the package that matches your distribution.  
2.  Extract the installation package directly to the root directory.  
```shell
sudo tar -zxvf mesa-for-android-container_25.3.0-devel- 20250725_debian_arm64.tar.gz -C /
```
3.  Refresh the dynamic linker cache.  
```shell
sudo ldconfig
```
## Building
This project is built in a Debian 13 arm64 environment. For detailed building procedures, please refer to the official Mesa documentation ([Compilation and Installation Using Meson — The Mesa 3D Graphics Library latest documentation](https://docs.mesa3d.org/meson.html)). The key steps are as follows:  

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
5.  Initialize the build directory.  
```shell
meson setup build/
```
6.  Modify the build options. You can refer to the [meson.options](https://github.com/lfdevs/mesa-for-android-container/blob/main/meson.options) file to see all available build options.  
```shell
meson configure build/ \
    --prefix=/usr \
    -Dplatforms=x11,wayland \
    -Dgallium-drivers=freedreno,panfrost,lima,virgl,zink,llvmpipe \
    -Dvulkan-drivers=freedreno,panfrost \
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
7.  Start the build.  
```shell
ninja -C build/
```
8.  To install directly on the build device, run the following command:  
```shell
ninja -C build/ install
```
9.  If you need to package the built Mesa drivers for installation on other devices with the same distribution, refer to the following commands:  
```shell
sudo mkdir /tmp/mesa-install-tmp
sudo DESTDIR=/tmp/mesa-install-tmp meson install -C build/
sudo tar -zcvf mesa-for-android-container_25.3.0-devel-20250725_debian_arm64.tar.gz -C /tmp/mesa-install-tmp .
sudo chown $USER:$USER mesa-for-android-container_25.3.0-devel-20250725_debian_arm64.tar.gz
chmod 644 mesa-for-android-container_25.3.0-devel-20250725_debian_arm64.tar.gz
sudo rm -rf /tmp/mesa-install-tmp
```
## Benchmarks
Detailed test results: [benchmark-result](../common/benchmark-result.md)  

|      Device      |   Model    |             SoC              |    GPU     | glmark2 | glmark2-es2 |      vkmark |
| :--------------: | :--------: | :--------------------------: | :--------: | ------: | ----------: | ----------: |
|  Redmi K40 Pro   | M2012K11G  |   Qualcomm Snapdragon 888    | Adreno 660 |     842 |         771 |        1170 |
| Xiaomi Pad 6 Pro | 23046RP50C | Qualcomm Snapdragon 8+ Gen 1 | Adreno 730 |    1169 |        1143 | Not working |
## Acknowledgements
  - [Lucas Fryzek](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21570): Author of the KGSL backend code for the Mesa Freedreno driver.  
  - [xMeM](https://github.com/xMeM/termux-packages/commit/401982b8d9eaef70669762bfff2a963341c65e52): For porting the Freedreno driver's KGSL backend to Termux:X11.  
  - [robertkirkman](https://github.com/robertkirkman/termux-packages/commit/06a959eeddf153cebd0d3ea1a6c2eb2921b5f786): For integrating and improving xMeM's patches.  