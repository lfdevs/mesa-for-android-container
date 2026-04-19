pipeline {
    agent { label 'linux-arm64' }

    triggers {
        // Every Monday at 10:18 UTC
        cron('18 10 * * 1')
    }

    parameters {
        string(
            name: 'MESA_HASH',
            defaultValue: '',
            description: 'Mesa commit hash (leave empty to use latest main)'
        )
        booleanParam(
            name: 'SKIP_UPDATE_RELEASE',
            defaultValue: false,
            description: 'Skip update-release stage'
        )
        booleanParam(
            name: 'SKIP_APPLY_PATCHES',
            defaultValue: false,
            description: 'Skip applying patches'
        )
    }

    options {
        timestamps()
        ansiColor('xterm')
    }

    environment {
        CI_REPO_URL    = 'https://github.com/lfdevs/mesa-for-android-container-ci.git'
        CI_REPO_BRANCH = 'ci'
        MESA_REPO_URL  = 'https://gitlab.freedesktop.org/mesa/mesa.git'
        PATCH_SUBDIR   = 'patches/turnip-weekly'
        RELEASE_REPO   = 'lfdevs/mesa-for-android-container'
    }

    stages {

        // ════════════════════════════════════════════════════════════════════
        // Stage 1 – Prepare Source
        // ════════════════════════════════════════════════════════════════════
        stage('Prepare Source') {
            steps {
                script {
                    // Resolve Mesa hash
                    def inputHash = params.MESA_HASH?.trim()
                    def resolvedHash

                    if (inputHash) {
                        echo "Using user-provided Mesa hash: ${inputHash}"
                        resolvedHash = inputHash
                    } else {
                        resolvedHash = sh(
                            script: '''
                                git ls-remote https://gitlab.freedesktop.org/mesa/mesa.git refs/heads/main \
                                    | awk '{print $1}'
                            ''',
                            returnStdout: true
                        ).trim()

                        if (!resolvedHash) {
                            error('Failed to resolve latest hash for main branch')
                        }
                        echo "Resolved latest Mesa main hash: ${resolvedHash}"
                    }

                    env.MESA_HASH = resolvedHash

                    // Prepare Mesa source
                    sh '''#!/bin/bash
                        set -euo pipefail

                        retry() {
                            local max_attempts="$1"
                            local delay_seconds="$2"
                            shift 2
                            local attempt=1
                            until "$@"; do
                                if [ "$attempt" -ge "$max_attempts" ]; then
                                    echo "Command failed after ${max_attempts} attempts: $*"
                                    return 1
                                fi
                                echo "Attempt ${attempt}/${max_attempts} failed: $*"
                                echo "Retrying in ${delay_seconds}s..."
                                sleep "$delay_seconds"
                                attempt=$((attempt + 1))
                            done
                        }

                        cd ~
                        git config --global user.email "jenkins@localhost"
                        git config --global user.name "Jenkins"
                        rm -rf mesa-for-android-container mesa-for-android-container-ci

                        retry 5 30 git clone -b main --depth 1 --single-branch --no-tags \
                            "$MESA_REPO_URL" mesa-for-android-container
                        cd mesa-for-android-container
                        retry 5 30 git fetch origin "$MESA_HASH"
                        git checkout --detach "$MESA_HASH"
                    '''

                    if (!params.SKIP_APPLY_PATCHES) {
                        sh '''#!/bin/bash
                            set -euo pipefail

                            retry() {
                                local max_attempts="$1"
                                local delay_seconds="$2"
                                shift 2
                                local attempt=1
                                until "$@"; do
                                    if [ "$attempt" -ge "$max_attempts" ]; then
                                        echo "Command failed after ${max_attempts} attempts: $*"
                                        return 1
                                    fi
                                    echo "Attempt ${attempt}/${max_attempts} failed: $*"
                                    echo "Retrying in ${delay_seconds}s..."
                                    sleep "$delay_seconds"
                                    attempt=$((attempt + 1))
                                done
                            }

                            cd ~
                            retry 5 30 git clone -b "$CI_REPO_BRANCH" --depth 1 --single-branch --no-tags \
                                "$CI_REPO_URL" mesa-for-android-container-ci

                            patch_dir=~/mesa-for-android-container-ci/$PATCH_SUBDIR
                            if [ ! -d "$patch_dir" ]; then
                                echo "Patch directory not found: $patch_dir"
                                exit 1
                            fi

                            mapfile -t patches < <(find "$patch_dir" -maxdepth 1 -type f -name "*.patch" \
                                -printf "%f\\n" | sort)

                            if [ "${#patches[@]}" -eq 0 ]; then
                                echo "No patches found in $patch_dir"
                            else
                                cd ~/mesa-for-android-container
                                for patch in "${patches[@]}"; do
                                    echo "Applying $patch"
                                    git apply --index "$patch_dir/$patch"
                                done
                                if git diff --cached --quiet; then
                                    echo "No staged patch changes after apply"
                                else
                                    git commit -m "Apply turnip-weekly patches"
                                fi
                            fi
                        '''
                    } else {
                        echo 'Skipping patch application as requested by parameter'
                    }

                    // Archive & stash prepared source
                    sh '''#!/bin/bash
                        set -euo pipefail
                        cd ~
                        tar -zcf mesa-source-prepared.tar.gz mesa-for-android-container
                        cp mesa-source-prepared.tar.gz "$WORKSPACE/"
                    '''

                }
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Stage 2 – Build (parallel across all distros)
        // ════════════════════════════════════════════════════════════════════
        stage('Build') {
            parallel {

                // Debian Trixie
                stage('debian-trixie') {
                    agent {
                        docker {
                            image 'debian:trixie-slim'
                            args  '--privileged --user root'
                            reuseNode true
                            label 'linux-arm64'
                        }
                    }
                    environment {
                        CCACHE_DIR = '/root/.ccache'
                    }
                    steps {
                        script {

                            sh '''#!/bin/bash
                                set -euo pipefail
                                export DEBIAN_FRONTEND=noninteractive
                                retry() {
                                    local max_attempts="$1"
                                    local delay_seconds="$2"
                                    shift 2
                                    local attempt=1
                                    until "$@"; do
                                        if [ "$attempt" -ge "$max_attempts" ]; then
                                            echo "Command failed after ${max_attempts} attempts: $*"
                                            return 1
                                        fi
                                        echo "Attempt ${attempt}/${max_attempts} failed: $*"
                                        echo "Retrying in ${delay_seconds}s..."
                                        sleep "$delay_seconds"
                                        attempt=$((attempt + 1))
                                    done
                                }

                                sed -i "s/^Types: deb$/Types: deb deb-src/" /etc/apt/sources.list.d/debian.sources
                                retry 5 30 apt update
                                retry 5 30 apt build-dep -y mesa
                                retry 5 30 apt install -y git ccache libxfixes-dev libarchive-dev
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                rm -rf ~/mesa-for-android-container
                                tar -zxf mesa-source-prepared.tar.gz -C ~
                            '''

                            sh '''#!/bin/bash
                                ccache --max-size=2G
                                ccache --show-config
                                ccache --zero-stats
                                ccache --show-stats
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                cd ~/mesa-for-android-container
                                version=$(grep -v "^[[:space:]]*$" VERSION | head -n1)
                                build_date=$(TZ=Asia/Shanghai date +%Y%m%d)
                                echo "MESA_RELEASE_NAME_SUFFIX=${version}-${build_date}_debian_trixie_arm64" \
                                    >> "$WORKSPACE/.debian_trixie_env"
                            '''

                            def envVars = readFile("${env.WORKSPACE}/.debian_trixie_env").trim()
                            envVars.split('\n').each { line ->
                                def parts = line.split('=', 2)
                                if (parts.size() == 2) env."${parts[0]}" = parts[1]
                            }

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.debian_trixie_env"
                                echo "Building Mesa..."
                                cd ~/mesa-for-android-container
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
                                ninja -C build/
                                echo "Mesa build completed successfully!"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.debian_trixie_env"
                                echo "Packaging Mesa..."
                                cd ~/mesa-for-android-container
                                mkdir /tmp/mesa-install-tmp
                                DESTDIR=/tmp/mesa-install-tmp meson install -C build/
                                tar -zcvf "turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz" \
                                    -C /tmp/mesa-install-tmp .
                                rm -rf /tmp/mesa-install-tmp
                                echo "Calculating sha256 checksums..."
                                find . -maxdepth 1 -name "*.tar.gz" -type f \
                                    -printf "%P\\0" | xargs -0 sha256sum | sort -k2 > sha256sums.txt
                                cat sha256sums.txt && echo
                                echo "Packaging completed successfully!"
                                cp turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz "$WORKSPACE/"
                                cp sha256sums.txt "$WORKSPACE/sha256sums_debian_trixie.txt"
                            '''

                            archiveArtifacts(
                                artifacts: 'turnip-weekly_*debian_trixie*.tar.gz, sha256sums_debian_trixie.txt',
                                fingerprint: true
                            )
                        }
                    }
                    post {
                        always {
                            sh 'ccache --show-stats || true'
                        }
                    }
                }

                // Ubuntu Noble
                stage('ubuntu-noble') {
                    agent {
                        docker {
                            image 'ubuntu:noble'
                            args  '--privileged --user root'
                            reuseNode true
                            label 'linux-arm64'
                        }
                    }
                    environment {
                        CCACHE_DIR = '/root/.ccache'
                    }
                    steps {
                        script {

                            sh '''#!/bin/bash
                                set -euo pipefail
                                export DEBIAN_FRONTEND=noninteractive
                                retry() {
                                    local max_attempts="$1"
                                    local delay_seconds="$2"
                                    shift 2
                                    local attempt=1
                                    until "$@"; do
                                        if [ "$attempt" -ge "$max_attempts" ]; then
                                            echo "Command failed after ${max_attempts} attempts: $*"
                                            return 1
                                        fi
                                        echo "Attempt ${attempt}/${max_attempts} failed: $*"
                                        echo "Retrying in ${delay_seconds}s..."
                                        sleep "$delay_seconds"
                                        attempt=$((attempt + 1))
                                    done
                                }

                                sed -i "s/^Types: deb$/Types: deb deb-src/" /etc/apt/sources.list.d/ubuntu.sources
                                retry 5 30 apt update
                                retry 5 30 apt build-dep -y mesa
                                retry 5 30 apt install -y git ccache libxfixes-dev libarchive-dev
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                rm -rf ~/mesa-for-android-container
                                tar -zxf mesa-source-prepared.tar.gz -C ~
                            '''

                            sh '''#!/bin/bash
                                ccache --max-size=2G
                                ccache --show-config
                                ccache --zero-stats
                                ccache --show-stats
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                cd ~/mesa-for-android-container
                                version=$(grep -v "^[[:space:]]*$" VERSION | head -n1)
                                build_date=$(TZ=Asia/Shanghai date +%Y%m%d)
                                echo "MESA_RELEASE_NAME_SUFFIX=${version}-${build_date}_ubuntu_noble_arm64" \
                                    >> "$WORKSPACE/.ubuntu_noble_env"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.ubuntu_noble_env"
                                export LLVM_CONFIG=/usr/lib/llvm-20/bin/llvm-config
                                echo "Building Mesa..."
                                cd ~/mesa-for-android-container
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
                                ninja -C build/
                                echo "Mesa build completed successfully!"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.ubuntu_noble_env"
                                echo "Packaging Mesa..."
                                cd ~/mesa-for-android-container
                                mkdir /tmp/mesa-install-tmp
                                DESTDIR=/tmp/mesa-install-tmp meson install -C build/
                                tar -zcvf "turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz" \
                                    -C /tmp/mesa-install-tmp .
                                chmod 644 "turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz"
                                rm -rf /tmp/mesa-install-tmp
                                echo "Calculating sha256 checksums..."
                                find . -maxdepth 1 -name "*.tar.gz" -type f \
                                    -printf "%P\\0" | xargs -0 sha256sum | sort -k2 > sha256sums.txt
                                cat sha256sums.txt && echo
                                echo "Packaging completed successfully!"
                                cp turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz "$WORKSPACE/"
                                cp sha256sums.txt "$WORKSPACE/sha256sums_ubuntu_noble.txt"
                            '''

                            archiveArtifacts(
                                artifacts: 'turnip-weekly_*ubuntu_noble*.tar.gz, sha256sums_ubuntu_noble.txt',
                                fingerprint: true
                            )
                        }
                    }
                    post {
                        always {
                            sh 'ccache --show-stats || true'
                        }
                    }
                }

                // Fedora 43
                stage('fedora-43') {
                    agent {
                        docker {
                            image 'fedora:43'
                            args  '--privileged --user root'
                            reuseNode true
                            label 'linux-arm64'
                        }
                    }
                    environment {
                        CCACHE_DIR = '/root/.ccache'
                    }
                    steps {
                        script {

                            sh '''#!/bin/bash
                                set -euo pipefail

                                retry() {
                                    local max_attempts="$1"
                                    local delay_seconds="$2"
                                    shift 2
                                    local attempt=1
                                    until "$@"; do
                                        if [ "$attempt" -ge "$max_attempts" ]; then
                                            echo "Command failed after ${max_attempts} attempts: $*"
                                            return 1
                                        fi
                                        echo "Attempt ${attempt}/${max_attempts} failed: $*"
                                        echo "Retrying in ${delay_seconds}s..."
                                        sleep "$delay_seconds"
                                        attempt=$((attempt + 1))
                                    done
                                }

                                echo "assumeyes=True" >> /etc/dnf/dnf.conf
                                retry 5 30 dnf install -y dnf-plugins-core git ccache libarchive-devel
                                dnf config-manager setopt fedora-source.enabled=1 updates-source.enabled=1
                                retry 5 30 dnf makecache
                                retry 5 30 dnf builddep -y mesa
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                rm -rf ~/mesa-for-android-container
                                tar -zxf mesa-source-prepared.tar.gz -C ~
                            '''

                            sh '''#!/bin/bash
                                ccache --max-size=2G
                                ccache --show-config
                                ccache --zero-stats
                                ccache --show-stats
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                cd ~/mesa-for-android-container
                                version=$(grep -v "^[[:space:]]*$" VERSION | head -n1)
                                build_date=$(TZ=Asia/Shanghai date +%Y%m%d)
                                echo "MESA_RELEASE_NAME_SUFFIX=${version}-${build_date}_fedora_43_arm64" \
                                    >> "$WORKSPACE/.fedora_43_env"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.fedora_43_env"
                                echo "Building Mesa..."
                                cd ~/mesa-for-android-container
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
                                ninja -C build/
                                echo "Mesa build completed successfully!"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.fedora_43_env"
                                echo "Packaging Mesa..."
                                cd ~/mesa-for-android-container
                                mkdir /tmp/mesa-install-tmp
                                DESTDIR=/tmp/mesa-install-tmp meson install -C build/
                                tar -zcvf "turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz" \
                                    -C /tmp/mesa-install-tmp .
                                rm -rf /tmp/mesa-install-tmp
                                echo "Calculating sha256 checksums..."
                                find . -maxdepth 1 -name "*.tar.gz" -type f \
                                    -printf "%P\\0" | xargs -0 sha256sum | sort -k2 > sha256sums.txt
                                cat sha256sums.txt && echo
                                echo "Packaging completed successfully!"
                                cp turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz "$WORKSPACE/"
                                cp sha256sums.txt "$WORKSPACE/sha256sums_fedora_43.txt"
                            '''

                            archiveArtifacts(
                                artifacts: 'turnip-weekly_*fedora_43*.tar.gz, sha256sums_fedora_43.txt',
                                fingerprint: true
                            )
                        }
                    }
                    post {
                        always {
                            sh 'ccache --show-stats || true'
                        }
                    }
                }

                // Arch Linux
                stage('archlinux') {
                    agent {
                        docker {
                            image 'lfdevs/archlinuxarm:base-devel'
                            args  '--privileged --user root'
                            reuseNode true
                            label 'linux-arm64'
                        }
                    }
                    environment {
                        CCACHE_DIR = '/root/.ccache'
                    }
                    steps {
                        script {

                            sh '''#!/bin/bash
                                set -euo pipefail

                                retry() {
                                    local max_attempts="$1"
                                    local delay_seconds="$2"
                                    shift 2
                                    local attempt=1
                                    until "$@"; do
                                        if [ "$attempt" -ge "$max_attempts" ]; then
                                            echo "Command failed after ${max_attempts} attempts: $*"
                                            return 1
                                        fi
                                        echo "Attempt ${attempt}/${max_attempts} failed: $*"
                                        echo "Retrying in ${delay_seconds}s..."
                                        sleep "$delay_seconds"
                                        attempt=$((attempt + 1))
                                    done
                                }

                                retry 5 30 pacman -Syyu --noconfirm
                                retry 5 30 pacman -S --noconfirm \
                                    base-devel git ccache clang directx-headers expat glibc \
                                    libdisplay-info libdrm libelf libgcc libglvnd libpng libstdc++ \
                                    libva libx11 libxcb libxext libxml2 libxrandr libxshmfence \
                                    libxxf86vm llvm llvm-libs lm_sensors rust spirv-llvm-translator \
                                    spirv-tools systemd-libs vulkan-icd-loader wayland \
                                    xcb-util-keysyms zlib zstd cbindgen clang cmake elfutils \
                                    glslang libclc meson python-mako python-packaging python-ply \
                                    python-pycparser python-yaml rust-bindgen wayland-protocols \
                                    xorgproto libsysprof-capture valgrind python-sphinx \
                                    python-sphinx-hawkmoth byacc flex wayland wayland-protocols
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                rm -rf ~/mesa-for-android-container
                                tar -zxf mesa-source-prepared.tar.gz -C ~
                            '''

                            sh '''#!/bin/bash
                                ccache --max-size=2G
                                ccache --show-config
                                ccache --zero-stats
                                ccache --show-stats
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                cd ~/mesa-for-android-container
                                version=$(grep -v "^[[:space:]]*$" VERSION | head -n1)
                                build_date=$(TZ=Asia/Shanghai date +%Y%m%d)
                                echo "MESA_RELEASE_NAME_SUFFIX=${version}-${build_date}_archlinux_arm64" \
                                    >> "$WORKSPACE/.archlinux_env"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.archlinux_env"
                                echo "Building Mesa..."
                                cd ~/mesa-for-android-container
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
                                ninja -C build/
                                echo "Mesa build completed successfully!"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.archlinux_env"
                                echo "Packaging Mesa..."
                                cd ~/mesa-for-android-container
                                mkdir /tmp/mesa-install-tmp
                                DESTDIR=/tmp/mesa-install-tmp meson install -C build/
                                tar -zcvf "turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz" \
                                    -C /tmp/mesa-install-tmp .
                                rm -rf /tmp/mesa-install-tmp
                                echo "Calculating sha256 checksums..."
                                find . -maxdepth 1 -name "*.tar.gz" -type f \
                                    -printf "%P\\0" | xargs -0 sha256sum | sort -k2 > sha256sums.txt
                                cat sha256sums.txt && echo
                                echo "Packaging completed successfully!"
                                cp turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz "$WORKSPACE/"
                                cp sha256sums.txt "$WORKSPACE/sha256sums_archlinux.txt"
                            '''

                            archiveArtifacts(
                                artifacts: 'turnip-weekly_*archlinux*.tar.gz, sha256sums_archlinux.txt',
                                fingerprint: true
                            )
                        }
                    }
                    post {
                        always {
                            sh 'ccache --show-stats || true'
                        }
                    }
                }

                // Void Linux
                stage('void-linux') {
                    agent {
                        docker {
                            image 'ghcr.io/void-linux/void-glibc-full:latest'
                            args  '--privileged --user root'
                            reuseNode true
                            label 'linux-arm64'
                        }
                    }
                    environment {
                        CCACHE_DIR = '/root/.ccache'
                    }
                    steps {
                        script {

                            sh '''#!/bin/sh
                                set -eu
                                
                                retry() {
                                    local max_attempts="$1"
                                    local delay_seconds="$2"
                                    shift 2
                                    local attempt=1
                                    until "$@"; do
                                        if [ "$attempt" -ge "$max_attempts" ]; then
                                            echo "Command failed after ${max_attempts} attempts: $*"
                                            return 1
                                        fi
                                        echo "Attempt ${attempt}/${max_attempts} failed: $*"
                                        echo "Retrying in ${delay_seconds}s..."
                                        sleep "$delay_seconds"
                                        attempt=$((attempt + 1))
                                    done
                                }

                                retry 5 30 xbps-install -Syu
                                retry 5 30 xbps-install -y \
                                    bash base-devel git ccache meson ninja pkg-config python3 \
                                    flex bison libdrm-devel libX11-devel libxcb-devel \
                                    libxshmfence-devel wayland-devel wayland-protocols zlib-devel \
                                    expat-devel llvm18-devel clang18 libxml2-devel \
                                    libarchive-devel python3-Mako python3-packaging \
                                    python3-yaml python3-ply libffi-devel libzstd-devel glslang
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                rm -rf ~/mesa-for-android-container
                                tar -zxf mesa-source-prepared.tar.gz -C ~
                            '''

                            sh '''#!/bin/bash
                                ccache --max-size=2G
                                ccache --zero-stats
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                cd ~/mesa-for-android-container
                                version=$(grep -v "^[[:space:]]*$" VERSION | head -n1)
                                build_date=$(TZ=Asia/Shanghai date +%Y%m%d)
                                echo "MESA_RELEASE_NAME_SUFFIX=${version}-${build_date}_void_arm64" \
                                    >> "$WORKSPACE/.void_linux_env"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.void_linux_env"
                                echo "Building Mesa..."
                                cd ~/mesa-for-android-container
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
                                ninja -C build/
                                echo "Mesa build completed successfully!"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.void_linux_env"
                                echo "Packaging Mesa..."
                                cd ~/mesa-for-android-container
                                mkdir /tmp/mesa-install-tmp
                                DESTDIR=/tmp/mesa-install-tmp meson install -C build/
                                tar -zcvf "turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz" \
                                    -C /tmp/mesa-install-tmp .
                                rm -rf /tmp/mesa-install-tmp
                                echo "Calculating sha256 checksums..."
                                find . -maxdepth 1 -name "*.tar.gz" -type f \
                                    -printf "%P\\0" | xargs -0 sha256sum | sort -k2 > sha256sums.txt
                                cat sha256sums.txt && echo
                                echo "Packaging completed successfully!"
                                cp turnip-weekly_${MESA_RELEASE_NAME_SUFFIX}.tar.gz "$WORKSPACE/"
                                cp sha256sums.txt "$WORKSPACE/sha256sums_void.txt"
                            '''

                            archiveArtifacts(
                                artifacts: 'turnip-weekly_*void*.tar.gz, sha256sums_void.txt',
                                fingerprint: true
                            )
                        }
                    }
                    post {
                        always {
                            sh 'ccache --show-stats || true'
                        }
                    }
                }

            } // end parallel
        } // end Build stage

        // ════════════════════════════════════════════════════════════════════
        // Stage 3 – Update Release
        // ════════════════════════════════════════════════════════════════════
        stage('Update Release') {
            when {
                expression { return !params.SKIP_UPDATE_RELEASE }
            }
            steps {
                script {
                    // Collect all sha256sums files archived by the build stages
                    // and merge them into a single release body.
                    sh '''#!/bin/bash
                        set -euo pipefail

                        # Gather checksums from all archived sha256sums_*.txt files in WORKSPACE
                        checksums=$(cat "$WORKSPACE"/sha256sums_*.txt 2>/dev/null \
                            | awk "NF>=2 {print \$1 \"  \" \$2}" | sort -k2,2 || true)

                        cat > release_body.md << 'MDEOF'
# Turnip Weekly Builds
## Important Notice
This release provides weekly automated builds of Turnip drivers. You can use them together with standard releases (whose title does not have the `turnip-` prefix), or you can use this release alone **(which usually has better compatibility)**. When used alone, you need to change the value of the environment variable `MESA_LOADER_DRIVER_OVERRIDE` from `kgsl` to `zink`. See [the README](https://github.com/lfdevs/mesa-for-android-container/blob/adreno-main/.github/README.md) for details.

## Installation Instructions

1.  Download an installation package from the Assets section. Please note the Linux distribution suffix in the filename, such as `debian_trixie_arm64`. **You can only install the package that matches your distribution.**
2.  Extract the installation package directly to the root directory.
```shell
sudo tar -zxvf turnip-weekly*.tar.gz -C /
```
3.  Refresh the dynamic linker cache.
```shell
sudo ldconfig
```

## Checksums
```plaintext
MDEOF
                        printf "%s\\n" "$checksums" >> release_body.md
                        printf "%s\\n" '```' >> release_body.md
                    '''

                    echo "Release body generated:"
                    sh 'cat release_body.md'

                    withCredentials([string(credentialsId: 'github-cli-token', variable: 'GH_TOKEN')]) {
                        sh '''#!/bin/bash
                            gh release edit turnip-weekly \
                                --notes-file release_body.md \
                                --prerelease \
                                --repo "$RELEASE_REPO"
                            gh release upload turnip-weekly \
                                $WORKSPACE/turnip-weekly*.tar.gz \
                                --repo "$RELEASE_REPO" \
                                --clobber
                        '''
                    }

                    echo "Update Release stage complete. " 
                }
            }
        }

    } // end stages

    post {
        always {
            echo "Pipeline finished with status: ${currentBuild.currentResult}"
        }
        success {
            echo "All stages completed successfully."
        }
        failure {
            echo "Pipeline failed. Check individual stage logs for details."
        }
    }
}
