#ifndef SUBTLE_COMPILER_H
#define SUBTLE_COMPILER_H

#include "common.h"
#include "object.h"

typedef struct VM VM;
typedef struct Compiler Compiler;

ObjFunction* compile(VM* vm, const char* source);

#endif
