#include "table.h"
#include "memory.h"
#include "vm.h"

#include <stdio.h>
#include <string.h>  // memcmp

void table_init(Table* table, VM* vm) {
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
    table->vm = vm;
}

void table_free(Table* table) {
    FREE_ARRAY(table->entries, Entry, table->count);
    table_init(table, NULL);
}

static Entry* table_find_entry(Entry* entries, size_t capacity, Value key) {
    size_t index = value_hash(key) & (capacity - 1);
    Entry* tombstone = NULL;
    // As long as we keep TABLE_MAX_LOAD < 1, we will never have to
    // worry about wrapping around to index, because the table will
    // always have enough space for empty entries.
    for (;;) {
        Entry* entry = &entries[index];
        if (IS_UNDEFINED(entry->key)) {
            if (IS_NIL(entry->value)) {
                return tombstone == NULL ? entry : tombstone;
            } else {
                if (tombstone == NULL)
                    tombstone = entry;
            }
        } else if (value_equal(entry->key, key)) {
            // Found the key.
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static void table_adjust_capacity(Table* table, size_t capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (size_t i = 0; i < capacity; i++) {
        entries[i].key = UNDEFINED_VAL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;

    for (size_t i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (IS_UNDEFINED(entry->key)) continue;

        Entry* dst = table_find_entry(entries, capacity, entry->key);
        dst->key = entry->key;
        dst->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(table->entries, Entry, table->capacity);
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

bool table_set(Table* table, Value key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        size_t new_capacity = GROW_CAPACITY(table->capacity);
        table_adjust_capacity(table, new_capacity);
    }

    Entry* entry = table_find_entry(table->entries, table->capacity, key);
    bool is_new_key = IS_UNDEFINED(entry->key);
    if (is_new_key && IS_NIL(entry->value))
        table->count++;

    entry->key = key;
    entry->value = value;

    return is_new_key;
}

bool table_delete(Table* table, Value key) {
    if (table->count == 0) return false;

    Entry* entry = table_find_entry(table->entries, table->capacity, key);
    if (IS_UNDEFINED(entry->key)) return false;

    // Leave a tombstone.
    entry->key = UNDEFINED_VAL;
    entry->value = BOOL_TO_VAL(true);
    return true;
}

ObjString*
table_find_string(Table* table,
               const char* chars, size_t length, uint32_t hash)
{
    if (table->count == 0) return NULL;
    size_t index = hash & (table->capacity - 1);
    for (;;) {
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
    }
}
