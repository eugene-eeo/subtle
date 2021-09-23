#ifndef SUBTLE_COMMON_H
#define SUBTLE_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SUBTLE_DEBUG_TRACE_EXECUTION
#define SUBTLE_DEBUG_PRINT_CODE
#define SUBTLE_DEBUG_TRACE_ALLOC

#ifdef SUBTLE_DEBUG
    #include <stdio.h>
    #include <stdlib.h>

    #define ASSERT(expr, msg) \
        do { \
            if (!(expr)) { \
                fprintf(stderr, "[%s:%d] Assert failed in %s(): %s\n", \
                        __FILE__, __LINE__, __func__, msg); \
                abort(); \
            } \
        } while (false)

    #define UNREACHABLE() \
        do { \
            fprintf(stderr, "[%s:%d] %s(): Reached unreachable code\n", \
                    __FILE__, __LINE__, __func__); \
            abort(); \
        } while (false)
#else
    #define ASSERT(expr, msg) do { } while (false)
    #define UNREACHABLE() __builtin_unreachable()
#endif

#endif
