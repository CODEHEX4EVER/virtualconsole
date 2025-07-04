    .section .text
    .globl _start
_start:
    la sp, _stack_top

    call os_main

1:  j 1b

    .section .bss
    .align 4
    .space 1024         
_stack_top:
