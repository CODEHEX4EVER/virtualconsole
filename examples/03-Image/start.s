    .section .text
    .globl _start
_start:
    # Configura el stack pointer (sp)
    la sp, _stack_top

    # Llama a main (sin argumentos)
    call os_main

    # Si main retorna, entra en bucle infinito
1:  j 1b

    .section .bss
    .align 4
    .space 1024         # 1 KB de pila (ajusta según tus necesidades)
_stack_top:
