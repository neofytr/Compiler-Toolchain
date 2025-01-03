section .bss
stack: resq 1024
section .data
mul_num: dq 100000000.0
floating_point: db "."

section .text
global _start

; VASM Library Functions are currently statically linked

print_num_rax:
    cmp rax, 0
    jnl is_non_negative
    neg rax
    mov r12, 0          ; 0 for negative
    jmp intermediate
is_non_negative:
    mov r12, 1          ; 1 for non-negative
intermediate:
    mov r14, 0
    cmp r11, 0          ; check if newline should be added
    jne skip_newline
    dec rsp
    mov byte [rsp], 10  ; add newline at end
    add r14, 1
skip_newline:
    mov r13, 10         ; divisor for decimal
div_loop:
    xor rdx, rdx        ; zero rdx before using the division instruction
    div r13
    add dl, 48          ; convert numeric digit to ASCII equivalent
    dec rsp
    mov byte [rsp], dl
    inc r14
    test rax, rax       ; test if rax is zero
    jnz div_loop
    test r12, r12       ; check if the number was negative
    jz handle_negative
    jmp write
handle_negative:
    dec rsp
    mov byte [rsp], 45  ; add '-' for negative number
    inc r14
write:
    mov rax, 1
    mov rdi, 1
    mov rsi, rsp
    mov rdx, r14
    syscall
    add rsp, r14        ; restore the stack pointer
    ret
print_f64:
    vmovsd xmm0, qword [r15]
    vcvttsd2si rax, xmm0 ; extract the truncated floating-point value into rax
    cvtsi2sd xmm1, rax 
    vsubsd xmm1, xmm0, xmm1 ; extract the fractional part of the floating point value into xmm1

    ; We will display the floating-point with a precision of 6

    vmulsd xmm1, xmm1, qword [mul_num]
    vcvtsd2si rbx, xmm1 ; rbx contains the fractional part

    cmp rbx, 0
    jge skip_neg_frac ; handle if the fractional part comes out to be negative by the virtue of the number being negative
    neg rbx
skip_neg_frac:
    mov r11, 1
    call print_num_rax

    mov rax, 1
    mov rdi, 1
    mov rsi, floating_point
    mov rdx, 1
    syscall

    mov rax, rbx
    mov r11, 0
    call print_num_rax
    
    ret
_start:
    mov r15, stack + 8192
L0:
    sub r15, 8
    mov QWORD [r15], 10

L1:
    sub r15, 8
    mov QWORD [r15], 10

L2:
    sub r15, 8
    mov QWORD [r15], 100

L3:
    mov rax, [r15]
    add r15, 8
    add [r15], rax

L4:
    mov r11, 0
mov rax, [r15]
    call print_num_rax

L5:
    sub r15, 8
    mov QWORD [r15], 1

L6:
    mov rax, 60
    mov rdi, [r15]
    syscall
