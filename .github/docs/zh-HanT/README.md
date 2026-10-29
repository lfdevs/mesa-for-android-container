# Mesa for Android Container
Forked From [Mesa - The 3D Graphics Library](https://gitlab.freedesktop.org/mesa/mesa)

[English](../../README.md) | [简体中文](../zh-HanS/README.md) | **繁體中文**

---

一個適用於 Android 容器（PRoot、Chroot、LXC、Droidspaces 等）的 [Mesa](https://gitlab.freedesktop.org/mesa/mesa) 建置版本，支援 Adreno GPU 的硬體加速。

## 特性
  - Mesa 26.0.0 及以上版本的 Freedreno 與 Turnip 驅動程式支援 Adreno 8xx GPU。
  - 在多個主流 Linux 發行版的 ARM64 Docker 容器環境下分別編譯，相容性更佳，以 Android 為主機系統的 PRoot、Chroot、LXC、Droidspaces 容器皆可使用本專案所編譯的 Mesa 驅動程式。
  - 對於部分 Adreno 6xx/7xx/8xx GPU，OpenGL、OpenGL ES、Vulkan 均可直接使用 Freedreno 驅動程式，不再需要透過 Zink 進行圖形 API 呼叫轉換，大幅提升了 GPU 的利用效率。
  - 僅編譯與絕大多數 Android 裝置相關的驅動程式，以縮小套件體積。
  - 支援使用 [Termux:X11](https://github.com/termux/termux-x11)、[Anland](https://github.com/superturtlee/anland) 和 [Anland: Termux](https://github.com/lfdevs/anland-termux) 作為顯示服務。
  - 支援透過 [Termux:VA](https://github.com/lfdevs/termux-va) 進行硬體影片解碼與編碼，可依照此指南進行嘗試：[Termux:VA User Guide](https://github.com/lfdevs/termux-va/blob/main/doc/user-guide.md)

## 相容性
這裡只展示了經我和其他人實測後確認支援的 GPU 型號，**不代表其他的 GPU 型號不支援**。你可以在 [freedreno_devices.py](https://github.com/lfdevs/mesa-for-android-container/blob/turnip-main/src/freedreno/common/freedreno_devices.py) 檔案中搜尋你裝置的 GPU 型號（例如，Adreno 725 可搜尋`725`），若該型號有完整的裝置定義，則它很可能受支援。歡迎在 [Issues](https://github.com/lfdevs/mesa-for-android-container/issues) 中分享驅動程式在你裝置上的執行情況，這將幫助我們完善下面的這張表。

|              GPU               | OpenGL | OpenGL ES | Vulkan |
| :----------------------------: | :----: | :-------: | :----: |
|         **Adreno 660**         |  ✔️支援  |   ✔️支援    |  ✔️支援  |
| **Adreno 710/720/722/730/732/735/740/750** |  ✔️支援  |   ✔️支援    |  ✔️支援  |
|       **Adreno 810/829/830/840**       |  ✔️支援  |   ✔️支援    |  ✔️支援  |

實驗性支援：**Adreno 825**（by [**whitebelyash**](https://github.com/whitebelyash)）、**Adreno 850**

## 安裝
本專案的 Releases 有兩種形式的安裝包，一種可以使用 Linux 發行版的套件管理器安裝，另一種只能直接解壓來安裝。若需要最新的 Mesa 功能（例如 **Adreno 8xx 的支援**）或 Anland 支援，請使用 `26.3.0` 或更新版本的安裝包。

若常規的 Release（標題不帶 `turnip-` 前綴）中的 Turnip 驅動無法正常運作，可使用**未打補丁的 Turnip 驅動**（標題帶 `turnip-` 前綴），直接覆蓋安裝即可。

**PS:** 本專案所提及的「未打補丁」指的是未套用 xMeM 的原始補丁，但仍會套用一些必要的補丁（例如新增更多 GPU 支援）。

### 使用套件管理器
根據所使用的 Linux 發行版，前往 [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) 下載對應 Release 的所有套件，並依照 Release 說明中的安裝指示進行安裝。以下為一些主流 Linux 發行版對應的最新 Release：

|    Linux 發行版     |                                                              最新 Release                                                              |                                                               未打補丁的 Turnip 驅動                                                               |
| :--------------: | :----------------------------------------------------------------------------------------------------------------------------------: | :-----------------------------------------------------------------------------------------------------------------------------------------: |
|    Debian 13     |            [25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-adreno)            |            [turnip-25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-turnip)            |
| Ubuntu 24.04 LTS | [25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-adreno) | [turnip-25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-turnip) |
|    Fedora 43     |             [25.2.7-4.fc43](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-25.2.7-4.fc43-adreno)             |               [turnip-25.2.7-4.fc43](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-25.2.7-4.fc43)                |
|    Arch Linux    |               [27.0.0-91](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-27.0.0-devel-20261025-r1)               |              [turnip-27.0.0-90](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-27.0.0-devel-20261025)              |

Termux native 版本可在另一個儲存庫的 Releases 中取得：<https://github.com/lfdevs/termux-packages/releases/latest>

### 直接解壓

> [!NOTE]
> Releases 中 `.tar.gz` 格式的安裝包僅能覆蓋原有的 Mesa 驅動程式，卸載時需手動刪除解壓出來的檔案，僅供測試使用。

目前「直接解壓」方式支援以下 Linux 發行版：

|    Linux 發行版     |       安裝包檔案名稱字尾        |
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

1. 前往 [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) 下載 `.tar.gz` 格式的安裝包。請注意檔案名稱中的 Linux 發行版後綴（例如 `debian_trixie_arm64`），僅可安裝與發行版相符的安裝包。

| 標準安裝包 | 未打補丁的 Turnip 安裝包（通常不需要） |
| :-: | :--: |
| [27.0.0-devel-20261025-r1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-27.0.0-devel-20261025-r1) | [turnip-27.0.0-devel-20261025](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-27.0.0-devel-20261025) |

> [!NOTE]
> 對於 7XX & 8XX，現在應該不再需要安裝未打補丁的 Turnip 驅動程式了。標準安裝包中打過補丁的 Turnip 驅動程式應該能正常運作。

若需要儘可能新的 Mesa 上游特性，可以使用 **Turnip 每週建置**：[turnip-weekly](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-weekly)

`turnip-weekly`可以與標準安裝包（Release 標題不帶`turnip-`前綴）搭配使用，也可以單獨使用 **（通常相容性更好）**。單獨使用時，需要將環境變數`MESA_LOADER_DRIVER_OVERRIDE`的值由`kgsl`改為`zink`。

> [!NOTE]
> `turnip-weekly`由 GitHub Actions 每週定時拉取上游主線程式碼進行建置，且**未經測試直接發布**，所以可能會出現各種問題。這些問題通常是由於上游尚未開發完成某個特性，需要等待上游將該特性開發完畢才能解決。所以當使用`turnip-weekly`遇到問題時，可以更換為其他版本的驅動程式，或者等待下週的建置。

2.  直接將安裝包解壓縮到根目錄。

```bash
sudo tar -zxvf mesa-for-android-container_26.2.0-devel-xxxxxxxx_debian_trixie_arm64.tar.gz -C /
```
3.  更新動態連結器快取。

```bash
sudo ldconfig
```

卸載可參考以下指令：

```bash
# 複製這條指令輸出的檔案清單
tar tf mesa-for-android-container_26.2.0-devel-xxxxxxxx_debian_trixie_arm64.tar.gz | grep -v '/$' | tr '\n' ' ' ; echo
cd /
# 將 <file-list> 替換為實際的檔案清單
sudo rm <file-list>
# 重新安裝發行版維護的 Mesa 驅動程式
# Debian 或 Ubuntu：
sudo apt update
sudo apt install --reinstall libegl-mesa0 libgbm1 libgl1-mesa-dri libglx-mesa0 mesa-libgallium mesa-vulkan-drivers
# Fedora:
sudo dnf reinstall mesa-filesystem mesa-libglapi mesa-libgbm mesa-libEGL mesa-libGL mesa-vulkan-drivers mesa-dri-drivers mesa-libOpenCL
# Arch Linux:
sudo pacman -S mesa mesa-docs opencl-mesa vulkan-freedreno vulkan-mesa-implicit-layers vulkan-mesa-layers
```
## 使用
在執行特定程式時指定環境變數`MESA_LOADER_DRIVER_OVERRIDE`，例如：

```bash
MESA_LOADER_DRIVER_OVERRIDE=kgsl glmark2
```

或者將其新增至`/etc/environment`檔案中，以便在開啟容器時自動載入：

```plaintext
MESA_LOADER_DRIVER_OVERRIDE=kgsl
```

> [!TIP]
>
> - 若執行某些程式或遊戲時出現畫面撕裂問題，可新增以下兩個環境變數以強制開啟垂直同步：
>
>   ```bash
>   vblank_mode=3 MESA_VK_WSI_PRESENT_MODE=mailbox
>   ```
>
> - `27.0.0-devel-20261025` 及以上版本預設為 X11 和 XWayland 啟用 [present wait fence](https://github.com/lfdevs/mesa-for-android-container/pull/96)，以避免某些應用程式或遊戲出現畫面撕裂問題，這可能會影響極限效能。如果你不需要此功能，可以設定 `MESA_DRI3_DISABLE_PRESENT_WAIT_FENCE=1` 來停用它。

## 開發
如果你是開發者，想要建置本專案的驅動程式或貢獻程式碼，請參閱[開發文件](../common/development.md)。

## 基準測試
詳細的測試結果： [benchmark-result](../common/benchmark-result.md)

| 裝置 | SoC | GPU | 容器類型 | glmark2 | glmark2-es2 | vkmark |
| :-: | :-: | :-: | :-: | -: | -: | -: |
| Redmi K40 Pro | 驍龍 888 | Adreno 660 | 842 | 771 | 1170 |
| Xiaomi Pad 6 Pro | 驍龍 8+ Gen 1 | Adreno 730 | 1360 | 1222 | 2669 |
| REDMI K80 Pro | 驍龍 8 Elite | Adreno 830 | 2211 | 2206 | 1153 |
| OnePlus 15 | 驍龍 8 Elite Gen 5 | Adreno 840 | 3574 | 3621 | 未測試 |

## 感謝
  - [Lucas Fryzek](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21570)：Mesa Freedreno 驅動程式的 KGSL 後端程式碼的作者。
  - [xMeM](https://github.com/xMeM/termux-packages/commit/401982b8d9eaef70669762bfff2a963341c65e52)：將 Freedreno 驅動程式的 KGSL 後端移植至 Termux:X11。
  - [Robert Kirkman](https://github.com/robertkirkman/termux-packages/commit/06a959eeddf153cebd0d3ea1a6c2eb2921b5f786)：整合並完善了 xMeM 的修補程式。
  - [Rob Clark](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/38450)：為 Adreno Gen8 架構（包含 Adreno 840）引入了 Freedreno （包含 Turnip）支援。
  - [whitebelyash](https://github.com/whitebelyash/mesa-tu8)：為 Adreno 825 新增實驗性支援。
  - [Termux 維護團隊及貢獻者](https://github.com/termux/termux-packages/tree/master/packages/mesa)：為 Mesa 驅動程式在 Termux 上的正常執行開發了一系列的修補程式。

## Star 歷史

<a href="https://www.star-history.com/?repos=lfdevs%2Fmesa-for-android-container&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=lfdevs/mesa-for-android-container&type=date&theme=dark&legend=top-left&sealed_token=-2pLkYNq1aQwJzT6Y1uRCEDJafu_HLcd9hb1VGrC9AFHi5L1fU21UewEFoql9B96-B2_Th981yCh2-JTfhuYnWfcEZy3pynzSJZbRf7wZFfLfxXqRO-dcbgT5JFsZ_qyxdVKBbY9bX_gtmBCB7vhMYejuCdB1r1N3ssXmepoU3JLI4Ds637uH0CXhm_Y" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=lfdevs/mesa-for-android-container&type=date&legend=top-left&sealed_token=-2pLkYNq1aQwJzT6Y1uRCEDJafu_HLcd9hb1VGrC9AFHi5L1fU21UewEFoql9B96-B2_Th981yCh2-JTfhuYnWfcEZy3pynzSJZbRf7wZFfLfxXqRO-dcbgT5JFsZ_qyxdVKBbY9bX_gtmBCB7vhMYejuCdB1r1N3ssXmepoU3JLI4Ds637uH0CXhm_Y" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=lfdevs/mesa-for-android-container&type=date&legend=top-left&sealed_token=-2pLkYNq1aQwJzT6Y1uRCEDJafu_HLcd9hb1VGrC9AFHi5L1fU21UewEFoql9B96-B2_Th981yCh2-JTfhuYnWfcEZy3pynzSJZbRf7wZFfLfxXqRO-dcbgT5JFsZ_qyxdVKBbY9bX_gtmBCB7vhMYejuCdB1r1N3ssXmepoU3JLI4Ds637uH0CXhm_Y" />
 </picture>
</a>
