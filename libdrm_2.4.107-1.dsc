-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA512

Format: 3.0 (quilt)
Source: libdrm
Binary: libdrm-dev, libdrm2, libdrm-common, libdrm-tests, libdrm2-udeb, libdrm-intel1, libdrm-nouveau2, libdrm-radeon1, libdrm-omap1, libdrm-freedreno1, libdrm-exynos1, libdrm-tegra0, libdrm-amdgpu1, libdrm-etnaviv1
Architecture: any all
Version: 2.4.107-1
Maintainer: Debian X Strike Force <debian-x@lists.debian.org>
Uploaders: Andreas Boll <aboll@debian.org>
Homepage: https://cgit.freedesktop.org/mesa/drm/
Standards-Version: 4.5.0
Vcs-Browser: https://salsa.debian.org/xorg-team/lib/libdrm
Vcs-Git: https://salsa.debian.org/xorg-team/lib/libdrm
Build-Depends: debhelper-compat (= 12), meson, quilt, xsltproc, libx11-dev, pkg-config, python3-setuptools, xutils-dev (>= 1:7.6+2), libudev-dev [linux-any], libpciaccess-dev, python3-docutils, valgrind [amd64 armhf i386 mips mipsel powerpc s390x]
Package-List:
 libdrm-amdgpu1 deb libs optional arch=linux-any,kfreebsd-any,hurd-any
 libdrm-common deb libs optional arch=all
 libdrm-dev deb libdevel optional arch=linux-any,kfreebsd-any,hurd-any
 libdrm-etnaviv1 deb libs optional arch=armhf,arm64
 libdrm-exynos1 deb libs optional arch=any-arm
 libdrm-freedreno1 deb libs optional arch=any-arm,arm64
 libdrm-intel1 deb libs optional arch=amd64,i386,kfreebsd-amd64,kfreebsd-i386,hurd-i386,x32
 libdrm-nouveau2 deb libs optional arch=linux-any
 libdrm-omap1 deb libs optional arch=any-arm
 libdrm-radeon1 deb libs optional arch=linux-any,kfreebsd-any,hurd-any
 libdrm-tegra0 deb libs optional arch=any-arm,arm64
 libdrm-tests deb libs optional arch=any
 libdrm2 deb libs optional arch=linux-any,kfreebsd-any,hurd-any
 libdrm2-udeb udeb debian-installer optional arch=linux-any,kfreebsd-any,hurd-any
Checksums-Sha1:
 372eb85849d1858a892dc5569edfa278640a9732 425612 libdrm_2.4.107.orig.tar.xz
 cbaad353d09223492abbc8f44687d77df1d8c08b 58672 libdrm_2.4.107-1.debian.tar.xz
Checksums-Sha256:
 c554cef03b033636a975543eab363cc19081cb464595d3da1ec129f87370f888 425612 libdrm_2.4.107.orig.tar.xz
 236f31ba827382f4c38dec9aabf9509c165e1f7226e6c7f2af0a0183cb38e36b 58672 libdrm_2.4.107-1.debian.tar.xz
Files:
 252175d363e3dbc4ffe32faaa8e93494 425612 libdrm_2.4.107.orig.tar.xz
 41f060cf719b668d875cfdfeeb2b6a0a 58672 libdrm_2.4.107-1.debian.tar.xz

-----BEGIN PGP SIGNATURE-----

iQIzBAEBCgAdFiEEdS3ifE3rFwGbS2Yjy3AxZaiJhNwFAmEKbEAACgkQy3AxZaiJ
hNzRyQ//Um/4YIc7aTSnA+AMTG78LSLT+X/w2czOgmA6R2rRWJhAT8mPdY5EabDT
fB34bM295lUPV0XTiLZyjsu6gkVzAxsrdyZHvmPJt/DTHCy40J1eu1EczXw8w8MC
j4YcoeJuGhmp7lyIBsL9gxZH6S7sNGtZTBqrdZLzlSKa3YjoKh1aF0bL0tDvDVb2
OR/kFgGNjlgtx8XRjFPMwMpLSqKxqyFaoQTl8rHX6d0yHWuEhuPn1gX6KtGjvPRH
8/qdTQW5RITPb6V4qKdKzSTI+G7kqWx8WTMaVCCHpbViFzH35oPtWzvlHN4f7zSv
GDqPug5wpS+IB2Q2vh8fmyHRP0y0KOwrujNi9txJcEmjNZBCX+xD0XdIHK3ibLXx
HshFwDBIuSExmk9YHNM9xaGqeoy5JEUcpmwWU8OpsRGyxPC3PpbQSdQi7lDRYYaz
P3jPE/l7dXP9Ypp/vB1ZfSEvIK4Xzw33/LHFohK09+eXQnpo3akaZMHstnOQnDxA
y7WYTLYvsU5f8c2ohpWvEviUdSSSljNd40P0mn34kwU+SB1NqugwK+mNKEUSqZWJ
qGYyym+O4qfSUF9cjAE2XofaSzVgSYXbRSt1jAzORiy40AQBE6tK+9fPhLA4kQPV
CMqMeO+CUTQzbLBHxJK+lj6Hu6I2lgP5ZJHyP4wZfYRMbPxkywg=
=aG9x
-----END PGP SIGNATURE-----
