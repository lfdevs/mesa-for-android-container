# Mesa for Android Container
Forked From [Mesa - The 3D Graphics Library](https://gitlab.freedesktop.org/mesa/mesa)  

[English](../../README.md) | [简体中文](../zh-HanS/README.md) | **繁體中文**  

---  
一個適用於 Android 容器（Proot、Chroot、LXC 等）的 [Mesa](https://gitlab.freedesktop.org/mesa/mesa) 建置版本，支援 Adreno、Mali GPU 的硬體加速。  

## 特性
  - 直接使用修改後的 Mesa 官方儲存庫原始碼，在 Debian 13 的 Linux arm64 Chroot 容器下編譯，向下相容 Debian 12。以 Android 為宿主機的 Proot、Chroot、LXC 容器均可使用本專案編譯的 Mesa 驅動程式。  
  - 對於部分 Adreno 6xx/7xx GPU，OpenGL、OpenGL ES、Vulkan 均可使用 Freedreno 驅動程式，不再需要使用 Zink 進行圖形 API 呼叫轉換，極大地提高了 GPU 的利用效率。
  - 只編譯與絕大部分 Android 裝置相關的驅動程式，減小套件大小。  
## 相容性

| GPU | OpenGL | OpenGL ES | Vulkan |
| :------------: | :----: | :-------: | :----: |
| **Adreno GPU** | ✅支援 | ✅支援 | ✅支援 |
| **Mali GPU** | ❔未測試 | ❔未測試 | ❔未測試 |
## 安裝
ℹ️**注意**：目前本專案的「發行版本 (Releases)」僅提供 `.tar.gz` 格式的安裝包，只能用來覆蓋原有的 Mesa 驅動程式，無法直接解除安裝，僅供測試使用。  

1.  前往 [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) 下載一個安裝包。請注意檔案名稱中的 Linux 發行版後綴，如 `debian_arm64`，只能安裝發行版相符的安裝包。  
2.  直接將安裝包解壓縮到根目錄。  
```shell
sudo tar -zxvf mesa-for-android-container_25.3.0-devel- 20250725_debian_arm64.tar.gz -C /
```
3.  更新動態連結器快取。  
```shell
sudo ldconfig
```
## 建置

本專案在 Debian 13 arm64 環境下建置，詳細的建置流程請參照 Mesa 官方說明（[Compilation and Installation Using Meson — The Mesa 3D Graphics Library latest documentation](https://docs.mesa3d.org/meson.html)），以下為關鍵步驟：  

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
5.  初始化建置目錄。  
```shell
meson setup build/
```
6.  修改建置選項。可以參照 [meson.options](https://github.com/lfdevs/mesa-for-android-container/blob/main/meson.options) 檔案來查看所有的建置選項。  
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
7.  開始建置。  
```shell
ninja -C build/
```
8.  若要直接在編譯裝置上安裝，可執行以下指令：  
```shell
ninja -C build/ install
```
9.  若需打包建置好的 Mesa 驅動程式，並在相同發行版的其他裝置上安裝，可參考以下指令：  
```shell
sudo mkdir /tmp/mesa-install-tmp
sudo DESTDIR=/tmp/mesa-install-tmp meson install -C build/
sudo tar -zcvf mesa-for-android-container_25.3.0-devel-20250725_debian_arm64.tar.gz -C /tmp/mesa-install-tmp .
sudo chown $USER:$USER mesa-for-android-container_25.3.0-devel-20250725_debian_arm64.tar.gz
chmod 644 mesa-for-android-container_25.3.0-devel-20250725_debian_arm64.tar.gz
sudo rm -rf /tmp/mesa-install-tmp
```
## 基準測試

詳細的測試結果： [benchmark-result](../common/benchmark-result.md)  

| 裝置 | 型號 | SoC | GPU | glmark2 | glmark2-es2 | vkmark |
| :--------------: | :--------: | :-----------: | :--------: | ------: | ----------: | -----: |
| Redmi K40 Pro | M2012K11G | 高通驍龍 888 | Adreno 660 | 842 | 771 | 1170 |
| Xiaomi Pad 6 Pro | 23046RP50C | 高通驍龍 8+ Gen 1 | Adreno 730 | 1169 | 1143 | 無法運作 |
## 感謝
  - [Lucas Fryzek](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21570)：Mesa Freedreno 驅動程式的 KGSL 後端程式碼的作者。  
  - [xMeM](https://github.com/xMeM/termux-packages/commit/401982b8d9eaef70669762bfff2a963341c65e52)：將 Freedreno 驅動程式的 KGSL 後端移植至 Termux:X11。  
  - [robertkirkman](https://github.com/robertkirkman/termux-packages/commit/06a959eeddf153cebd0d3ea1a6c2eb2921b5f786)：整合並完善了 xMeM 的修補程式。  