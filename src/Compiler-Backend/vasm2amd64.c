#define _VM_IMPLEMENTATION
#define _SV_IMPLEMENATION
#include "../non_nanboxed/virt_mach.h"
#include "../non_nanboxed/String_View.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <getopt.h>

#define alloc 0
#define free_vm 1
#define print_f64 2
#define print_s64 3
#define print_u64 4
#define dump_static 5
#define print_string 6
#define read 7
#define write 8

#ifndef PATH_TO_NATIVE
#define PATH_TO_NATIVE "/home/raj/Desktop/VirtualMachine/src/Compiler-Backend/NativeFunctionImplementations/native_print.asm"
#endif

size_t call_no = 0;
size_t current_inst_num = 0;
bool target_is_num = false;

// the swap instruction basically converts the top of the VM stack into an implicit register; we can bring any value in the stack to the top of the
// stack (the implicit register) using swap and work on it and put it right back into it's original place using swap again.

// the thing is, i did not put any fucking checks on instruction operands that help access some VM stack memory to detect stack overflow/underflows and all
// like i did when a .vasm file converts to VM bytecode and runs on the VM; this prolly doesn't matter since this is how actual assembly is

// so, the halt instruction is required, otherwise the code will give seg fault since exit system call is needed to return control back to the
// OS after there are no instructions left to execute (seg fault occurs since memory beyond the last instruction is accessed when the program does not exit)
// also, the halt instruction returns the control to the OS with the exit code being the stack top; so there should be some stack top
// before the halt instruction is called; if there is nothing in the VM stack when halt is called, a seg fault will result

// jmp/call instructions in vasm should be used with labels only when converting directly to x86-64 code; they couldve been used with hardcoded unsigned integers that represented the actual instruction number
// in the source file when these vasm files are run on the vm, but when they are converted directly into x86-64 code, there is a difference; usage with labels is fine
// and i think i will implement the unsigned integer one now since the jump is directly to a specific instruction number in the vasm code but that will require me to
// have a label everytime a new vasm instruction begins in the asm code since x86-64 encoding size scheme is a fucking hot garbage mess that i don't give two shits about run; this will just
// fucking ruin the asm code with labels everywhere; should i do it or not wtf

// there is no bounds checking for pushing and popping off the stack; doing so out of bounds will just return a seg fault from the OS

#define TYPE_INVALID ((uint8_t)10)
#define ERROR_BUFFER_SIZE 256

#define PROG_TEXT(inst) fprintf(ctx->program_file, "    "##inst "\n")
#define PROG_DATA(inst) fprintf(ctx->data_file, "   "##inst "\n")

typedef struct label_hashnode_
{
    String_View label;
    struct label_hashnode_ *next;
} label_hashnode;

typedef struct
{
    String_View label;
    size_t line_no;
    bool target_is_num_;
} unresolved_label_;

typedef struct
{
    FILE *program_file;
    FILE *data_file;
    size_t code_section_offset;
    size_t data_section_offset;
    bool is_code;
    bool is_data;
    bool compilation_successful;
    char error_buffer[ERROR_BUFFER_SIZE];
    size_t line_no;
    size_t l_num;
    unresolved_label_ *unresolved_labels;
    size_t unresolved_labels_counter;
} CompilerContext;

label_hashnode *label_table[MAX_HASHTABLE_SIZE] = {NULL};

// function prototypes
bool init_compiler_context(CompilerContext *ctx, const char *output_file);
void cleanup_compiler_context(CompilerContext *ctx);
label_hashnode *search_in_label_table(String_View label);
bool push_to_label_table(String_View label, CompilerContext *ctx);
uint8_t check_operand_type(String_View *operand);
bool handle_instruction(CompilerContext *ctx, size_t inst_number, String_View *operand);
bool handle_code_line(CompilerContext *ctx, String_View *line);
bool handle_data_line(CompilerContext *ctx, String_View *line);
bool process_source_file(CompilerContext *ctx, const char *input_file);

// initialize compiler context
bool init_compiler_context(CompilerContext *ctx, const char *output_file)
{
    ctx->program_file = fopen("temp.asm", "w+");
    if (!ctx->program_file)
    {
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE, "Failed to open temporary program file: %s", strerror(errno));
        return false;
    }

    ctx->data_file = fopen(output_file, "w+");
    if (!ctx->data_file)
    {
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE, "Failed to open output file: %s", strerror(errno));
        fclose(ctx->program_file);
        return false;
    }

    fprintf(ctx->data_file, "section .bss\nstack: resq %zu\nsection .data\nmul_num: dq 100000000.0\nfloating_point: db \".\"\n", vm_stack_capacity);

    fprintf(ctx->program_file, "section .text\nglobal _start\n\n");
    fprintf(ctx->program_file, "; VASM Library Functions are currently statically linked\n\n");

    // VASM Library functions are linked statically, i.e, they are implemented (resolved) directly into the assembly file

    FILE *native_print = fopen(PATH_TO_NATIVE, "r");
    if (native_print == NULL)
    {
        perror("Error retrieving native print implementations: ");
        fprintf(stderr, "Set the path to native implementations while invoking the compiler as:\n -DPATH_TO_NATIVE=/your/path\n");
        exit(EXIT_FAILURE); // Exit if there's an error opening the source file
    }

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), native_print) != NULL)
    {
        if (fputs(buffer, ctx->program_file) == EOF)
        {
            perror("Error writing native print implementations to the output assembly file: ");
            fclose(native_print);
            exit(EXIT_FAILURE); // Exit on write error
        }
    }

    fclose(native_print);

    fprintf(ctx->program_file, "\n");

    // the call instruction places the return address on the stack itself, so the called function must ensure that the stack it uses is cleaned up before it returns using ret

    ctx->code_section_offset = 0;
    ctx->data_section_offset = 0;
    ctx->is_code = true;
    ctx->is_data = false;
    ctx->compilation_successful = true;
    ctx->line_no = 0;
    ctx->l_num = 0;
    ctx->unresolved_labels_counter = 0;

    ctx->unresolved_labels = malloc(sizeof(unresolved_label_) * label_capacity);
    if (!ctx->unresolved_labels)
    {
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE, "Failed to allocate memory for unresolved labels");
        fclose(ctx->program_file);
        fclose(ctx->data_file);
        return false;
    }

    return true;
}

void cleanup_compiler_context(CompilerContext *ctx)
{
    if (ctx->program_file)
        fclose(ctx->program_file);
    if (ctx->data_file)
        fclose(ctx->data_file);
    free(ctx->unresolved_labels);
}

label_hashnode *search_in_label_table(String_View label)
{
    uint32_t key = hash_sv(label) % MAX_HASHTABLE_SIZE;
    label_hashnode *current_node = label_table[key];

    while (current_node != NULL)
    {
        if (sv_eq(current_node->label, label))
        {
            return current_node;
        }
        current_node = current_node->next;
    }

    return NULL;
}

bool push_to_label_table(String_View label, CompilerContext *ctx)
{
    if (search_in_label_table(label))
    {
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                 "Line Number %zu -> ERROR: Label '%.*s' redefinition",
                 ctx->line_no, (int)label.count, label.data);
        ctx->compilation_successful = false;
        return false;
    }

    uint32_t key = hash_sv(label) % MAX_HASHTABLE_SIZE;
    label_hashnode *node = malloc(sizeof(label_hashnode));
    if (!node)
    {
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                 "ERROR: Memory allocation failed for label '%.*s'",
                 (int)label.count, label.data);
        return false;
    }

    node->label = label;
    node->next = label_table[key];
    label_table[key] = node;
    return true;
}

uint8_t check_operand_type(String_View *operand)
{
    bool point_found = false;
    bool is_negative = false;

    for (size_t i = 0; i < operand->count; i++)
    {
        char data = operand->data[i];

        if (i == 0 && (data == '-' || data == '+'))
        {
            is_negative = (data == '-');
            continue;
        }

        if (data == '.' && !point_found)
        {
            point_found = true;
            continue;
        }

        if (data < '0' || data > '9')
        {
            return TYPE_INVALID;
        }
    }

    if (point_found)
        return TYPE_DOUBLE;
    if (is_negative)
        return TYPE_SIGNED_64INT;
    return TYPE_UNSIGNED_64INT;
}

bool handle_instruction(CompilerContext *ctx, size_t inst_number, String_View *operand)
{
    fprintf(ctx->program_file, "L%zu:\n", current_inst_num++);

    switch (inst_number)
    {
    case INST_UPUSH:
    case INST_SPUSH:
        fprintf(ctx->program_file, "    sub r15, 8\n" // r15 is my personal stack pointer
                                   "    mov QWORD [r15], %.*s\n\n",
                (int)operand->count,
                operand->data);
        break;
    case INST_FPUSH:
        fprintf(ctx->data_file, "L%zu: dq %.*s\n", ctx->l_num,
                (int)operand->count, operand->data);
        fprintf(ctx->program_file, "    sub r15, 8\n"
                                   "    vmovsd xmm0, [L%zu]\n"
                                   "    vmovsd [r15], xmm0\n\n",
                ctx->l_num);
        ctx->l_num++;
        break;
    case INST_HALT:
        fprintf(ctx->program_file, "    mov rax, 60\n"
                                   "    mov rdi, [r15]\n" // the current program exit code is the value at the top of the VM stack
                                   "    syscall\n");
        break;
    case INST_SPLUS:
    case INST_UPLUS:
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    add [r15], rax\n\n"); // stack is the pointer to the stack; stack_top is the pointer to the address of stack top
        break;
    case INST_FPLUS:
        fprintf(ctx->program_file, "    vmovsd xmm0, [r15]\n"
                                   "    add r15, 8\n"
                                   "    vaddsd xmm0, [r15]\n"
                                   "    vmovsd [r15], xmm0\n\n");
        break;

    case INST_SMINUS:
    case INST_UMINUS:
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    sub [r15], rax\n\n");
        break;

    case INST_FMINUS:
        fprintf(ctx->program_file, "    vmovsd xmm0, [r15 + 8]\n"
                                   "    vsubsd xmm0, [r15]\n"
                                   "    add r15, 8\n"
                                   "    vmovsd [r15], xmm0\n");
        break;

    case INST_SMULT:
    case INST_UMULT:
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    imul rax, [r15]\n"
                                   "    mov qword [r15], rax\n\n");
        break;

    case INST_FMULT:
        fprintf(ctx->program_file, "    movsd xmm0, [r15 + 8]\n"
                                   "    vmulsd xmm0 , xmm0, [r15]\n"
                                   "    add r15, 8\n"
                                   "    vmovsd [r15], xmm0\n\n");
        break;

    case INST_SDIV:
    case INST_UDIV:
        fprintf(ctx->program_file, "    mov rax, [r15 + 8]\n"
                                   "    xor edx, edx\n"
                                   "    idiv [r15]\n"
                                   "    add r15, 8\n"
                                   "    mov qword [r15], rax\n\n");
        break;

    case INST_FDIV:
        fprintf(ctx->program_file, "    movsd xmm0, [r15 + 8]\n"
                                   "    vdivsd xmm0 , xmm0, [r15]\n"
                                   "    add r15, 8\n"
                                   "    vmovsd [r15], xmm0\n\n");
        break;

    case INST_NATIVE:
    {
        uint8_t lib_function_no = sv_to_unsigned64(operand);
        switch (lib_function_no)
        {
        case alloc:
            fprintf(ctx->program_file, "    call alloc\n\n");
            break;
        case free_vm:
            fprintf(ctx->program_file, "    call free\n\n");
            break;
        case print_f64:
            fprintf(ctx->program_file, "    call print_f64\n\n");
            break;
        case print_s64:
            fprintf(ctx->program_file, "    mov r11, 0\nmov rax, [r15]\n"
                                       "    call print_num_rax\n\n");
            break;
        case print_u64:
            fprintf(ctx->program_file, "    mov r11, 0\nmov rax, [r15]\n"
                                       "    call print_num_rax\n\n");
            break;
        case dump_static:
            fprintf(ctx->program_file, "    call dump_static\n\n");
            break;
        case print_string:
            fprintf(ctx->program_file, "    call print_string\n\n");
            break;
        case read:
            fprintf(ctx->program_file, "    call read\n\n");
            break;
        case write:
            fprintf(ctx->program_file, "    call write\n\n");
            break;
        }
        break;
    }

    case INST_RSWAP:
    {
        uint64_t operand_conv = sv_to_unsigned64(operand);
        fprintf(ctx->program_file, "    mov rax, qword [r15 + %llu]\n"
                                   "    mov rbx, [r15]\n"
                                   "    mov [r15 + %llu], rbx\n"
                                   "    mov [r15], rax\n\n",
                (operand_conv) * 8, (operand_conv) * 8);
        break;
    }

    case INST_ASWAP:
    {
        uint64_t operand_conv = sv_to_unsigned64(operand);
        fprintf(ctx->program_file, "    mov rax, [stack + 8184 - %llu]\n"
                                   "    mov rbx, [r15]\n"
                                   "    mov [stack + 8184 - %llu], rbx\n"
                                   "    mov [r15], rax\n\n",
                (operand_conv) * 8, (operand_conv) * 8);
        break;
    }

    case INST_RDUP:
    {
        uint64_t operand_conv = sv_to_unsigned64(operand);
        fprintf(ctx->program_file, "    mov rax, qword [r15 + %llu]\n"
                                   "    sub r15, 8\n"
                                   "    mov [r15], rax\n\n",
                (operand_conv) * 8);
        break;
    }

    case INST_ADUP:
    {
        uint64_t operand_conv = sv_to_unsigned64(operand);
        fprintf(ctx->program_file, "    mov rax, qword [stack + 8184 - %llu]\n"
                                   "    sub r15, 8\n"
                                   "    mov [r15], rax\n\n",
                (operand_conv) * 8);
        break;
    }

    case INST_JMP:
    {
        if (target_is_num)
        {
            fprintf(ctx->program_file, "    jmp L%.*s\n\n", (int)operand->count, operand->data);
        }
        else
        {
            fprintf(ctx->program_file, "    jmp %.*s\n\n", (int)operand->count, operand->data);
        }

        break;
    }

    case INST_CALL:
    {
        if (target_is_num)
        {
            fprintf(ctx->program_file, "    sub r15, 8\n"
                                       "    mov qword [r15], call_%d\n"
                                       "    jmp L%.*s\n"
                                       "call_%d:\n"
                                       "    add r15, 8\n\n",
                    call_no, (int)operand->count, operand->data, call_no);
        }
        else
        {
            fprintf(ctx->program_file, "    sub r15, 8\n"
                                       "    mov qword [r15], call_%d\n"
                                       "    jmp %.*s\n"
                                       "call_%d:\n"
                                       "    add r15, 8\n\n",
                    call_no, (int)operand->count, operand->data, call_no);
        }

        call_no++;
        break;
    }

    case INST_RET:
    {
        fprintf(ctx->program_file, "    jmp [r15]\n\n");
        break;
    }

    case INST_EQU:
    case INST_EQS:
    {
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    cmp rax, [r15]"
                                   "    setz byte [r15]\n\n");
        break;
    }

    case INST_GEU:
    {
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    cmp rax, [r15]\n"
                                   "    setae byte [r15]\n\n");
        break;
    }

    case INST_GES:
    {
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    cmp rax, [r15]\n"
                                   "    setge byte [r15]\n\n");
        break;
    }

    case INST_GU:
    {
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    cmp rax, [r15]\n"
                                   "    seta byte [r15]\n\n");
        break;
    }

    case INST_GS:
    {
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    cmp rax, [r15]\n"
                                   "    setg byte [r15]\n\n");
        break;
    }

    case INST_LEU:
    {
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    cmp rax, [r15]\n"
                                   "    setbe byte [r15]\n\n");
        break;
    }

    case INST_LES:
    {
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    cmp rax, [r15]\n"
                                   "    setle byte [r15]\n\n");
        break;
    }

    case INST_LU:
    {
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    cmp rax, [r15]\n"
                                   "    setb byte [r15]\n\n");
        break;
    }

    case INST_LS:
    {
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    cmp rax, [r15]\n"
                                   "    setl byte [r15]\n\n");
        break;
    }

    case INST_EQF:
    {
        fprintf(ctx->program_file, "    movsd xmm0, [r15]\n"
                                   "    add r15, 8\n"
                                   "    vucomisd xmm0, [r15]\n"
                                   "    setz byte [r15]\n");
        break;
    }

    case INST_GEF:
    {
        fprintf(ctx->program_file, "    movsd xmm0, [r15]\n"
                                   "    add r15, 8\n"
                                   "    vucomisd xmm0, [r15]\n"
                                   "    setae byte [r15]\n");
        break;
    }

    case INST_LEF:
    {
        fprintf(ctx->program_file, "    movsd xmm0, [r15]\n"
                                   "    add r15, 8\n"
                                   "    vucomisd xmm0, [r15]\n"
                                   "    setbe byte [r15]\n");
        break;
    }

    case INST_GF:
    {
        fprintf(ctx->program_file, "    movsd xmm0, [r15]\n"
                                   "    add r15, 8\n"
                                   "    vucomisd xmm0, [r15]\n"
                                   "    seta byte [r15]\n");
        break;
    }

    case INST_LF:
    {
        fprintf(ctx->program_file, "    movsd xmm0, [r15]\n"
                                   "    add r15, 8\n"
                                   "    vucomisd xmm0, [r15]\n"
                                   "    setb byte [r15]\n\n");
        break;
    }

    case INST_NOTB:
    {
        fprintf(ctx->program_file, "    not qword [r15]\n\n");
        break;
    }

    case INST_ANDB:
    {
        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    and [r15], rax\n\n");
        break;
    }

    case INST_ORB:
    {

        fprintf(ctx->program_file, "    mov rax, [r15]\n"
                                   "    add r15, 8\n"
                                   "    or [r15], rax\n\n");
        break;
    }

    case INST_UJMP_IF:
    {
        if (target_is_num)
        {
            fprintf(ctx->program_file, "    cmp qword [r15], 0\n"
                                       "    jne L%.*s\n\n",
                    (int)operand->count, operand->data);
        }
        else
        {
            fprintf(ctx->program_file, "    cmp qword [r15], 0\n"
                                       "    jne %.*s\n\n",
                    (int)operand->count, operand->data);
        }
        break;
    }

    case INST_FJMP_IF:
    {
        if (target_is_num)
        {
            fprintf(ctx->program_file, "    movsd xmm0, [r15]\n"
                                       "    vxorpd xmm1, xmm1 ; bitwise xor the register contents\n"
                                       "    vucomisd xmm0, xmm1\n ; cmp equivalent for double-precision floating points"
                                       "    jne L%.*s\n\n",
                    (int)operand->count, operand->data);
        }
        else
        {
            fprintf(ctx->program_file, "    movsd xmm0, [r15]\n"
                                       "    vxorpd xmm1, xmm1 ; bitwise xor the register contents\n"
                                       "    vucomisd xmm0, xmm1\n ; cmp equivalent for double-precision floating points"
                                       "    jne %.*s\n\n",
                    (int)operand->count, operand->data);
        }

        break;
    }

    case INST_ASR:
    {
        fprintf(ctx->program_file, "    mov cl, %.*s\n"
                                   "    sar qword [r15], cl\n\n",
                (int)operand->count, operand->data);
        break;
    }

    case INST_LSR:
    {
        fprintf(ctx->program_file, "    mov cl, %.*s\n"
                                   "    shr qword [r15], cl\n\n",
                (int)operand->count, operand->data);
        break;
    }

    case INST_SL:
    {
        fprintf(ctx->program_file, "    mov cl, %.*s\n"
                                   "    shl qword [r15], cl\n\n",
                (int)operand->count, operand->data);
        break;
    }

    case INST_POP:
    {
        fprintf(ctx->program_file, "    add r15, 8\n\n");
        break;
    }

    default:
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                 "Line Number %zu -> ERROR: Unknown instruction number %zu",
                 ctx->line_no, inst_number);
        return false;
    }

    target_is_num = false;
    return true;
}

bool handle_data_line(CompilerContext *ctx, String_View *line)
{
    String_View label = sv_chop_by_delim(line, ':');
    if (*(line->data - 1) == ':')
    { // if there's a label
        if (!push_to_label_table(label, ctx))
        {
            return false; // Error already set in push_to_label_table
        }
        fprintf(ctx->data_file, "%.*s: ", (int)label.count, label.data);
        sv_trim_left(line);
    }
    else
    {
        // Reset line if no label found
        line->count = label.count;
        line->data = label.data;
    }

    if (line->count == 0)
    {
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                 "Line Number %zu -> ERROR: Data directive expected after label",
                 ctx->line_no);
        return false;
    }

    String_View data_type = sv_chop_by_delim(line, ' ');
    sv_trim_left(line);

    if (line->count == 0)
    {
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                 "Line Number %zu -> ERROR: Data value expected after directive %.*s",
                 ctx->line_no, (int)data_type.count, data_type.data);
        return false;
    }

    if (sv_eq(data_type, cstr_as_sv(".byte")))
    {
        if (ctx->data_section_offset >= vm_default_memory_size - sizeof(int8_t))
        {
            snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                     "Line Number %zu -> ERROR: Not enough default static memory for .byte %.*s",
                     ctx->line_no, (int)line->count, line->data);
            return false;
        }
        fprintf(ctx->data_file, "db %.*s\n", (int)line->count, line->data);
        ctx->data_section_offset += sizeof(int8_t);
    }
    else if (sv_eq(data_type, cstr_as_sv(".word")))
    {
        if (ctx->data_section_offset >= vm_default_memory_size - sizeof(int16_t))
        {
            snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                     "Line Number %zu -> ERROR: Not enough default static memory for .word %.*s",
                     ctx->line_no, (int)line->count, line->data);
            return false;
        }
        fprintf(ctx->data_file, "dw %.*s\n", (int)line->count, line->data);
        ctx->data_section_offset += sizeof(int16_t);
    }
    else if (sv_eq(data_type, cstr_as_sv(".doubleword")))
    {
        if (ctx->data_section_offset >= vm_default_memory_size - sizeof(int32_t))
        {
            snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                     "Line Number %zu -> ERROR: Not enough default static memory for .doubleword %.*s",
                     ctx->line_no, (int)line->count, line->data);
            return false;
        }
        fprintf(ctx->data_file, "dd %.*s\n", (int)line->count, line->data);
        ctx->data_section_offset += sizeof(int32_t);
    }
    else if (sv_eq(data_type, cstr_as_sv(".quadword")))
    {
        if (ctx->data_section_offset >= vm_default_memory_size - sizeof(int64_t))
        {
            snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                     "Line Number %zu -> ERROR: Not enough default static memory for .quadword %.*s",
                     ctx->line_no, (int)line->count, line->data);
            return false;
        }
        fprintf(ctx->data_file, "dq %.*s\n", (int)line->count, line->data);
        ctx->data_section_offset += sizeof(int64_t);
    }
    else if (sv_eq(data_type, cstr_as_sv(".double")))
    {
        if (ctx->data_section_offset >= vm_default_memory_size - sizeof(double))
        {
            snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                     "Line Number %zu -> ERROR: Not enough default static memory for .double %.*s",
                     ctx->line_no, (int)line->count, line->data);
            return false;
        }
        fprintf(ctx->data_file, "dq %.*s\n", (int)line->count, line->data);
        ctx->data_section_offset += sizeof(double);
    }
    else if (sv_eq(data_type, cstr_as_sv(".string")))
    {
        if (line->count >= 2 && line->data[0] == '"' && line->data[line->count - 1] == '"')
        {
            line->data++;
            line->count -= 2;
        }
        else
        {
            snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                     "Line Number %zu -> ERROR: %.*s not a valid vasm string",
                     ctx->line_no, (int)line->count, line->data);
            return false;
        }

        if (ctx->data_section_offset >= vm_default_memory_size - line->count - 1)
        {
            snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                     "Line Number %zu -> ERROR: Not enough default static memory for .string %.*s",
                     ctx->line_no, (int)line->count, line->data);
            return false;
        }

        fprintf(ctx->data_file, "db '%.*s'\n", (int)line->count, line->data);
        ctx->data_section_offset += line->count + 1; // +1 for null terminator
    }
    else
    {
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                 "Line Number %zu -> ERROR: invalid data type '%.*s'",
                 ctx->line_no, (int)data_type.count, data_type.data);
        return false;
    }

    return true;
}

bool handle_code_line(CompilerContext *ctx, String_View *line)
{
    if (ctx->code_section_offset >= vm_program_capacity)
    {
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE, "Program Too Big");
        return false;
    }

    String_View inst_name = sv_chop_by_delim(line, ' ');
    sv_trim_left(line);
    bool has_operand_value = line->count > 0;

    for (size_t i = 1; i < (size_t)INST_COUNT; i++)
    {
        if (sv_eq(inst_name, cstr_as_sv(get_inst_name(i))))
        {
            if (has_operand_function(i) != has_operand_value)
            {
                snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                         "Line Number %zu -> ERROR: %s %s an operand",
                         ctx->line_no, get_inst_name(i),
                         has_operand_function(i) ? "requires" : "doesn't require");
                return false;
            }

            if (!has_operand_function(i))
            {
                return handle_instruction(ctx, i, &(String_View){0});
            }

            uint8_t operand_type = check_operand_type(line);

            switch (get_operand_type(i))
            {
            case TYPE_SIGNED_64INT:
            {
                if (operand_type == TYPE_UNSIGNED_64INT || operand_type == TYPE_SIGNED_64INT)
                {
                    if (!handle_instruction(ctx, i, line))
                        return false;
                }
                else if (operand_type == TYPE_DOUBLE)
                {
                    snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                             "Line Number %zu -> ERROR: illegal operand value for %s instruction: %.*s\n"
                             "Must be a signed integral value",
                             ctx->line_no, get_inst_name(i), (int)line->count, line->data);
                    return false;
                }
                else if (operand_type == TYPE_INVALID)
                {
                    if (ctx->unresolved_labels_counter >= label_capacity)
                    {
                        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                                 "Line Number %zu -> ERROR: Too many unresolved labels",
                                 ctx->line_no);
                        return false;
                    }
                    ctx->unresolved_labels[ctx->unresolved_labels_counter].label = *line;
                    ctx->unresolved_labels[ctx->unresolved_labels_counter].line_no = ctx->line_no;
                    ctx->unresolved_labels[ctx->unresolved_labels_counter].target_is_num_ = false;

                    ctx->unresolved_labels_counter++;
                }
                return true;
            }
            case TYPE_UNSIGNED_64INT:
            {
                if (i == INST_CALL || i == INST_JMP || i == INST_FJMP_IF || i == INST_UJMP_IF)
                {
                    if (operand_type == TYPE_SIGNED_64INT)
                    {
                        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                                 "Line Number %zu -> ERROR: illegal operand value for %s instruction: %.*s\n"
                                 "Must be a label/unsigned 64 bit integer",
                                 ctx->line_no, get_inst_name(i), (int)line->count, line->data);
                        return false;
                    }
                    else if (operand_type == TYPE_UNSIGNED_64INT)
                    {
                        if (ctx->unresolved_labels_counter >= label_capacity)
                        {
                            snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                                     "Line Number %zu -> ERROR: Too many unresolved bindings",
                                     ctx->line_no);
                            return false;
                        }

                        char str[128];
                        snprintf(str, 128, "L%.*s", (int)line->count, line->data);

                        ctx->unresolved_labels[ctx->unresolved_labels_counter].label = cstr_as_sv(str);
                        ctx->unresolved_labels[ctx->unresolved_labels_counter].line_no = ctx->line_no;
                        ctx->unresolved_labels[ctx->unresolved_labels_counter].target_is_num_ = true;
                        ctx->unresolved_labels_counter++;

                        target_is_num = true;
                        if (!handle_instruction(ctx, i, line))
                            return false;
                    }
                    else
                    {
                        if (ctx->unresolved_labels_counter >= label_capacity)
                        {
                            snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                                     "Line Number %zu -> ERROR: Too many unresolved labels",
                                     ctx->line_no);
                            return false;
                        }
                        ctx->unresolved_labels[ctx->unresolved_labels_counter].label = *line;
                        ctx->unresolved_labels[ctx->unresolved_labels_counter].line_no = ctx->line_no;
                        ctx->unresolved_labels[ctx->unresolved_labels_counter].target_is_num_ = false;

                        ctx->unresolved_labels_counter++;

                        if (!handle_instruction(ctx, i, line))
                            return false;
                    }
                }
                else
                {
                    if (operand_type == TYPE_UNSIGNED_64INT)
                    {
                        if (!handle_instruction(ctx, i, line))
                            return false;
                    }
                    else if (operand_type == TYPE_DOUBLE || operand_type == TYPE_SIGNED_64INT)
                    {
                        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                                 "Line Number %zu -> ERROR: illegal operand value for %s instruction: %.*s\n"
                                 "Must be an unsigned integral value",
                                 ctx->line_no, get_inst_name(i), (int)line->count, line->data);
                        return false;
                    }
                    else if (operand_type == TYPE_INVALID)
                    {
                        if (ctx->unresolved_labels_counter >= label_capacity)
                        {
                            snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                                     "Line Number %zu -> ERROR: Too many unresolved labels",
                                     ctx->line_no);
                            return false;
                        }
                        ctx->unresolved_labels[ctx->unresolved_labels_counter].label = *line;
                        ctx->unresolved_labels[ctx->unresolved_labels_counter].line_no = ctx->line_no;
                        ctx->unresolved_labels[ctx->unresolved_labels_counter].target_is_num_ = false;

                        ctx->unresolved_labels_counter++;
                    }
                }
                return true;
            }
            case TYPE_DOUBLE:
            {
                if (operand_type == TYPE_INVALID)
                {
                    if (ctx->unresolved_labels_counter >= label_capacity)
                    {
                        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                                 "Line Number %zu -> ERROR: Too many unresolved labels",
                                 ctx->line_no);
                        return false;
                    }
                    ctx->unresolved_labels[ctx->unresolved_labels_counter].label = *line;
                    ctx->unresolved_labels[ctx->unresolved_labels_counter].line_no = ctx->line_no;
                    ctx->unresolved_labels[ctx->unresolved_labels_counter].target_is_num_ = false;

                    ctx->unresolved_labels_counter++;
                }
                else
                {
                    if (!handle_instruction(ctx, i, line))
                        return false;
                }
                return true;
            }
            default:
                snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                         "Line Number %zu -> ERROR: Unknown operand type for instruction %s",
                         ctx->line_no, get_inst_name(i));
                return false;
            }

            return handle_instruction(ctx, i, line);
        }
    }

    snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
             "Line Number %zu -> ERROR: Invalid instruction %.*s",
             ctx->line_no, (int)inst_name.count, inst_name.data);
    return false;
}

bool process_source_file(CompilerContext *ctx, const char *input_file)
{
    String_View source = slurp_file(input_file);
    if (!source.data)
    {
        snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE, "Failed to read input file: %s", input_file);
        return false;
    }

    while (source.count > 0)
    {
        ctx->line_no++;
        String_View line = sv_chop_by_delim(&source, '\n');
        line = sv_chop_by_delim(&line, ';');
        sv_trim_left(&line);
        sv_trim_right(&line);

        if (line.count == 0)
            continue;

        String_View section = line;
        String_View directive = sv_chop_by_delim(&section, ' ');

        if (sv_eq(directive, cstr_as_sv(".text")))
        {
            ctx->is_code = true;
            ctx->is_data = false;
        }
        else if (sv_eq(directive, cstr_as_sv(".data")))
        {
            ctx->is_code = false;
            ctx->is_data = true;
        }
        else if (ctx->is_code)
        {
            String_View label = sv_chop_by_delim(&line, ':');
            if (*(line.data - 1) == ':')
            {
                if (!push_to_label_table(label, ctx))
                    return false;
                fprintf(ctx->program_file, "%.*s:\n", (int)label.count, label.data);
                sv_trim_left(&line);
                if (line.count > 0 && !handle_code_line(ctx, &line))
                    return false;
            }
            else
            {
                if (!handle_code_line(ctx, &label))
                    return false;
            }

            if (sv_eq(label, cstr_as_sv("_start")))
            {
                size_t offset = vm_stack_capacity * sizeof(uint64_t);
                fprintf(ctx->program_file, "    mov r15, stack + %zu\n", offset);
            }
            ctx->code_section_offset++;
        }
        else if (ctx->is_data)
        {
            if (!handle_data_line(ctx, &line))
                return false;
        }
    }

    // Check for unresolved labels
    bool has_unresolved = false;
    for (size_t i = 0; i < ctx->unresolved_labels_counter; i++)
    {
        if (!search_in_label_table(ctx->unresolved_labels[i].label))
        {
            if (ctx->unresolved_labels[i].target_is_num_)
            {
                snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                         "Line No: %zu -> ERROR: Invalid VASM instruction number: '%.*s'",
                         ctx->unresolved_labels[i].line_no,
                         (int)ctx->unresolved_labels[i].label.count - 2,
                         ctx->unresolved_labels[i].label.data + 2);
            }
            else
            {
                snprintf(ctx->error_buffer, ERROR_BUFFER_SIZE,
                         "Line No: %zu -> ERROR: Undefined label: '%.*s'",
                         ctx->unresolved_labels[i].line_no,
                         (int)ctx->unresolved_labels[i].label.count,
                         ctx->unresolved_labels[i].label.data);
            }
            has_unresolved = true;
        }
    }

    if (!search_in_label_table(cstr_as_sv("_start")))
    {
        fprintf(stderr, "_start not found in the VASM source file; Defaulting to the first VASM instruction\n");
        if (current_inst_num >= 0)
        {
            size_t offset = vm_stack_capacity * sizeof(uint64_t);
            fprintf(ctx->program_file, "_start:\n"
                                       "    mov r15, stack + %zu\n"
                                       "    jmp L0",
                    offset);
        }
        else
        {
            fprintf(stderr, "The source file has no VASM instructions, cannot default\n");
            has_unresolved = true;
        }
    }

    if (has_unresolved)
    {
        return false;
    }

    return true;
}

void print_usage(char *program_name)
{
    fprintf(stderr, "Usage: %s <input_file> <output_file> [OPTIONS]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --stack-capacity <bytes>     Set the VM stack capacity in bytes (default: %llu)\n", VM_STACK_CAPACITY);
    fprintf(stderr, "  --label-capacity <bytes>     Set the label capacity for the program in bytes (default: %llu)\n", VM_LABEL_CAPACITY);
    fprintf(stderr, "  --static-limit <bytes>       Set the static memory limit in bytes (default: %llu)\n", VM_MEMORY_CAPACITY);
    fprintf(stderr, "  --default-static <bytes>     Set the default static memory size in bytes (default: %llu)\n", VM_DEFAULT_MEMORY_SIZE);
    fprintf(stderr, "\n");
}

int main(int argc, char **argv)
{
    static struct option long_options[] = {
        {"stack-capacity", required_argument, 0, 0},
        {"label-capacity", required_argument, 0, 0},
        {"static-limit", required_argument, 0, 0},
        {"default-static", required_argument, 0, 0},
        {0, 0, 0, 0}};

    int option_index = 0;
    int c;

    while ((c = getopt_long(argc, argv, "", long_options, &option_index)) != -1)
    {
        switch (option_index)
        {
        case 0:
            vm_stack_capacity = strtoul(optarg, NULL, 10);
            break;
        case 1:
            label_capacity = strtoul(optarg, NULL, 10);
            break;
        case 2:
            vm_memory_capacity = strtoul(optarg, NULL, 10);
            break;
        case 3:
            vm_default_memory_size = strtoul(optarg, NULL, 10);
            break;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind + 2 > argc)
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char *input_file = argv[optind];
    char *output_file = argv[optind + 1];

    assert(vm_program_capacity <= UINT64_MAX);
    assert(vm_memory_capacity <= UINT64_MAX);
    assert(vm_stack_capacity <= UINT64_MAX);
    assert(vm_default_memory_size < vm_memory_capacity);

    CompilerContext ctx = {0};
    if (!init_compiler_context(&ctx, output_file))
    {
        fprintf(stderr, "Initialization failed: %s\n", ctx.error_buffer);
        return EXIT_FAILURE;
    }

    if (!process_source_file(&ctx, input_file))
    {
        fprintf(stderr, "Compilation failed: %s\n", ctx.error_buffer);
        cleanup_compiler_context(&ctx);
        remove("temp.asm");
        remove(output_file);
        return EXIT_FAILURE;
    }

    // Merge program file into data file
    fseek(ctx.program_file, 0, SEEK_END);
    long program_size = ftell(ctx.program_file);
    fseek(ctx.program_file, 0, SEEK_SET);

    char *program_buffer = malloc(program_size);
    if (!program_buffer)
    {
        fprintf(stderr, "Failed to allocate memory for program buffer\n");
        cleanup_compiler_context(&ctx);
        return EXIT_FAILURE;
    }

    fread(program_buffer, 1, program_size, ctx.program_file);
    fprintf(ctx.data_file, "\n");
    fwrite(program_buffer, 1, program_size, ctx.data_file);

    free(program_buffer);
    cleanup_compiler_context(&ctx);
    remove("temp.asm");

    printf("Compilation successful!\n");
    return EXIT_SUCCESS;
}