# How a new coroutine starts on every CPU

This note explains how State Threads starts a new thread (coroutine) in a small assembly entry,
`_st_md_thread_start`, on every Linux and macOS CPU and on Windows x64, instead of returning a second time
from a context save. The code is in `md_linux.S`, `md_linux2.S`, `md_darwin.S`, `md_win64.asm`, the
`MD_INIT_THREAD_ENTRY` macros in `md.h`, and `st_thread_create` in `sched.c`. Only Cygwin64 keeps the old
start. [win64_coroutine.md](win64_coroutine.md) explains the jmpbuf, the context switch, and why the old
start is unsafe, using Windows x64, where the entry came first.

## The old start: save, then patch the SP

Before v1.9.3, Linux and macOS started a new thread like this, and Cygwin64 still does:

```c
if (_st_md_cxt_save(thread->context)) {  /* 1. save the creator's registers */
    _st_thread_main();                   /* 3. the new thread starts here */
}
MD_GET_SP(thread) = stack->sp;           /* 2. point only the SP at the new stack */
```

The first restore of the jmpbuf jumps back into `st_thread_create`, the save returns a second time, and the
code calls `_st_thread_main`, now on the new stack, where `st_thread_create` was never called. This works
only while the compiler emits nothing there but "if 1, call `_st_thread_main`", which the C language does
not promise. The new thread also inherits its creator's callee-saved registers, and its outermost frame is a
frame of `st_thread_create` that was never called, so a stack walk has no clean end.

## The new start: an assembly entry

Every platform but Cygwin64 defines `MD_INIT_THREAD_ENTRY` in `md.h`, and `st_thread_create` starts a new
thread like this:

```c
_st_md_cxt_save(thread->context);  /* 1. fill the jmpbuf as a template */
MD_GET_SP(thread) = stack->sp;     /* 2. point the SP at the new stack */
MD_INIT_THREAD_ENTRY(thread);      /* 3. align the SP, start in _st_md_thread_start, null frame pointer */
```

The save still runs, but only to fill the jmpbuf with valid values, such as the FPU control words, or `$gp`
on MIPS. The new thread never resumes at it: the first restore of the jmpbuf jumps to `_st_md_thread_start`,
a small function written in assembly, on the new stack, so no compiler decides what runs first there. The
entry calls `_st_thread_main`, which runs the thread function and then `st_thread_exit`, so it never
returns; a trap instruction after the call says so.

## The entry on each CPU

The entry has the same shape everywhere. `MD_INIT_THREAD_ENTRY` aligns the saved SP as the ABI wants at a
function entry, sets the saved PC to `_st_md_thread_start`, and nulls the saved frame pointer. The entry
nulls the frame pointer and the return address register again, marks the return address undefined for
unwinders, and calls `_st_thread_main`.

| Platform | File | SP at the entry | Return address | Nulled | Unwind mark |
| --- | --- | --- | --- | --- | --- |
| Windows x64 | `md_win64.asm` | 8 mod 16 | 0 on the stack | `rbp` | `PROC FRAME`, `.allocstack 40` |
| Linux and macOS x86_64 | `md_linux.S`, `md_darwin.S` | 8 mod 16 | 0 on the stack | `rbp` | `.cfi_undefined rip` |
| Linux i386 | `md_linux.S` | 12 mod 16 | 0 on the stack | `ebp` | `.cfi_undefined eip` |
| Linux and macOS aarch64 | `md_linux2.S`, `md_darwin.S` | 16-byte aligned | `x30` | `x29`, `x30` | `.cfi_undefined x30` |
| Linux arm | `md_linux2.S` | 8-byte aligned | `lr` | `fp`, `r7`, `lr` | `.cfi_undefined lr`, EHABI `.save {fp, lr}` |
| Linux riscv64 | `md_linux2.S` | 16-byte aligned | `ra` | `s0`, `ra` | `.cfi_undefined ra` |
| Linux loongarch64 | `md_linux2.S` | 16-byte aligned | `ra` | `fp`, `ra` | `.cfi_undefined 1` (`ra`) |
| Linux mips64, mips64el (n64) | `md_linux2.S` | 16-byte aligned | `$ra` | `$fp`, `$ra` | `.cfi_undefined $ra` |
| Linux mips, mipsel (o32) | `md_linux2.S` | 8-byte aligned | `$ra` | `$fp`, `$ra` | `.cfi_undefined $ra` |

- **x86:** a `call` pushes the return address, so a function starts with SP 8 (x86_64) or 4 (i386) below a
  16-byte boundary. The macro writes a null return address there, and the entry subtracts the rest to align
  SP to 16 at its own call. On i386 the entry also puts the GOT address in `ebx`, which a PIC call through
  the PLT needs.
- **RISC CPUs:** the restore jumps to the saved return address register, so that slot holds the entry, and
  the SP needs only to be aligned. The entry then sets the register to 0 before its call.
- **arm:** the entry is ARM code, and the restore's `bx lr` switches to it from Thumb C code. glibc
  `backtrace()` and C++ exceptions walk ARM EHABI tables, not DWARF, so the entry pushes two null words with
  `.save {fp, lr}`: the walk lists the entry, pops a return address of 0, and stops. GCC leaves unwind tables
  out of C code on arm and MIPS, so the Linux library builds with `-funwind-tables`.
- **MIPS:** PIC code computes `$gp` from `$t9`, which must hold the address of the function it enters, and
  the restore does not set it. So the entry finds its own address with `bal`, computes `$gp` from it, and
  calls `_st_thread_main` through `$t9`. On o32 it also reserves the 16-byte argument save area of the callee.
- **macOS:** `backtrace()` follows the frame pointer chain, so the null `rbp` or `x29` ends it.
- **Windows x64:** see [win64_coroutine.md](win64_coroutine.md) for the entry in detail, SEH, and the TIB
  stack bounds.

## Stack walks

On Linux, glibc `backtrace()` and C++ exceptions use the DWARF unwinder of libgcc (EHABI on arm), and the
undefined return address of the entry ends the walk there. macOS `backtrace()` stops at the null frame
pointer. So a backtrace in a thread function ends with `_st_thread_main` and `_st_md_thread_start`, and no
frame of the creator, such as `st_thread_create` or `main`, follows. C++ code on a thread stack normally
catches its exceptions inside the thread function.

## Tests

The tests check the entry on every CPU:

- `ThreadEntryTest` in `utest/st_utest_entry.cpp` reads the jmpbuf of a new thread before it runs: the PC,
  the alignment of the SP, the null frame pointer, and on x86 the null return address.
- `tools/backtrace` checks that a backtrace in a thread ends at a return address inside
  `_st_md_thread_start`.

The README tells how to run them on every CPU with QEMU, and on macOS x86_64 under Rosetta 2.

## Summary

| | Old start (Cygwin64 only) | Assembly entry (Linux, macOS, Windows x64) |
| --- | --- | --- |
| PC of a new thread | In `st_thread_create`, after the save | `_st_md_thread_start`, in assembly |
| SP of a new thread | The new stack top | The new stack top, aligned as at a function entry |
| First code on the new stack | The tail of `st_thread_create`, from the compiler | A few instructions written by hand |
| Depends on the optimizer | Yes, in theory | No |
| Frame pointer and return address | The creator's | Null |
| Stack walk at the top | Not defined | Ends at the entry |
