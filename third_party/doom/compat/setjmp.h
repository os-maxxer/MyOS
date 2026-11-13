#ifndef SOLIS_DOOM_SETJMP_H
#define SOLIS_DOOM_SETJMP_H

typedef unsigned long solis_jmp_buf[8];

int setjmp(solis_jmp_buf buffer);
void longjmp(solis_jmp_buf buffer, int value) __attribute__((noreturn));

#endif
