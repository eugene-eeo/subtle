#ifndef SUBTLE_TABLE_H
#define SUBTLE_TABLE_H

#include "common.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

// Entries can be in 3 possible states:
//  1. !IS_UNDEFINED(key)                   -- the entry is valid (holds a key-value pair).
//  2.  IS_UNDEFINED(key) &&  IS_NIL(value) -- the entry is empty.
//  3.  IS_UNDEFINED(key) && !IS_NIL(value) -- the entry is a tombstone.
typedef struct {
    Value key;
    Value value;
} Entry;

#ifdef SUBTLE_DEBUG_TABLE_STATS
// Useful statistics for profiling tables.
#include <limits.h> // INT_MAX

typedef struct {
    int min;
    int max;
    float avg;
    int total;
    int count;
} TableStats;

extern TableStats table_stats;

#define UPDATE_TABLE_STATS(probes) \
    do { \
        table_stats.count++; \
        table_stats.total += probes; \
        table_stats.min = (table_stats.min > probes) ? probes : table_stats.min; \
        table_stats.max = (table_stats.max < probes) ? probes : table_stats.max; \
        table_stats.avg = (float)(table_stats.total) / (float)(table_stats.count); \
    } while (false)

#define RESET_TABLE_STATS() \
    do {\
        table_stats.min = INT_MAX; \
        table_stats.max = 0; \
        table_stats.avg = 0; \
        table_stats.count = 0; \
        table_stats.total = 0; \
    } while (false)
#endif

// We need the VM struct here, but vm.h needs table.h as well.
typedef struct VM VM;
typedef struct ObjString ObjString;

typedef struct {
    Entry* entries;
    size_t count; // Number of valid + tombstone entries
    size_t valid; // Number of valid entries
    size_t capacity;
} Table;

void table_init(Table* table);
void table_free(Table* table, VM* vm);
bool table_get(Table* table, Value key, Value* value);
bool table_set(Table* table, VM* vm, Value key, Value value);
bool table_delete(Table* table, VM* vm, Value key);
ObjString* table_find_string(Table* table,
                             const char* str, size_t length, uint32_t hash);
void table_mark(Table* table, VM* vm);
void table_remove_white(Table* table, VM* vm);

#ifdef SUBTLE_DEBUG
void table_print(Table* table);
#endif

#endif
