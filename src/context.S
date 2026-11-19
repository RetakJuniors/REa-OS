.text
.global task_context_switch
.type task_context_switch, @function
task_context_switch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, (%rdi)
    mov (%rsi), %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
.size task_context_switch, .-task_context_switch
.section .note.GNU-stack,"",@progbits
