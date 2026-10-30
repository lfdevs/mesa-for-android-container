# Development documentation

## Building

### GitHub Actions

This project supports building using [GitHub Actions](https://github.com/lfdevs/mesa-for-android-container/actions). You can fork this repository and then enable Actions in your fork.

#### Workflows

There are six workflows in this repository, briefly introduced below:

| File | Name | Applicable Branches | Build Target |
| :-: | :-: | - | - |
| `build.yml` | Build | `dev/adreno-main`, etc. | Latest standard installation packages |
| `build-turnip.yml` | Build Turnip | `turnip-main`, etc. | Latest unpatched Turnip drivers |
| `build-turnip-weekly.yml` | Build Turnip Weekly | Use upstream `main` branch and apply patches in the `patches/turnip-weekly` directory of the `ci` branch | Turnip Weekly Builds |
| `build-debian.yml` | Build Debian Packages | `adreno-debian-trixie`, `turnip-debian-trixie`, etc. | Debian installation packages installable via `apt` |
| `build-ubuntu.yml` | Build Ubuntu Packages | `adreno-ubuntu-noble-updates`, `turnip-ubuntu-noble-updates`, etc. | Ubuntu installation packages installable via `apt` |
| `build-fedora.yml` | Build Fedora Packages | `adreno-fedora-f43`, `turnip-fedora-f43`, etc. | Fedora installation packages installable via `dnf` |

**PS:** Due to the "rolling release" nature of Arch Linux, Arch Linux installation packages that can be installed using `pacman` are directly built by `build.yml` and `build-turnip.yml`.

#### Input Parameters

Workflows other than `build-turnip-weekly.yml` have two input parameters when manually triggered, briefly introduced as follows:

| Parameter | Name | Description |
| :-: | :-: | - |
| `tag` | Branch or tag | Build target, supports branches or tags under that branch |
| `draft_release` | Draft a new release | Automatically generate a draft release (applicable when the build target is a tag) |

The workflow `build-turnip-weekly.yml` has five optional input parameters when manually triggered, briefly introduced as follows:

| Parameter | Name | Description |
| :-: | :-: | - |
| `mesa_git_url` | Mesa git URL | Specify the URL of the Git repository to be cloned |
| `mesa_git_branch` | Mesa git branch | Specify the branch to be cloned |
| `mesa_hash` | Mesa commit hash | Specify the commit hash to be checked out |
| `skip_update_release` | Skip update-release | Do not perform a Release update |
| `skip_apply_patches` | Skip applying patches | Do not apply patches from the `patches/turnip-weekly` directory of the `ci` branch to the source code |

### Jenkins

This project supports CI building using Jenkins. The Jenkinsfiles are located in the `Jenkins` directory of the `ci` branch.

#### Requirements

To run these pipelines, your Jenkins environment needs the following:

- **Plugins**: In addition to the recommended plugins, you must install:
  - Docker (`docker-plugin`)
  - Docker Pipeline (`docker-workflow`)
  - AnsiColor (`ansicolor`)
- **Credentials**: If you want the pipeline to upload artifacts to GitHub Releases, you need to configure a "Secret text" credential with the ID `github-cli-token` containing a GitHub Personal Access Token (classic).
- **Agent Node**: The pipelines require an agent node with Linux Arm64 architecture (using the `linux-arm64` label). The agent must have JRE and Docker installed.

The pipeline stages and build parameters are very similar to the GitHub Actions workflows described above.

### Local Build

For detailed building procedures, please refer to the official Mesa documentation ([Compilation and Installation Using Meson — The Mesa 3D Graphics Library latest documentation](https://docs.mesa3d.org/meson.html)). Key commands for building installable packages compatible with package managers (e.g., `apt`, `dnf`, `pacman`) can be found in [this document](build-for-distros.md). Below are the key steps for building Mesa in a Debian 13 arm64 environment:

1. Check if the source code repositories are enabled. If you are using the traditional format for software sources (`/etc/apt/sources.list`), check for a configuration similar to the following.

   ```plaintext
   deb-src https://deb.debian.org/debian trixie main contrib non-free non-free-firmware
   ```

2. Install the dependencies required to build Mesa.

   ```bash
   sudo apt build-dep mesa
   ```

3. Clone this project's repository.

   ```bash
   git clone https://github.com/lfdevs/mesa-for-android-container.git
   ```

4. Switch to the tag where the relevant patches have been applied, such as `mesa-26.1.0-devel-20260404` (Freedreno KGSL) or `turnip-26.1.0-devel-20260404` (Turnip).

   ```bash
   cd mesa-for-android-container
   git checkout mesa-26.1.0-devel-20260404
   ```

5. Initialize the build directory. You can refer to the [meson.options](https://github.com/lfdevs/mesa-for-android-container/blob/main/meson.options) file to see all available build options.

   ```bash
   meson setup build/ \
      --prefix=/usr \
      -Dplatforms=x11,wayland \
      -Dgallium-drivers=freedreno,zink,virgl,llvmpipe \
      -Dgallium-va=enabled \
      -Dtermux-va-bridge=enabled \
      -Dtermux-va-wayland-shim=enabled \
      -Dvideo-codecs=all \
      -Dgallium-mediafoundation=disabled \
      -Dvulkan-drivers=freedreno \
      -Dvulkan-layers= \
      -Degl=enabled \
      -Dgles2=enabled \
      -Dglvnd=enabled \
      -Dglx=dri \
      -Dlibunwind=disabled \
      -Dintel-rt=disabled \
      -Dmicrosoft-clc=disabled \
      -Dvalgrind=disabled \
      -Dgles1=disabled \
      -Dfreedreno-kmds=kgsl \
      -Dbuildtype=release
   ```

   If you need to build the Turnip driver separately, you can use the following initialization command:

   ```bash
   meson setup build/ \
      --prefix=/usr \
      -Dplatforms=x11,wayland \
      -Dgallium-drivers= \
      -Dgallium-va=disabled \
      -Dgallium-mediafoundation=disabled \
      -Dvulkan-drivers=freedreno \
      -Dvulkan-layers= \
      -Dgles1=disabled \
      -Dgles2=disabled \
      -Dopengl=false \
      -Dgbm=disabled \
      -Dglx=disabled \
      -Dxlib-lease=disabled \
      -Dfreedreno-kmds=kgsl \
      -Degl=disabled \
      -Dglvnd=disabled \
      -Dintel-rt=disabled \
      -Dmicrosoft-clc=disabled \
      -Dllvm=disabled \
      -Dvalgrind=disabled \
      -Dbuild-tests=false \
      -Dlibunwind=disabled \
      -Dlmsensors=disabled \
      -Dandroid-libbacktrace=disabled \
      -Dbuildtype=release
   ```

6. Start the build.

   ```bash
   ninja -C build/
   ```

7. To install directly on the build device, run the following command:

   ```bash
   ninja -C build/ install
   ```

8. If you need to package the built Mesa drivers for installation on other devices with the same distribution, refer to the following commands:

   ```bash
   export MESA_RELEASE_NAME_SUFFIX=26.0.0-devel-xxxxxxxx_debian_trixie_arm64
   sudo mkdir /tmp/mesa-install-tmp
   sudo DESTDIR=/tmp/mesa-install-tmp meson install -C build/
   sudo tar -zcvf mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz -C /tmp/mesa-install-tmp .
   sudo chown $USER:$USER mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz
   chmod 644 mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz
   sudo rm -rf /tmp/mesa-install-tmp
   ```
