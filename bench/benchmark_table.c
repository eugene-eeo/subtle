#include <assert.h>
#include <bits/time.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>

#include "../vm.h"
#include "../table.h"
#include "../value.h"


void timespec_diff(struct timespec *start, struct timespec *stop,
                   struct timespec *result)
{
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }

    return;
}

#define MAX_VALUE 8192 * 4

int main() {
    printf("---------------\n");
    printf("Table benchmark (keys=%d)\n", MAX_VALUE);
    printf("---------------\n");
    struct timespec t1, t2, tdiff;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    VM vm;
    Table table;

    // Init
    vm_init(&vm);
    vm.next_gc = 5L * 1024 * 1024 * 1024;
    table_init(&table);
    Value tmp;
    int ops = 0;

    for (int i = 0; i < MAX_VALUE; i++) {
        table_set(&table, &vm, NUMBER_TO_VAL(i), NUMBER_TO_VAL(i));
        assert(table_get(&table, NUMBER_TO_VAL(i), &tmp));
        assert(value_equal(tmp, NUMBER_TO_VAL(i)));
        ops += 2;

        assert(table.count == table.valid);
        assert(table.valid == i + 1);
        assert(table.capacity >= i);
        assert(table.capacity >= 8);

        for (int j = 0; j < i; j++) {
            assert(table_get(&table, NUMBER_TO_VAL(j), &tmp));
            assert(value_equal(tmp, NUMBER_TO_VAL(j)));
            ops += 1;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t2);
    timespec_diff(&t1, &t2, &tdiff);
    printf("[insert+read] ops: %d, total: %fs, ns/op: %f\n",
           ops,
           ((double) tdiff.tv_sec + (double) tdiff.tv_nsec / 1e9),
           ((double) tdiff.tv_sec * 1e9 + (double) tdiff.tv_nsec) / (double)ops);

    // Deletion Benchmark
    ops = 0;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    for (int i = MAX_VALUE - 1; i >= 0; i--) {
        table_delete(&table, &vm, NUMBER_TO_VAL(i));
        assert(!table_get(&table, NUMBER_TO_VAL(i), &tmp));
        ops += 2;

        assert(table.count >= table.valid);
        assert(table.valid == i);
        assert(table.capacity >= 8);

        for (int j = 0; j < i; j++) {
            assert(table_get(&table, NUMBER_TO_VAL(j), &tmp));
            assert(value_equal(tmp, NUMBER_TO_VAL(j)));
            ops += 1;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t2);
    timespec_diff(&t1, &t2, &tdiff);
    printf("[delete+read] ops: %d, total: %fs, ns/op: %f\n",
           ops,
           ((double) tdiff.tv_sec + (double) tdiff.tv_nsec / 1e9),
           ((double) tdiff.tv_sec * 1e9 + (double) tdiff.tv_nsec) / (double)ops);
}
