.section .text

.global gdt_load
.type gdt_load, @function
gdt_load:
    lgdt (%rdi)
    pushq $0x08
    leaq .Lgdt_reloaded(%rip), %rax
    pushq %rax
    lretq
.Lgdt_reloaded:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x18, %ax
    ltr %ax
    ret
.size gdt_load, .-gdt_load

.macro ISR_NO_ERROR number
    .global isr\number
    .type isr\number, @function
isr\number:
    pushq $0
    pushq $\number
    jmp interrupt_common
    .size isr\number, .-isr\number
.endm

.macro ISR_ERROR number
    .global isr\number
    .type isr\number, @function
isr\number:
    pushq $\number
    jmp interrupt_common
    .size isr\number, .-isr\number
.endm

.macro IRQ_STUB number
    .global irq\number
    .type irq\number, @function
irq\number:
    pushq $0
    pushq $\number
    jmp interrupt_common
    .size irq\number, .-irq\number
.endm

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR    8
ISR_NO_ERROR 9
ISR_ERROR    10
ISR_ERROR    11
ISR_ERROR    12
ISR_ERROR    13
ISR_ERROR    14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR    17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_ERROR    20
ISR_ERROR    21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR    29
ISR_ERROR    30
ISR_NO_ERROR 31

IRQ_STUB 32
IRQ_STUB 33
IRQ_STUB 34
IRQ_STUB 35
IRQ_STUB 36
IRQ_STUB 37
IRQ_STUB 38
IRQ_STUB 39
IRQ_STUB 40
IRQ_STUB 41
IRQ_STUB 42
IRQ_STUB 43
IRQ_STUB 44
IRQ_STUB 45
IRQ_STUB 46
IRQ_STUB 47

.global interrupt_default_stub
.type interrupt_default_stub, @function
interrupt_default_stub:
    pushq $0
    pushq $255
    jmp interrupt_common
.size interrupt_default_stub, .-interrupt_default_stub

.type interrupt_common, @function
interrupt_common:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %r12
    movq %rsp, %rdi
    andq $-16, %rsp
    call interrupt_dispatch
    movq %r12, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq
.size interrupt_common, .-interrupt_common

.section .rodata
.align 8
.global exception_stub_table
exception_stub_table:
    .quad isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    .quad isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    .quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31

.global irq_stub_table
irq_stub_table:
    .quad irq32, irq33, irq34, irq35, irq36, irq37, irq38, irq39
    .quad irq40, irq41, irq42, irq43, irq44, irq45, irq46, irq47

.section .note.GNU-stack,"",@progbits
