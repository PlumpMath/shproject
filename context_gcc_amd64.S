.text


#define FUNCTION(symbol)               \
    .globl symbol;                     \
    .type symbol, @function;           \
    symbol


#include <scheduler_defs.h>


FUNCTION(_context_create):
    // Store the function pointer in PC
    movq %rsi, 56(%rdi);

    // Align the stack pointer, and store a NULL return address
    subq $16, %rdx;
    movq $132, 0(%rdx);
    movq %rdx, 16(%rdi);

    ret;


FUNCTION(_context_save):
    // Save all callee-saves into the context
    movq %rbx, 0(%rdi);
    movq %rbp, 8(%rdi);
    movq %rsp, 16(%rdi);
    movq %r12, 24(%rdi);
    movq %r13, 32(%rdi);
    movq %r14, 40(%rdi);
    movq %r15, 48(%rdi);

    movq 0(%rsp), %rax;
    movq %rax, 56(%rdi);

    xorl %eax, %eax;
    ret;


FUNCTION(_context_restore):
    // Restore all the callee-saved registers
    movq 0(%rdi), %rbx;
    movq 8(%rdi), %rbp;
    movq 16(%rdi), %rsp;
    movq 24(%rdi), %r12;
    movq 32(%rdi), %r13;
    movq 40(%rdi), %r14;
    movq 48(%rdi), %r15;

    // Place the PC in our return address
    movq 56(%rdi), %rax;
    movq %rax, 0(%rsp);

    movl $1, %eax;
    ret;


FUNCTION(_context_sleep_and_restore):
    movl  $STATE_BLOCKED, %eax;
    xchg  %eax, 0(%rsi);
    testl $STATE_RUNNABLE, %eax;
    jz _context_restore
    movl  $STATE_RUNNABLE, 0(%rsi);
    ret;
