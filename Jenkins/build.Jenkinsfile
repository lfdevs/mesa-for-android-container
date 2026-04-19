pipeline {
    agent { label 'linux-arm64' }

    parameters {
        string(
            name: 'TAG',
            defaultValue: 'merge/adreno-main',
            description: 'Branch or tag to build'
        )
        booleanParam(
            name: 'DRAFT_RELEASE',
            defaultValue: false,
            description: 'Draft a new release after build'
        )
    }

    options {
        timestamps()
        ansiColor('xterm')
    }

    environment {
        MESA_REPO_URL  = 'https://github.com/lfdevs/mesa-for-android-container.git'
        PKGBUILD_REPO_URL = 'https://github.com/lfdevs/archlinuxarm-PKGBUILDs.git'
        RELEASE_REPO   = 'lfdevs/mesa-for-android-container'
    }

    stages {

        // ════════════════════════════════════════════════════════════════════
        // Stage 1 – Prepare Source
        // Clone the mesa-for-android-container repo at the requested tag and
        // stash it as a tarball for all non-archlinux build stages.
        // ════════════════════════════════════════════════════════════════════
        stage('Prepare Source') {
            steps {
                script {
                    sh '''#!/bin/bash
                        set -euo pipefail
                        cd ~
                        git config --global user.email "jenkins@localhost"
                        git config --global user.name "Jenkins"
                        rm -rf mesa-for-android-container

                        git clone -b "$TAG" --depth 1 "$MESA_REPO_URL" mesa-for-android-container
                        tar -zcf mesa-source-prepared.tar.gz mesa-for-android-container
                        cp mesa-source-prepared.tar.gz "$WORKSPACE/"
                    '''

                }
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Stage 2 – Build (parallel)
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
                                sed -i "s/^Types: deb$/Types: deb deb-src/" /etc/apt/sources.list.d/debian.sources
                                apt update
                                apt build-dep -y mesa
                                apt install -y git ccache libxfixes-dev libarchive-dev
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

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.debian_trixie_env"
                                echo "Building Mesa..."
                                cd ~/mesa-for-android-container
                                meson setup build/ \
                                    --prefix=/usr \
                                    -Dplatforms=x11,wayland \
                                    -Dgallium-drivers=freedreno,zink,virgl,llvmpipe \
                                    -Dgallium-va=disabled \
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
                                tar -zcvf "mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz" \
                                    -C /tmp/mesa-install-tmp .
                                rm -rf /tmp/mesa-install-tmp
                                echo "Calculating sha256 checksums..."
                                find . -maxdepth 1 -name "*.tar.gz" -type f \
                                    -printf "%P\\0" | xargs -0 sha256sum | sort -k2 > sha256sums.txt
                                cat sha256sums.txt && echo
                                echo "Packaging completed successfully!"
                                cp mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz "$WORKSPACE/"
                                cp sha256sums.txt "$WORKSPACE/sha256sums_debian_trixie.txt"
                            '''

                            archiveArtifacts(
                                artifacts: 'mesa-for-android-container_*debian_trixie*.tar.gz, sha256sums_debian_trixie.txt',
                                fingerprint: true
                            )
                        }
                    }
                    post {
                        always { sh 'ccache --show-stats || true' }
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
                                sed -i "s/^Types: deb$/Types: deb deb-src/" /etc/apt/sources.list.d/ubuntu.sources
                                apt update
                                apt build-dep -y mesa
                                apt install -y git ccache libxfixes-dev libarchive-dev
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
                                    -Dgallium-drivers=freedreno,zink,virgl,llvmpipe \
                                    -Dgallium-va=disabled \
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
                                tar -zcvf "mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz" \
                                    -C /tmp/mesa-install-tmp .
                                chmod 644 "mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz"
                                rm -rf /tmp/mesa-install-tmp
                                echo "Calculating sha256 checksums..."
                                find . -maxdepth 1 -name "*.tar.gz" -type f \
                                    -printf "%P\\0" | xargs -0 sha256sum | sort -k2 > sha256sums.txt
                                cat sha256sums.txt && echo
                                echo "Packaging completed successfully!"
                                cp mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz "$WORKSPACE/"
                                cp sha256sums.txt "$WORKSPACE/sha256sums_ubuntu_noble.txt"
                            '''

                            archiveArtifacts(
                                artifacts: 'mesa-for-android-container_*ubuntu_noble*.tar.gz, sha256sums_ubuntu_noble.txt',
                                fingerprint: true
                            )
                        }
                    }
                    post {
                        always { sh 'ccache --show-stats || true' }
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
                                echo "assumeyes=True" >> /etc/dnf/dnf.conf
                                dnf install -y dnf-plugins-core git ccache libarchive-devel
                                dnf config-manager setopt fedora-source.enabled=1 updates-source.enabled=1
                                dnf makecache
                                dnf builddep -y mesa
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
                                    -Dgallium-drivers=freedreno,zink,virgl,llvmpipe \
                                    -Dgallium-va=disabled \
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
                                tar -zcvf "mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz" \
                                    -C /tmp/mesa-install-tmp .
                                rm -rf /tmp/mesa-install-tmp
                                echo "Calculating sha256 checksums..."
                                find . -maxdepth 1 -name "*.tar.gz" -type f \
                                    -printf "%P\\0" | xargs -0 sha256sum | sort -k2 > sha256sums.txt
                                cat sha256sums.txt && echo
                                echo "Packaging completed successfully!"
                                cp mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz "$WORKSPACE/"
                                cp sha256sums.txt "$WORKSPACE/sha256sums_fedora_43.txt"
                            '''

                            archiveArtifacts(
                                artifacts: 'mesa-for-android-container_*fedora_43*.tar.gz, sha256sums_fedora_43.txt',
                                fingerprint: true
                            )
                        }
                    }
                    post {
                        always { sh 'ccache --show-stats || true' }
                    }
                }

                // Arch Linux — no Prepare Source stash; clones archlinuxarm-PKGBUILDs directly
                stage('archlinux') {
                    agent {
                        docker {
                            image 'lfdevs/archlinuxarm:base-devel'
                            args  '--privileged --user root'
                            reuseNode true
                            label 'linux-arm64'
                        }
                    }
                    steps {
                        script {
                            sh '''#!/bin/bash
                                set -euo pipefail
                                pacman -Syyu --noconfirm
                                pacman -S --noconfirm sudo base-devel git ccache
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                useradd -m -s /bin/bash runner
                                echo "runner ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/runner
                                chmod 0440 /etc/sudoers.d/runner
                                mkdir -p /home/runner/.ccache
                                chown -R runner:runner /home/runner
                            '''

                            sh '''#!/bin/bash
                                chown -R runner:runner /home/runner/.ccache
                                sudo -u runner bash -c "
                                    export CCACHE_DIR=/home/runner/.ccache
                                    ccache --max-size=2G
                                    ccache --show-config
                                    ccache --zero-stats
                                    ccache --show-stats
                                "
                            '''

                            sh """
                                set -euo pipefail
                                sudo -u runner bash -c '
                                    cd /home/runner
                                    git config --global user.email "jenkins@localhost"
                                    git config --global user.name "Jenkins"
                                    tag="${params.TAG}"
                                    if [ "\$tag" = "merge/adreno-main" ] || [ "\$tag" = "adreno-main" ]; then
                                        git clone -b freedreno --depth 1 ${PKGBUILD_REPO_URL}
                                    else
                                        git clone -b "\$tag" --depth 1 ${PKGBUILD_REPO_URL}
                                    fi
                                '
                            """

                            sh '''#!/bin/bash
                                set -euo pipefail
                                cd /home/runner/archlinuxarm-PKGBUILDs/extra/mesa
                                version=$(awk -F= "/^pkgver=/{print \\$2; exit}" PKGBUILD)
                                build_date=$(TZ=Asia/Shanghai date +%Y%m%d)
                                echo "MESA_RELEASE_NAME_SUFFIX=${version}-devel-${build_date}_archlinux_arm64" \
                                    >> "$WORKSPACE/.archlinux_env"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                sudo -u runner bash -c "
                                    export CCACHE_DIR=/home/runner/.ccache
                                    echo 'Building Mesa...'
                                    cd /home/runner/archlinuxarm-PKGBUILDs/extra/mesa
                                    makepkg -s --noconfirm
                                    echo 'Mesa build completed successfully!'
                                "
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                cd /home/runner/archlinuxarm-PKGBUILDs/extra/mesa
                                for f in mesa-for-android-container*.pkg.tar.xz; do
                                    [[ -f "$f" ]] || continue
                                    newname="${f//:/-}"
                                    [[ "$f" != "$newname" ]] && mv -v "$f" "$newname"
                                done
                                source "$WORKSPACE/.archlinux_env"
                                echo "Packaging Mesa..."
                                tar -cvf "mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar" *.pkg.tar.xz
                                echo "Calculating sha256 checksums..."
                                find . -maxdepth 1 -name "*.tar" -type f \
                                    -printf "%P\\0" | xargs -0 sha256sum | sort -k2 > sha256sums.txt
                                cat sha256sums.txt && echo
                                echo "Packaging completed successfully!"
                                cp mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar "$WORKSPACE/"
                                cp sha256sums.txt "$WORKSPACE/sha256sums_archlinux.txt"
                            '''

                            archiveArtifacts(
                                artifacts: 'mesa-for-android-container_*archlinux*.tar, sha256sums_archlinux.txt',
                                fingerprint: true
                            )
                        }
                    }
                    post {
                        always {
                            sh 'sudo -u runner bash -c "export CCACHE_DIR=/home/runner/.ccache; ccache --show-stats" || true'
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
                                xbps-install -Syu
                                xbps-install -y bash base-devel git ccache meson ninja pkg-config python3 \
                                    flex bison \
                                    libdrm-devel \
                                    libX11-devel libxcb-devel libXext-devel libXfixes-devel libXrandr-devel \
                                    libXxf86vm-devel libxshmfence-devel \
                                    wayland-devel wayland-protocols \
                                    libglvnd-devel \
                                    zlib-devel expat-devel \
                                    elfutils-devel \
                                    llvm18-devel clang18 \
                                    libxml2-devel \
                                    libarchive-devel \
                                    python3-Mako python3-packaging python3-yaml python3-ply \
                                    libffi-devel \
                                    libzstd-devel \
                                    glslang
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
                                echo "MESA_RELEASE_NAME_SUFFIX=${version}-${build_date}_void_arm64" \
                                    >> "$WORKSPACE/.void_env"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.void_env"
                                echo "Building Mesa..."
                                cd ~/mesa-for-android-container
                                meson setup build/ \
                                    --prefix=/usr \
                                    -Dplatforms=x11,wayland \
                                    -Dgallium-drivers=freedreno,zink,virgl,llvmpipe \
                                    -Dgallium-va=disabled \
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
                                ninja -C build/
                                echo "Mesa build completed successfully!"
                            '''

                            sh '''#!/bin/bash
                                set -euo pipefail
                                source "$WORKSPACE/.void_env"
                                echo "Packaging Mesa..."
                                cd ~/mesa-for-android-container
                                mkdir /tmp/mesa-install-tmp
                                DESTDIR=/tmp/mesa-install-tmp meson install -C build/
                                tar -zcvf "mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz" \
                                    -C /tmp/mesa-install-tmp .
                                rm -rf /tmp/mesa-install-tmp
                                echo "Calculating sha256 checksums..."
                                find . -maxdepth 1 -name "*.tar.gz" -type f \
                                    -printf "%P\\0" | xargs -0 sha256sum | sort -k2 > sha256sums.txt
                                cat sha256sums.txt && echo
                                echo "Packaging completed successfully!"
                                cp mesa-for-android-container_${MESA_RELEASE_NAME_SUFFIX}.tar.gz "$WORKSPACE/"
                                cp sha256sums.txt "$WORKSPACE/sha256sums_void.txt"
                            '''

                            archiveArtifacts(
                                artifacts: 'mesa-for-android-container_*void*.tar.gz, sha256sums_void.txt',
                                fingerprint: true
                            )
                        }
                    }
                    post {
                        always { sh 'ccache --show-stats || true' }
                    }
                }

            } // end parallel
        } // end Build

        // ════════════════════════════════════════════════════════════════════
        // Stage 3 – Draft Release
        // ════════════════════════════════════════════════════════════════════
        stage('Draft Release') {
            when {
                expression { return params.DRAFT_RELEASE }
            }
            steps {
                script {
                    // Restore all build artifacts from parallel stages into this workspace

                    sh '''#!/bin/bash
                        set -euo pipefail

                        checksums=$(cat "$WORKSPACE"/sha256sums_*.txt 2>/dev/null \
                            | awk "NF>=2 {print \$1 \"  \" \$2}" | sort -k2,2 || true)

                        cat > release_body.md << 'MDEOF'
## Checksums
```plaintext
MDEOF
                        printf "%s\\n" "$checksums" >> release_body.md
                        printf "%s\\n" '```' >> release_body.md
                    '''

                    sh 'cat release_body.md'

                    withCredentials([string(credentialsId: 'github-cli-token', variable: 'GH_TOKEN')]) {
                        sh """#!/bin/bash
                            set -euo pipefail
                            tag="${params.TAG}"
                            title="\${tag#mesa-}"
                            gh release create "\$tag" \
                                --title "\$title" \
                                --notes-file release_body.md \
                                --draft \
                                --repo "\$RELEASE_REPO" \
                                \$WORKSPACE/mesa-for-android-container_*.tar.gz \
                                \$WORKSPACE/mesa-for-android-container_*.tar \
                                || gh release edit "\$tag" \
                                    --title "\$title" \
                                    --notes-file release_body.md \
                                    --draft \
                                    --repo "\$RELEASE_REPO"
                        """
                    }
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
