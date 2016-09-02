state-threads
=============

fork from http://sourceforge.net/projects/state-threads, nothing changed.<br/>
see: https://github.com/winlinvip/state-threads/blob/master/README

## Branch SRS

The branch [srs](https://github.com/ossrs/state-threads/tree/srs) patched the following patches and refined the TAB.

1. [st.arm.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/1.st.arm.patch), for ARM.
1. [st.osx.kqueue.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/3.st.osx.kqueue.patch), for osx.
1. [st.disable.examples.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/4.st.disable.examples.patch), for ubuntu.

## Usage

For linux:

```
make linux-debug EXTRA_CFLAGS="-DMD_HAVE_EPOLL"
```

For osx:

```
make darwin-debug EXTRA_CFLAGS="-DMD_HAVE_KQUEUE"
```

Winlin 2016
