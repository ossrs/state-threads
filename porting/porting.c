/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2021 Winlin */

#include <stdio.h>
#include <setjmp.h>

extern void print_buf(unsigned char* p, int nn_jb);
extern void print_jmpbuf();

int main(int argc, char** argv)
{
    printf("OS specs:\n");
#ifdef __linux__
    printf("__linux__: %d\n", __linux__);
#endif
#ifdef __APPLE__
    printf("__APPLE__: %d\n", __APPLE__);
#endif

    printf("\nCPU specs:\n");
#ifdef __mips__
    // https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00565-2B-MIPS32-QRC-01.01.pdf
    printf("__mips__: %d, __mips:%d, __mips_isa_rev:%d, _MIPSEL:%d\n", __mips__, __mips, __mips_isa_rev, _MIPSEL);
#endif
#ifdef __x86_64__
    printf("__x86_64__: %d\n", __x86_64__);
#endif

    printf("\nCompiler specs:\n");
#ifdef __GLIBC__
    printf("__GLIBC__: %d\n", __GLIBC__);
#endif
    printf("sizeof(long)=%d\n", (int)sizeof(long));
    printf("sizeof(long long int)=%d\n", (int)sizeof(long long int));
    printf("sizeof(void*)=%d\n", (int)sizeof(void*));
#ifdef __ptr_t
    printf("sizeof(__ptr_t)=%d\n", sizeof(__ptr_t));
#endif

    printf("\nCalling conventions:\n");
    print_jmpbuf();

    return 0;
}

#ifdef __linux__
#ifdef __mips__
void print_jmpbuf()
{
    // https://en.wikipedia.org/wiki/MIPS_architecture#Calling_conventions
    register long int gp asm("gp");
    register long int sp asm("sp");
    register long int fp asm("fp");
    // $s0–$s7	$16–$23	saved temporaries
    register long int s0 asm("s0");
    register long int s1 asm("s1");
    register long int s2 asm("s2");
    register long int s3 asm("s3");
    register long int s4 asm("s4");
    register long int s5 asm("s5");
    register long int s6 asm("s6");
    register long int s7 asm("s7");
    printf("gp=%p, fp=%p, sp=%p, s0=%p, s1=%p, s2=%p, s3=%p, s4=%p, s5=%p, s6=%p, s7=%p\n",
        (void*)gp, (void*)fp, (void*)sp, (void*)s0, (void*)s1, (void*)s2, (void*)s3, (void*)s4, (void*)s5, (void*)s6, (void*)s7);

    /*
    typedef unsigned long long __jmp_buf[13];
    typedef struct __jmp_buf_tag {
        __jmp_buf __jb;
        unsigned long __fl;
        unsigned long __ss[128/sizeof(long)];
    } jmp_buf[1];
    */
    jmp_buf ctx = {0};
    setjmp(ctx);

    int nn_jb = sizeof(ctx[0].__jb);
    printf("sizeof(jmp_buf)=%d (unsigned long long [%d])\n", nn_jb, nn_jb/8);

    unsigned char* p = (unsigned char*)ctx[0].__jb;
    print_buf(p, nn_jb);
}
#endif
#endif

#ifdef __APPLE__
#ifdef __x86_64__
void print_jmpbuf()
{
    // https://courses.cs.washington.edu/courses/cse378/10au/sections/Section1_recap.pdf
    register long int rsp asm("rsp");
    register long int rbx asm("rbx");
    register long int rbp asm("rbp");
    register long int r12 asm("r12");
    register long int r13 asm("r13");
    register long int r14 asm("r14");
    register long int r15 asm("r15");
    printf("rsp=%p, rbx=%p, rbp=%p, r12=%p, r13=%p, r14=%p, r15=%p\n",
        (void*)rbx, (void*)rbp, (void*)r12, (void*)r13, (void*)r14, (void*)r15);

    jmp_buf ctx = {0};
    setjmp(ctx);

    int nn_jb = sizeof(ctx);
    printf("sizeof(jmp_buf)=%d (unsigned long long [%d])\n", nn_jb, nn_jb/8);

    unsigned char* p = (unsigned char*)ctx;
    print_buf(p, nn_jb);
}
#endif
#endif

void print_buf(unsigned char* p, int nn_jb)
{
    printf("    ");

    for (int i = 0; i < nn_jb; i++) {
        printf("0x%02x ", (unsigned char)p[i]);

        int newline = ((i + 1) % sizeof(void*));
        if (!newline || i == nn_jb - 1) {
            printf("\n");
        }

        if (!newline && i < nn_jb - 1) {
            printf("    ");
        }
    }
}

