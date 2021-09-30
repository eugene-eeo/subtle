#ifndef SUBTLE_TABLE_H
#define SUBTLE_TABLE_H

#include "common.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

// Entries can be in 3 possible states:
//  1. !IS_UNDEFINED(key)                   -- the entry holds a key-value pair.
//  2.  IS_UNDEFINED(key) &&  IS_NIL(value) -- the entry is empty.
//  3.  IS_UNDEFINED(key) && !IS_NIL(value) -- the entry is a tombstone.
typedef struct {
    Value key;
    Value value;
} Entry;

// We need the VM struct here, but vm.h needs table.h as well.
typedef struct VM VM;
typedef struct ObjString ObjString;

typedef struct {
    Entry* entries;
    size_t count;
    size_t capacity;
} Table;

void table_init(Table* table);
void table_free(Table* table, VM* vm);
bool table_get(Table* table, Value key, Value* value);
bool table_set(Table* table, VM* vm, Value key, Value value);
bool table_delete(Table* table, Value key);
ObjString* table_find_string(Table* table,
                             const char* str, size_t length, uint32_t hash);
void table_mark(Table* table, VM* vm);
void table_remove_white(Table* table, VM* vm);

#endif
