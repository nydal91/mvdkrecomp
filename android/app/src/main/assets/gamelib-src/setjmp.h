// Free-standing setjmp.h for the on-device game code build (no sysroot there). The generated code
// sets up the host jmp_buf runtime/hle/setjmp.c hands it (rt_setjmp_slot) and the runtime longjmps
// back into it: the layout must be bionic's own (arm64: long[32], <setjmp.h> _JBLEN), and the
// functions resolve against bionic's libc.so.
#ifndef _SW_SHIM_SETJMP_H
#define _SW_SHIM_SETJMP_H

typedef long jmp_buf[32];
typedef long sigjmp_buf[32 + 1];

int setjmp(jmp_buf __env) __attribute__((returns_twice));
__attribute__((noreturn)) void longjmp(jmp_buf __env, int __value);
int _setjmp(jmp_buf __env) __attribute__((returns_twice));
__attribute__((noreturn)) void _longjmp(jmp_buf __env, int __value);
int sigsetjmp(sigjmp_buf __env, int __save_signal_mask) __attribute__((returns_twice));
__attribute__((noreturn)) void siglongjmp(sigjmp_buf __env, int __value);

#define setjmp(__env) setjmp(__env)

#endif
