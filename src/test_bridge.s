.global _start
_start:
    cmp $4, %edi
    jne .done_success
    test %rsi, %rsi
    jz .done_err
    push %rdi
    push %rsi
    push %rcx
    mov %rsi, %rdi
    movabsq $0x1122334455667788, %rsi
    mov $202, %ecx
    rep movsb
    pop %rcx
    pop %rsi
    pop %rdi
.done_success:
    xor %eax, %eax
    ret
.done_err:
    mov $-1, %eax
    ret
