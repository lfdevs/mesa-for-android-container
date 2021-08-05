-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA512

Format: 3.0 (quilt)
Source: libdrm
Binary: libdrm-dev, libdrm2, libdrm-common, libdrm-tests, libdrm2-udeb, libdrm-intel1, libdrm-nouveau2, libdrm-radeon1, libdrm-omap1, libdrm-freedreno1, libdrm-exynos1, libdrm-tegra0, libdrm-amdgpu1, libdrm-etnaviv1
Architecture: any all
Version: 2.4.105-3
Maintainer: Debian X Strike Force <debian-x@lists.debian.org>
Uploaders: Andreas Boll <aboll@debian.org>
Homepage: https://cgit.freedesktop.org/mesa/drm/
Standards-Version: 4.5.0
Vcs-Browser: https://salsa.debian.org/xorg-team/lib/libdrm
Vcs-Git: https://salsa.debian.org/xorg-team/lib/libdrm
Build-Depends: debhelper-compat (= 12), meson, quilt, xsltproc, libx11-dev, pkg-config, xutils-dev (>= 1:7.6+2), libudev-dev [linux-any], libpciaccess-dev, python3-docutils, valgrind [amd64 armhf i386 mips mipsel powerpc s390x]
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
 ac37d82d6f1f6673488f39f22b581536ada2e2d8 420376 libdrm_2.4.105.orig.tar.xz
 263183a23828e868c1010773c8b1f9ff064e0a56 59144 libdrm_2.4.105-3.debian.tar.xz
Checksums-Sha256:
 1d1d024b7cadc63e2b59cddaca94f78864940ab440843841113fbac6afaf2a46 420376 libdrm_2.4.105.orig.tar.xz
 24a145d0bd231c5ae74ceeb45a8fcfcadb309e7e08d2c5d190ae2aff7baac441 59144 libdrm_2.4.105-3.debian.tar.xz
Files:
 773fd2a78a93a83c477e588d85319b08 420376 libdrm_2.4.105.orig.tar.xz
 70d6260f8ce19ee3eff54ecf6563bd66 59144 libdrm_2.4.105-3.debian.tar.xz

-----BEGIN PGP SIGNATURE-----

iQIzBAEBCgAdFiEEdS3ifE3rFwGbS2Yjy3AxZaiJhNwFAmCW7AEACgkQy3AxZaiJ
hNyi/A/+Pxrf8osHDqrD0Du7Ni5IuoaF2zysId8z7+VIgJhb1kLU/4H/od1Vs7vB
pVooH8u6tchhD9LkrTbk/NAG8/KGZKyZ8FvUjPpCFmyLxwDTw84C/xYcQ87s+MpD
aTZq6OmuRek7BJtSo995XxT//56s+NI+QK1Y0E55S7noeMu3la/Ecr61tYdjD9cX
Ak18Wx5Lri2I7jcXvgrdHVowqVjmcLb7r6hfFO9BBWOiya6I3zKM1KN3aV7zFxFI
XB/2neOJAFYi21dBxO7YCPAAAsvDbyMwZVY4i2ecKmueH+d7lmPLmxwWngKY0iYH
RJxFIQ+w2gjLIBP4CTUdrhZRo3Y0Ki4l4XUhDA7Evu3zlljRY35lCQ2FWOTqSCmb
Oxjdomv7ULa+WGWSXro1FyJahCQMmivMjGU+pNgZDWscUdQmuR0G0nkop5pXVvkV
9g/GLn+I46QuPqYCARCYn5gnmguYW2Z3SVyBk0EXVFIAcQF+zYb1zhKQab+T4d7P
CMQz3Qtr7Gfxp13JMVlc30gWs25w0cxc2qTJ5W7NWwNC84v9h6eEFE8FHznMI/t/
8e459Casn8wapiE6t+JuXK2mIv7DbfDM7wxUDB32m8rf4fa4nBRH54wjuxn7NIiI
D0EdXj6OU3rbFM4pjqCMd8Wk2O0y5K2x7N6JudAzJwSmsE48oJI=
=YhMu
-----END PGP SIGNATURE-----
