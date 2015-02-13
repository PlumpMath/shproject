
#define FUNCTION(symbol)               \
    .globl symbol;                     \
    .type symbol, @function;           \
    symbol

