state-threads
=============

Fork from http://sourceforge.net/projects/state-threads, patched for [SRS](https://github.com/ossrs/srs/tree/2.0release).

> See: https://github.com/ossrs/state-threads/blob/srs/README

## Branch SRS

The branch [srs](https://github.com/ossrs/state-threads/tree/srs) will be patched the following patches:

- [x] Patch [st.arm.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/1.st.arm.patch), for ARM.
- [x] Patch [st.osx.kqueue.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/3.st.osx.kqueue.patch), for osx.
- [x] Patch [st.disable.examples.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/4.st.disable.examples.patch), for ubuntu.
- [x] [Refine TAB of code](https://github.com/ossrs/state-threads/compare/c2001d30ca58f55d72a6cc6b9b6c70391eaf14db...d2101b26988b0e0db0aabc53ddf452068c1e2cbc).
- [x] Merge from [michaeltalyansky](https://github.com/michaeltalyansky/state-threads) and [xzh3836598](https://github.com/ossrs/state-threads/commit/9a17dec8f9c2814d93761665df7c5575a4d2d8a3), support [ARM](https://github.com/ossrs/state-threads/issues/1).
- [x] Merge from [toffaletti](https://github.com/toffaletti/state-threads), support [valgrind](https://github.com/ossrs/state-threads/issues/2) for ST.

## Usage

For Linux:

```
make linux-debug EXTRA_CFLAGS="-DMD_HAVE_EPOLL"
```

For OSX:

```
make darwin-debug EXTRA_CFLAGS="-DMD_HAVE_KQUEUE"
```

Linux with valgrind:

```
make linux-debug EXTRA_CFLAGS="-DMD_VALGRIND"
```

> Remark: User must install valgrind, for instance, in centos6 `sudo yum install -y valgrind valgrind-devel`.

Linux with valgrind and epoll:

```
make linux-debug EXTRA_CFLAGS="-DMD_HAVE_EPOLL -DMD_VALGRIND"
```

For OSX, user must specifies the valgrind header files:

```
make darwin-debug EXTRA_CFLAGS="-DMD_VALGRIND -I/usr/local/include"
```

Winlin 2016
