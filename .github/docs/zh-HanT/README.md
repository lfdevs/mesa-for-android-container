# Mesa for Android Container
Forked From [Mesa - The 3D Graphics Library](https://gitlab.freedesktop.org/mesa/mesa)  

[English](../../README.md) | [简体中文](../zh-HanS/README.md) | **繁體中文**  

---  
一個適用於 Android 容器（Proot、Chroot、LXC 等）的 [Mesa](https://gitlab.freedesktop.org/mesa/mesa) 建置版本，支援 Adreno GPU 的硬體加速。  

## 特性
  - Mesa 26.0.0 及以上版本的 Freedreno 驅動程式支援 Adreno 840 GPU。  
  - 在多個主流 Linux 發行版的 arm64 Chroot 容器環境下分別編譯，相容性更佳，以 Android 為主機系統的 Proot、Chroot、LXC 容器皆可使用本專案所編譯的 Mesa 驅動程式。  
  - 只編譯與絕大部分 Android 裝置相關的驅動程式，減小套件大小。  
## 相容性

|          GPU           | OpenGL | OpenGL ES | Vulkan (Zink) | Vulkan |
| :--------------------: | :----: | :-------: | :-----------: | :----: |
|     **Adreno 660**     |  ✔️支援  |   ✔️支援    |     ✔️支援      |  ✔️支援  |
| **Adreno 730/740/750** |  ✔️支援  |   ✔️支援    |     ✔️支援      |  ❌不支援  |
|     **Adreno 840**     |  ✔️支援  |   ✔️支援    |     ❔未測試      |  ❌不支援  |
## 安裝
本專案的 Releases 有兩種形式的安裝包，一種可以使用 Linux 發行版的套件管理器安裝，另一種只能直接解壓來安裝。推薦使用第一種安裝包，若需要最新的 Mesa 功能（例如 Adreno 840 的支援），則可以使用第二種。  
### 使用套件管理器  
根據所使用的 Linux 發行版，前往 [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) 下載對應 Release 的所有套件，並依照 Release 說明中的安裝指示進行安裝。以下為一些主流 Linux 發行版對應的最新 Release：  

| Linux 發行版  |                                                              最新 Release                                                              |
| :--------: | :----------------------------------------------------------------------------------------------------------------------------------: |
|   Debian   |            [25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-adreno)            |
|   Ubuntu   | [25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-adreno) |
| Arch Linux |              [26.0.0-2.1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-26.0.0-devel-20251209)              |
### 直接解壓  
ℹ️**注意**：Releases 中 `.tar.gz` 格式的安裝包僅能覆蓋原有的 Mesa 驅動程式，卸載時需手動刪除解壓出來的檔案，僅供測試使用。  

1. 前往 [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) 下載一個 `.tar.gz` 格式的安裝包。請注意檔案名稱中的 Linux 發行版後綴（例如 `debian_trixie_arm64`），僅可安裝與發行版相符的安裝包。
2.  直接將安裝包解壓縮到根目錄。  
```shell
sudo tar -zxvf mesa-for-android-container_26.0.0-devel-20251209_debian_trixie_arm64.tar.gz -C /
```
3.  更新動態連結器快取。  
```shell
sudo ldconfig
```

卸載可參考以下指令：  
```shell
# 複製這條指令輸出的檔案清單
tar tf mesa-for-android-container_26.0.0-devel-20251209_debian_trixie_arm64.tar.gz | grep -v '/$' | tr '\n' ' ' ; echo
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
在執行特定程式時指定環境變數`MESA_LOADER_DRIVER_OVERRIDE`和`TU_DEBUG`，例如：  
```shell
MESA_LOADER_DRIVER_OVERRIDE=kgsl TU_DEBUG=noconform glmark2
```
或者將其新增至`/etc/environment`檔案中，以便在開啟容器時自動載入：  
```plaintext
MESA_LOADER_DRIVER_OVERRIDE=kgsl
TU_DEBUG=noconform
```
## 建置

詳細的建置流程請參照 Mesa 官方說明（[Compilation and Installation Using Meson — The Mesa 3D Graphics Library latest documentation](https://docs.mesa3d.org/meson.html)），以下為在 Debian 13 arm64 環境下建置的關鍵步驟：  

1.  檢查是否已啟用原始碼軟體來源。若使用傳統格式（`/etc/apt/sources.list`）的軟體來源，可檢查是否有類似於以下的設定。  
```plaintext
deb-src https://deb.debian.org/debian trixie main contrib non-free non-free-firmware
```
2.  安裝建置 Mesa 所需的依賴套件。  
```shell
sudo apt build-dep mesa
```
3.  複製本專案的儲存庫。  
```shell
git clone https://github.com/lfdevs/mesa-for-android-container.git
```
4.  切換到已應用相關修補程式的分支，如 `adreno-main`。  
```shell
cd mesa-for-android-container
git checkout adreno-main
```
5.  初始化建置目錄。可以參照 [meson.options](https://github.com/lfdevs/mesa-for-android-container/blob/main/meson.options) 檔案來查看所有的建置選項。  
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
6.  開始建置。  
```shell
ninja -C build/
```
7.  若要直接在編譯裝置上安裝，可執行以下指令：  
```shell
ninja -C build/ install
```
8.  若需打包建置好的 Mesa 驅動程式，並在相同發行版的其他裝置上安裝，可參考以下指令：  
```shell
export MESA_RELEASE_NAME_SUFFIX=26.0.0-devel-20251209_debian_trixie_arm64
sudo mkdir /tmp/mesa-install-tmp
sudo DESTDIR=/tmp/mesa-install-tmp meson install -C build/
sudo tar -zcvf mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz -C /tmp/mesa-install-tmp .
sudo chown $USER:$USER mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz
chmod 644 mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz
sudo rm -rf /tmp/mesa-install-tmp
```
## 基準測試

詳細的測試結果： [benchmark-result](../common/benchmark-result.md)  

| 裝置 | 型號 | SoC | GPU | glmark2 | glmark2-es2 | vkmark |
| :--------------: | :--------: | :-----------: | :--------: | ------: | ----------: | -----: |
| Redmi K40 Pro | M2012K11G | 高通驍龍 888 | Adreno 660 | 842 | 771 | 1170 |
| Xiaomi Pad 6 Pro | 23046RP50C | 高通驍龍 8+ Gen 1 | Adreno 730 | 1169 | 1143 | 無法運作 |
| Xiaomi 17 Pro Max | 2509FPN0BC | 高通驍龍 8 Elite Gen 5 | Adreno 840 | 2947 | 2901 | 無法運作 |
## 感謝
  - [Lucas Fryzek](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21570)：Mesa Freedreno 驅動程式的 KGSL 後端程式碼的作者。  
  - [xMeM](https://github.com/xMeM/termux-packages/commit/401982b8d9eaef70669762bfff2a963341c65e52)：將 Freedreno 驅動程式的 KGSL 後端移植至 Termux:X11。  
  - [Robert Kirkman](https://github.com/robertkirkman/termux-packages/commit/06a959eeddf153cebd0d3ea1a6c2eb2921b5f786)：整合並完善了 xMeM 的修補程式。  
  - [Rob Clark](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/38450)：為 Adreno Gen8 架構（包含 Adreno 840）引入了初步的 Freedreno 支援。
