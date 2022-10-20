#include "table.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>  // memcmp

void table_init(Table* table) {
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
}

void table_free(Table* table, VM* vm) {
    FREE_ARRAY(vm, table->entries, Entry, table->capacity);
    table_init(table);
}

static Entry* table_find_entry(Entry* entries, uint32_t capacity, Value key) {
    uint32_t index = value_hash(key) & (capacity - 1);
    uint32_t start = index;
    Entry* tombstone = NULL;
    do {
        Entry* entry = &entries[index];
        if (IS_UNDEFINED(entry->key)) {
            if (IS_NIL(entry->value)) {
                // Empty entry
                return tombstone == NULL ? entry : tombstone;
            } else {
                // Tombstone entry
                if (tombstone == NULL)
                    tombstone = entry;
            }
        } else if (value_equal(entry->key, key)) {
            // Found the key.
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    } while (index != start);
    ASSERT(tombstone != NULL, "Table should have empty values or tombstones.");
    return tombstone;
}

static void table_adjust_capacity(Table* table, VM* vm, uint32_t capacity) {
    Entry* entries = ALLOCATE_ARRAY(vm, Entry, capacity);
    for (uint32_t i = 0; i < capacity; i++) {
        entries[i].key = UNDEFINED_VAL;
        entries[i].value = NIL_VAL;
    }

    for (uint32_t i = 0; i < table->capacity; i++) {
        Entry* src = &table->entries[i];
        if (IS_UNDEFINED(src->key)) continue;

        Entry* dst = table_find_entry(entries, capacity, src->key);
        ASSERT(IS_UNDEFINED(dst->key) && IS_NIL(dst->value), "dst is not empty");
        dst->key = src->key;
        dst->value = src->value;
    }

    FREE_ARRAY(vm, table->entries, Entry, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool table_get(Table* table, Value key, Value* value) {
    if (table->count == 0) return false;

    Entry* entry = table_find_entry(table->entries, table->capacity, key);
    if (IS_UNDEFINED(entry->key)) return false;

    *value = entry->value;
    return true;
}

bool table_set(Table* table, VM* vm, Value key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        uint32_t new_capacity = GROW_CAPACITY(table->capacity);
        table_adjust_capacity(table, vm, new_capacity);
    }

    Entry* entry = table_find_entry(table->entries, table->capacity, key);
    bool is_new_key = IS_UNDEFINED(entry->key);
    if (is_new_key && IS_NIL(entry->value))
        table->count++;

    entry->key = key;
    entry->value = value;

    return is_new_key;
}

// Just deletes a key from the table, without compaction
static
bool table_delete_key(Table* table, VM* vm, Value key) {
    if (table->count == 0) return false;

    Entry* entry = table_find_entry(table->entries, table->capacity, key);
    if (IS_UNDEFINED(entry->key)) return false;

    // Leave a tombstone.
    entry->key = UNDEFINED_VAL;
    entry->value = NIL_VAL;
    table->count--;
    return true;
}

static
void table_compact(Table* table, VM* vm) {
    // Compact the table if necessary.
    if (table->capacity > 8
            && table->count * GROW_FACTOR < table->capacity * TABLE_MAX_LOAD) {
        // count * FACTOR < cap * MAX_LOAD
        // count < (cap / FACTOR) * MAX_LOAD
        uint32_t new_capacity = SHRINK_CAPACITY(table->capacity);
        table_adjust_capacity(table, vm, new_capacity);
        ASSERT(table->capacity >= 8, "capacity < min_capacity");
    }

    ASSERT(table->count <= table->capacity * TABLE_MAX_LOAD, "count < max_load");
}

bool table_delete(Table* table, VM* vm, Value key) {
    bool rv = table_delete_key(table, vm, key);
    table_compact(table, vm);
    return rv;
}

ObjString*
table_find_string(Table* table,
                  const char* chars, size_t length, uint32_t hash)
{
    if (table->count == 0) return NULL;
    uint32_t index = hash & (table->capacity - 1);
    uint32_t start = index;
    do {
        Entry* entry = &table->entries[index];
        if (IS_UNDEFINED(entry->key)) {
            if (IS_NIL(entry->value)) return NULL;
        } else if (IS_STRING(entry->key)) {
            ObjString* str = VAL_TO_STRING(entry->key);
            if (str->hash == hash
                    && str->length == length
                    && memcmp(str->chars, chars, length) == 0) {
                return str;
            }
        }
        index = (index + 1) & (table->capacity - 1);
    } while (index != start);
    return NULL;
}

void
table_mark(Table* table, VM* vm)
{
    for (uint32_t i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        mark_value(vm, entry->key);
        mark_value(vm, entry->value);
    }
}

void
table_remove_white(Table* table, VM* vm)
{
    for (uint32_t i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (IS_OBJ(entry->key) && !VAL_TO_OBJ(entry->key)->marked)
            table_delete_key(table, vm, entry->key);
    }
}
