#include <arch/gcc_asm.h>

.text


FUNCTION(_context_create):
    // Store the function pointer in PC
    movq %rsi, 56(%rdi);

    // Align the stack pointer, and store a NULL return address
    subq $16, %rdx;
    movq $132, 0(%rdx);
    movq %rdx, 16(%rdi);

    ret;


FUNCTION(context_save):
    movl 4(%esp), %ecx; // First param context

    // Save all callee-saves into the context
    movl %ebx, 0(%ecx);
    movl %ebp, 4(%ecx);
    movl %edi, 8(%ecx)
    movl %esi, 12(%ecx);
    movl %esp, 16(%ecx);

    movl 0(%esp), %edx;
    movl %edx, 20(%ecx);

    xorl %eax, %eax;
    ret;


FUNCTION(context_restore):
    movl 4(%esp), %ecx; // First param context

_context_restore_nostack:
    // Restore all the callee-saved registers
    movl 0(%ecx), %ebx;
    movl 4(%ecx), %ebp;
    movl 8(%ecx), %edi;
    movl 12(%ecx), %esi;
    movl 16(%ecx), %esp;

    // Place the PC in our return address
    movl 20(%ecx), %edx;
    movl %edx, 0(%esp);

    movl $1, %eax;
    ret;


FUNCTION(context_cmpxchg_restore):
    // We need another register to do it all atomically. Avoid
    // touching ESP so we can call _context_restore_nostack.
    movl %edi, -4(%esp);

    // First param context (as used by _context_restore_nostack)
    movl 4(%esp), %ecx;

    movl 8(%esp), %edi;  // Second param address
    movl 12(%esp), %eax; // Third param old value
    movl 16(%esp), %edx; // Fourth param new value
    lock cmpxchg %edx, 0(%edi);
    jz   _context_restore_nostack;

    movl -4(%esp), %edi; // Restore EDI
    ret;
