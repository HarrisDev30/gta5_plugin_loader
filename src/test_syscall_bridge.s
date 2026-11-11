.global _start
_start:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8,  %r10
    mov %r9,  %r8
    mov 8(%rsp), %r9
    movabsq $0x1122334455667788, %r11
    jmp *%r11

