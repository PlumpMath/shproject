#ifndef _CONTEXT_X86_64_H
#define _CONTEXT_X86_64_H


struct arch_context {
    long rbx;
    long rbp;
    long rsp;
    long r12;
    long r13;
    long r14;
    long r15;
    long pc;
};


#endif
