# Mesa for Android Container
Forked From [Mesa - The 3D Graphics Library](https://gitlab.freedesktop.org/mesa/mesa)  

[English](../../README.md) | **简体中文** | [繁體中文](../zh-HanT/README.md)  

---  
一个适用于 Android 容器（Proot、Chroot、LXC 等）的 [Mesa](https://gitlab.freedesktop.org/mesa/mesa) 构建，支持 Adreno、Mali GPU 的硬件加速。  

## 特性
  - 直接使用修改后的 Mesa 官方仓库源码在 Debian 13 的 Linux arm64 Chroot 容器下编译，向下兼容 Debian 12，以 Android 为宿主机的 Proot、Chroot、LXC 容器均可使用本项目编译的 Mesa 驱动。  
  - 对于部分 Adreno 6xx/7xx GPU，OpenGL、OpenGL ES、Vulkan 均可使用 Freedreno 驱动，不再需要使用 Zink 进行图形 API 调用转换，极大地提高了 GPU 的利用效率。  
  - 只编译与绝大部分 Android 设备相关的驱动，减小包体体积。  
## 兼容性

|      GPU       | OpenGL | OpenGL ES | Vulkan |
| :------------: | :----: | :-------: | :----: |
| **Adreno GPU** |  ✅支持   |    ✅支持    |  ✅支持   |
|  **Mali GPU**  |  ❔未测试  |   ❔未测试    |  ❔未测试  |
## 安装
ℹ️**注意**：目前本项目的 Releases 只有`.tar.gz`格式的安装包，只能覆盖原来的 Mesa 驱动，不能直接卸载，仅供测试使用。  

1. 前往 [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) 下载一个安装包。请注意文件名中的 Linux 发行版后缀，如`debian_arm64`，只能安装发行版匹配的安装包。  
2. 直接将安装包解压到根目录。  
```shell
sudo tar -zxvf mesa-for-android-container_25.3.0-devel-20250725_debian_arm64.tar.gz -C /
```
3. 刷新动态链接器缓存。  
```shell
sudo ldconfig
```
## 使用
### Adreno GPU
在运行特定程序时指定环境变量`MESA_LOADER_DRIVER_OVERRIDE`和`TU_DEBUG`，如：  
```shell
MESA_LOADER_DRIVER_OVERRIDE=kgsl TU_DEBUG=noconform glmark2
```
或者将其添加到`/etc/environment`文件中以便在开启容器时自动加载：  
```plaintext
MESA_LOADER_DRIVER_OVERRIDE=kgsl
TU_DEBUG=noconform
```
### Mali GPU
ℹ️**注意**：目前本驱动未在 Mali GPU 上进行测试，以下方法不一定可行。  
  - 对于较新的 Mali GPU （基于 Midgard 和 Bifrost 微架构），指定环境变量`GALLIUM_DRIVER=panfrost`。  
  - 对于较老的 Mali GPU （基于 Utgard 架构），指定环境变量`GALLIUM_DRIVER=lima`。  
## 构建
本项目在 Debian 13 arm64 环境下构建，详细构建过程请参照 Mesa 官方说明（[Compilation and Installation Using Meson — The Mesa 3D Graphics Library latest documentation](https://docs.mesa3d.org/meson.html)），以下为关键步骤：  

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
5. 初始化构建目录。  
```shell
meson setup build/
```
6. 修改构建选项。可以参照 [meson.options](https://github.com/lfdevs/mesa-for-android-container/blob/main/meson.options) 文件查看所有的构建选项。  
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
7. 开始构建。  
```shell
ninja -C build/
```
8. 若直接在编译设备上安装，可运行以下命令：  
```shell
ninja -C build/ install
```
9. 若需打包构建好的 Mesa 驱动，并在相同发行版的其他设备上安装，可参考以下命令：  
```shell
sudo mkdir /tmp/mesa-install-tmp
sudo DESTDIR=/tmp/mesa-install-tmp meson install -C build/
sudo tar -zcvf mesa-for-android-container_25.3.0-devel-20250725_debian_arm64.tar.gz -C /tmp/mesa-install-tmp .
sudo chown $USER:$USER mesa-for-android-container_25.3.0-devel-20250725_debian_arm64.tar.gz
chmod 644 mesa-for-android-container_25.3.0-devel-20250725_debian_arm64.tar.gz
sudo rm -rf /tmp/mesa-install-tmp
```
## 基准测试
详细的测试结果：[benchmark-result](../common/benchmark-result.md)  

|        设备        |     型号     |      SoC      |    GPU     | glmark2 | glmark2-es2 | vkmark |
| :--------------: | :--------: | :-----------: | :--------: | ------: | ----------: | -----: |
|  Redmi K40 Pro   | M2012K11G  |   高通骁龙 888    | Adreno 660 |     842 |         771 |   1170 |
| Xiaomi Pad 6 Pro | 23046RP50C | 高通骁龙 8+ Gen 1 | Adreno 730 |    1169 |        1143 |    无法运行 |
## 感谢
  - [Lucas Fryzek](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21570)：Mesa Freedreno 驱动的 KGSL 后端代码的作者。  
  - [xMeM](https://github.com/xMeM/termux-packages/commit/401982b8d9eaef70669762bfff2a963341c65e52)：将 Freedreno 驱动的 KGSL 后端移植到了 Termux:X11。  
  - [robertkirkman](https://github.com/robertkirkman/termux-packages/commit/06a959eeddf153cebd0d3ea1a6c2eb2921b5f786)：整合并完善了 xMeM 的补丁。  
