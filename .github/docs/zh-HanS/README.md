# Mesa for Android Container
Forked From [Mesa - The 3D Graphics Library](https://gitlab.freedesktop.org/mesa/mesa)  

[English](../../README.md) | **简体中文** | [繁體中文](../zh-HanT/README.md)  

---  
一个适用于 Android 容器（Proot、Chroot、LXC 等）的 [Mesa](https://gitlab.freedesktop.org/mesa/mesa) 构建，支持 Adreno GPU 的硬件加速。  

## 特性
  - Mesa 26.0.0 及以上版本的 Freedreno 驱动支持 Adreno 840 GPU。  
  - 在多个主流的 Linux 发行版的 arm64 Chroot 容器下分别编译，兼容性更好，以 Android 为宿主机的 Proot、Chroot、LXC 容器均可使用本项目编译的 Mesa 驱动。  
  - 对于部分 Adreno 6xx/7xx GPU，OpenGL、OpenGL ES、Vulkan 均可使用 Freedreno 驱动，不再需要使用 Zink 进行图形 API 调用转换，极大地提高了 GPU 的利用效率。  
  - 只编译与绝大部分 Android 设备相关的驱动，减小包体体积。  
## 兼容性

|          GPU           | OpenGL | OpenGL ES | Vulkan (Zink) | Vulkan |
| :--------------------: | :----: | :-------: | :-----------: | :----: |
|     **Adreno 660**     |  ✔️支持  |   ✔️支持    |     ✔️支持      |  ✔️支持  |
| **Adreno 730/740/750** |  ✔️支持  |   ✔️支持    |     ✔️支持      |  ✔️支持  |
|     **Adreno 840**     |  ✔️支持  |   ✔️支持    |     ❔未测试      |  ❌不支持  |
## 安装
本项目的 Releases 有两种形式的安装包，一种可以使用 Linux 发行版的软件包管理器安装，另一种只能直接解压来安装。推荐使用第一种安装包，若需要最新的 Mesa 特性（如 Adreno 840 的支持），则可以使用第二种。  
若常规的 Release （标题不带`turnip-`前缀）的 Turnip 驱动不能正常运行，可以使用**未打补丁的 Turnip 驱动**（标题带`turnip-`前缀），直接覆盖安装即可。  
### 使用软件包管理器
根据使用的 Linux 发行版，前往 [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) 下载某个对应的 Release 的所有软件包，按照 Release 说明中的安装说明进行安装。以下为一些主流的 Linux 发行版对应的最新 Release：  

| Linux 发行版  |                                                              最新 Release                                                              |                                                               未打补丁的 Turnip 驱动                                                               |
| :--------: | :----------------------------------------------------------------------------------------------------------------------------------: | :-----------------------------------------------------------------------------------------------------------------------------------------: |
|   Debian   |            [25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-adreno)            |            [turnip-25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-turnip)            |
|   Ubuntu   | [25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-adreno) | [turnip-25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-turnip) |
| Arch Linux |              [26.0.0-1.1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-26.0.0-devel-20251209)              |              [turnip-26.0.0-2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-26.0.0-devel-20251215)              |
### 直接解压
ℹ️**注意**：Releases 中`.tar.gz`格式的安装包只能覆盖原来的 Mesa 驱动，卸载时需要手动删除解压得到的文件，仅供测试使用。  

1. 前往 [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) 下载一个`.tar.gz`格式的安装包。请注意文件名中的 Linux 发行版后缀，如`debian_trixie_arm64`，只能安装发行版匹配的安装包。  
2. 直接将安装包解压到根目录。  
```shell
sudo tar -zxvf mesa-for-android-container_26.0.0-devel-20251209_debian_trixie_arm64.tar.gz -C /
```
3. 刷新动态链接器缓存。  
```shell
sudo ldconfig
```

卸载可参考以下命令：  
```shell
# 复制这条命令输出的文件列表
tar tf mesa-for-android-container_26.0.0-devel-20251209_debian_trixie_arm64.tar.gz | grep -v '/$' | tr '\n' ' ' ; echo
cd /
# 替换 <file-list> 为实际的文件列表
sudo rm <file-list>
# 重新安装发行版维护的 Mesa 驱动
# Debian 或 Ubuntu:
sudo apt update
sudo apt install --reinstall libegl-mesa0 libgbm1 libgl1-mesa-dri libglx-mesa0 mesa-libgallium mesa-vulkan-drivers
# Fedora:
sudo dnf reinstall mesa-filesystem mesa-libglapi mesa-libgbm mesa-libEGL mesa-libGL mesa-vulkan-drivers mesa-dri-drivers mesa-libOpenCL
# Arch Linux:
sudo pacman -S mesa mesa-docs opencl-mesa vulkan-freedreno vulkan-mesa-implicit-layers vulkan-mesa-layers
```
## 使用
在运行特定程序时指定环境变量`MESA_LOADER_DRIVER_OVERRIDE`和`TU_DEBUG`，如：  
```shell
MESA_LOADER_DRIVER_OVERRIDE=kgsl TU_DEBUG=noconform glmark2
```
或者将其添加到`/etc/environment`文件中以便在开启容器时自动加载：  
```plaintext
MESA_LOADER_DRIVER_OVERRIDE=kgsl
TU_DEBUG=noconform
```
## 构建
详细构建过程请参照 Mesa 官方说明（[Compilation and Installation Using Meson — The Mesa 3D Graphics Library latest documentation](https://docs.mesa3d.org/meson.html)），以下为在 Debian 13 arm64 环境下构建的关键步骤：  

1. 检查是否启用了源码软件源。若使用传统格式（`/etc/apt/sources.list`）的软件源，可检查是否有类似于以下的配置。  
```plaintext
deb-src https://deb.debian.org/debian trixie main contrib non-free non-free-firmware
```
2. 安装构建 Mesa 所需的依赖。  
```shell
sudo apt build-dep mesa
```
3. 克隆本项目的仓库。  
```shell
git clone https://github.com/lfdevs/mesa-for-android-container.git
```
4. 切换到已经应用了相关补丁的分支，如`adreno-main`。  
```shell
cd mesa-for-android-container
git checkout adreno-main
```
5. 初始化构建目录。可以参照 [meson.options](https://github.com/lfdevs/mesa-for-android-container/blob/main/meson.options) 文件查看所有的构建选项。  
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
6. 开始构建。  
```shell
ninja -C build/
```
7. 若直接在编译设备上安装，可运行以下命令：  
```shell
ninja -C build/ install
```
8. 若需打包构建好的 Mesa 驱动，并在相同发行版的其他设备上安装，可参考以下命令：  
```shell
export MESA_RELEASE_NAME_SUFFIX=26.0.0-devel-20251209_debian_trixie_arm64
sudo mkdir /tmp/mesa-install-tmp
sudo DESTDIR=/tmp/mesa-install-tmp meson install -C build/
sudo tar -zcvf mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz -C /tmp/mesa-install-tmp .
sudo chown $USER:$USER mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz
chmod 644 mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz
sudo rm -rf /tmp/mesa-install-tmp
```
## 基准测试
详细的测试结果：[benchmark-result](../common/benchmark-result.md)  

|        设备         |     型号     |        SoC         |    GPU     | glmark2 | glmark2-es2 | vkmark |
| :---------------: | :--------: | :----------------: | :--------: | ------: | ----------: | -----: |
|   Redmi K40 Pro   | M2012K11G  |      高通骁龙 888      | Adreno 660 |     842 |         771 |   1170 |
| Xiaomi Pad 6 Pro  | 23046RP50C |   高通骁龙 8+ Gen 1    | Adreno 730 |    1169 |        1143 |    263 |
| Xiaomi 17 Pro Max | 2509FPN0BC | 高通骁龙 8 Elite Gen 5 | Adreno 840 |    2947 |        2901 |   无法运行 |
## 感谢
  - [Lucas Fryzek](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21570)：Mesa Freedreno 驱动的 KGSL 后端代码的作者。  
  - [xMeM](https://github.com/xMeM/termux-packages/commit/401982b8d9eaef70669762bfff2a963341c65e52)：将 Freedreno 驱动的 KGSL 后端移植到了 Termux:X11。  
  - [Robert Kirkman](https://github.com/robertkirkman/termux-packages/commit/06a959eeddf153cebd0d3ea1a6c2eb2921b5f786)：整合并完善了 xMeM 的补丁。  
  - [Rob Clark](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/38450)：为 Adreno Gen8 架构（包含 Adreno 840）引入了初步的 Freedreno 支持。
