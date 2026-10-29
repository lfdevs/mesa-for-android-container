# Mesa for Android Container
Forked From [Mesa - The 3D Graphics Library](https://gitlab.freedesktop.org/mesa/mesa)

[English](../../README.md) | **简体中文** | [繁體中文](../zh-HanT/README.md)

---

一个适用于 Android 容器（PRoot、Chroot、LXC、Droidspaces 等）的 [Mesa](https://gitlab.freedesktop.org/mesa/mesa) 构建，支持 Adreno GPU 的硬件加速。

## 特性
  - Mesa 26.0.0 及以上版本的 Freedreno 和 Turnip 驱动支持 Adreno 8xx GPU。
  - 在多个主流的 Linux 发行版的 ARM64 Docker 容器下分别编译，兼容性更好，以 Android 为宿主机的 PRoot、Chroot、LXC、Droidspaces 容器均可使用本项目编译的 Mesa 驱动。
  - 对于部分 Adreno 6xx/7xx/8xx GPU，OpenGL、OpenGL ES、Vulkan 均可使用 Freedreno 驱动，不再需要使用 Zink 进行图形 API 调用转换，极大地提高了 GPU 的利用效率。
  - 只编译与绝大部分 Android 设备相关的驱动，减小包体体积。
  - 支持使用 [Termux:X11](https://github.com/termux/termux-x11)、[Anland](https://github.com/superturtlee/anland) 和 [Anland: Termux](https://github.com/lfdevs/anland-termux) 作为显示服务。
  - 支持通过 [Termux:VA](https://github.com/lfdevs/termux-va) 进行硬件视频解码和编码，可以按照该指南来进行尝试：[Termux:VA User Guide](https://github.com/lfdevs/termux-va/blob/main/doc/user-guide.md)

## 兼容性
这里只展示了经我和其他人实测后确认支持的 GPU 型号，**不代表其他的 GPU 型号不支持**。你可以在 [freedreno_devices.py](https://github.com/lfdevs/mesa-for-android-container/blob/turnip-main/src/freedreno/common/freedreno_devices.py) 文件中搜索你设备的 GPU 型号（例如，Adreno 725 可搜索`725`），若该型号有完整的设备定义，则它很可能受支持。欢迎在 [Issues](https://github.com/lfdevs/mesa-for-android-container/issues) 中分享驱动在你的设备上的运行情况，这将帮助我们完善下面的这张表。

|              GPU               | OpenGL | OpenGL ES | Vulkan |
| :----------------------------: | :----: | :-------: | :----: |
|         **Adreno 660**         |  ✔️支持  |   ✔️支持    |  ✔️支持  |
| **Adreno 710/720/722/730/732/735/740/750** |  ✔️支持  |   ✔️支持    |  ✔️支持  |
|       **Adreno 810/829/830/840**       |  ✔️支持  |   ✔️支持    |  ✔️支持  |

实验性支持：**Adreno 825**（by [**whitebelyash**](https://github.com/whitebelyash)）、**Adreno 850**

## 安装
本项目的 Releases 有两种形式的安装包，一种可以使用 Linux 发行版的软件包管理器安装，另一种只能直接解压来安装。若需要最新的 Mesa 特性（如 **Adreno 8xx 的支持**）或者 Anland 支持，请使用 `26.3.0` 或更新版本的安装包。

若常规的 Release （标题不带`turnip-`前缀）的 Turnip 驱动不能正常运行，可以使用**未打补丁的 Turnip 驱动**（标题带`turnip-`前缀），直接覆盖安装即可。

**PS:** 本项目提到的“未打补丁”指的是没有应用 xMeM 的原始补丁，依然会应用一些必要的补丁（如更多的 GPU 支持）。

### 使用软件包管理器
根据使用的 Linux 发行版，前往 [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) 下载某个对应的 Release 的所有软件包，按照 Release 说明中的安装说明进行安装。以下为一些主流的 Linux 发行版对应的最新 Release：

|    Linux 发行版     |                                                              最新 Release                                                              |                                                               未打补丁的 Turnip 驱动                                                               |
| :--------------: | :----------------------------------------------------------------------------------------------------------------------------------: | :-----------------------------------------------------------------------------------------------------------------------------------------: |
|    Debian 13     |            [25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-adreno)            |            [turnip-25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-turnip)            |
| Ubuntu 24.04 LTS | [25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-adreno) | [turnip-25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-turnip) |
|    Fedora 43     |             [25.2.7-4.fc43](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-25.2.7-4.fc43-adreno)             |               [turnip-25.2.7-4.fc43](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-25.2.7-4.fc43)                |
|    Arch Linux    |               [27.0.0-91](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-27.0.0-devel-20261025-r1)               |              [turnip-27.0.0-90](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-27.0.0-devel-20261025)              |

Termux native 版本在另一个仓库的 Releases 里：<https://github.com/lfdevs/termux-packages/releases/latest>

### 直接解压

> [!NOTE]
> Releases 中`.tar.gz`格式的安装包只能覆盖原来的 Mesa 驱动，卸载时需要手动删除解压得到的文件，仅供测试使用。

目前“直接解压”方式支持以下 Linux 发行版：

|    Linux 发行版     |        安装包文件名后缀        |
| :--------------: | :--------------------: |
|    Debian 13     | `debian_trixie_arm64`  |
| Ubuntu 24.04 LTS |  `ubuntu_noble_arm64`  |
|   Ubuntu 25.10   | `ubuntu_questing_arm64` |
| Ubuntu 26.04 LTS | `ubuntu_resolute_arm64` |
|    Fedora 43     |   `fedora_43_arm64`    |
|    Fedora 44     |   `fedora_44_arm64`    |
|    Arch Linux    |   `archlinux_arm64`    |
|    Void Linux    |      `void_arm64`      |
|    Alpine 3.24   |   `alpine_3.24_arm64`  |

1. 前往 [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) 下载`.tar.gz`格式的安装包。请注意文件名中的 Linux 发行版后缀，如`debian_trixie_arm64`，只能安装发行版匹配的安装包。

| 标准安装包 | 未打补丁的 Turnip 安装包（通常不需要） |
| :-: | :-: |
| [27.0.0-devel-20261025-r1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-27.0.0-devel-20261025-r1) | [turnip-27.0.0-devel-20261025](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-27.0.0-devel-20261025) |

> [!NOTE]
> 对于 Adreno 7XX & 8XX，现在应该不再需要安装未打补丁的 Turnip 驱动了。标准安装包中打了补丁的 Turnip 驱动应该能正常运行。

若需要尽可能新的 Mesa 上游特性，可以使用 **Turnip 每周构建**：[turnip-weekly](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-weekly)

`turnip-weekly`可以与标准安装包（Release 标题不带`turnip-`前缀）搭配使用，也可以单独使用 **（通常兼容性更好）**。单独使用时，需要将环境变量`MESA_LOADER_DRIVER_OVERRIDE`的值由`kgsl`改为`zink`。

> [!NOTE]
> `turnip-weekly`由 GitHub Actions 每周定时拉取上游主线代码进行构建，且**不经测试直接发布**，所以可能会出现各种问题。这些问题通常是由于上游还未开发完成某个特性，需要等待上游将该特性开发完毕才能解决。所以当使用`turnip-weekly`遇到问题时，可以更换为其他版本的驱动，或者等待下周的构建。

2. 直接将安装包解压到根目录。

```bash
sudo tar -zxvf mesa-for-android-container_26.2.0-devel-xxxxxxxx_debian_trixie_arm64.tar.gz -C /
```
3. 刷新动态链接器缓存。

```bash
sudo ldconfig
```

卸载可参考以下命令：

```bash
# 复制这条命令输出的文件列表
tar tf mesa-for-android-container_26.2.0-devel-xxxxxxxx_debian_trixie_arm64.tar.gz | grep -v '/$' | tr '\n' ' ' ; echo
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
在运行特定程序时指定环境变量`MESA_LOADER_DRIVER_OVERRIDE`，如：

```bash
MESA_LOADER_DRIVER_OVERRIDE=kgsl glmark2
```

或者将其添加到`/etc/environment`文件中以便在开启容器时自动加载：

```plaintext
MESA_LOADER_DRIVER_OVERRIDE=kgsl
```

> [!TIP]
>
> - 若运行某些程序或游戏时出现画面撕裂问题，可添加以下两个环境变量以强制开启垂直同步：
>
>   ```bash
>   vblank_mode=3 MESA_VK_WSI_PRESENT_MODE=mailbox
>   ```
>
> - `27.0.0-devel-20261025` 及以上版本为 X11 和 XWayland 默认启用了 [present wait fence](https://github.com/lfdevs/mesa-for-android-container/pull/96) 以避免某些应用或游戏的画面撕裂问题，这可能对极限性能有所影响。如果你不需要该特性，可以通过声明环境变量 `MESA_DRI3_DISABLE_PRESENT_WAIT_FENCE=1` 来禁用它。

## 开发
如果你是开发者，想要构建本项目的驱动或贡献代码，请参阅[开发文档](../common/development.md)。

## 基准测试
详细的测试结果：[benchmark-result](../common/benchmark-result.md)

| 设备 | SoC | GPU | 容器类型 | glmark2 | glmark2-es2 | vkmark |
| :-: | :-: | :-: | :-: | -: | -: | -: |
| Redmi K40 Pro | 骁龙 888 | Adreno 660 | LXC | 842 | 771 | 1170 |
| Xiaomi Pad 6 Pro | 骁龙 8+ Gen 1 | Adreno 730 | Chroot | 1360 | 1222 | 2669 |
| REDMI K80 Pro | 骁龙 8 Elite | Adreno 830 | PRoot | 2211 | 2206 | 1153 |
| OnePlus 15 | 骁龙 8 Elite Gen 5 | Adreno 840 | PRoot | 3574 | 3621 | 未测试 |

## 感谢
  - [Lucas Fryzek](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21570)：Mesa Freedreno 驱动的 KGSL 后端代码的作者。
  - [xMeM](https://github.com/xMeM/termux-packages/commit/401982b8d9eaef70669762bfff2a963341c65e52)：将 Freedreno 驱动的 KGSL 后端移植到了 Termux:X11。
  - [Robert Kirkman](https://github.com/robertkirkman/termux-packages/commit/06a959eeddf153cebd0d3ea1a6c2eb2921b5f786)：整合并完善了 xMeM 的补丁。
  - [Rob Clark](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/38450)：为 Adreno Gen8 架构（包含 Adreno 840）引入了 Freedreno （包含 Turnip）支持。
  - [whitebelyash](https://github.com/whitebelyash/mesa-tu8)：为 Adreno 825 添加实验性支持。
  - [Termux 维护团队及贡献者](https://github.com/termux/termux-packages/tree/master/packages/mesa)：为 Mesa 驱动在 Termux 上的正常运行开发了一系列的补丁。

## Star 历史

<a href="https://www.star-history.com/?repos=lfdevs%2Fmesa-for-android-container&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=lfdevs/mesa-for-android-container&type=date&theme=dark&legend=top-left&sealed_token=-2pLkYNq1aQwJzT6Y1uRCEDJafu_HLcd9hb1VGrC9AFHi5L1fU21UewEFoql9B96-B2_Th981yCh2-JTfhuYnWfcEZy3pynzSJZbRf7wZFfLfxXqRO-dcbgT5JFsZ_qyxdVKBbY9bX_gtmBCB7vhMYejuCdB1r1N3ssXmepoU3JLI4Ds637uH0CXhm_Y" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=lfdevs/mesa-for-android-container&type=date&legend=top-left&sealed_token=-2pLkYNq1aQwJzT6Y1uRCEDJafu_HLcd9hb1VGrC9AFHi5L1fU21UewEFoql9B96-B2_Th981yCh2-JTfhuYnWfcEZy3pynzSJZbRf7wZFfLfxXqRO-dcbgT5JFsZ_qyxdVKBbY9bX_gtmBCB7vhMYejuCdB1r1N3ssXmepoU3JLI4Ds637uH0CXhm_Y" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=lfdevs/mesa-for-android-container&type=date&legend=top-left&sealed_token=-2pLkYNq1aQwJzT6Y1uRCEDJafu_HLcd9hb1VGrC9AFHi5L1fU21UewEFoql9B96-B2_Th981yCh2-JTfhuYnWfcEZy3pynzSJZbRf7wZFfLfxXqRO-dcbgT5JFsZ_qyxdVKBbY9bX_gtmBCB7vhMYejuCdB1r1N3ssXmepoU3JLI4Ds637uH0CXhm_Y" />
 </picture>
</a>
