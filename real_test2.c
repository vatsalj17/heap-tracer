#define _GNU_SOURCE
#include <stdio.h>
#include <wait.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define ASSERT_EQ(a, b) assert((a) == (b))

// ---- HOOKS FROM HEAP TRACER ----
extern size_t ht_total_allocated();
extern size_t ht_total_freed();
extern size_t ht_current_usage();
extern size_t ht_leak_count();
extern size_t ht_invalid_frees();

// ---- RESET STATE BETWEEN TESTS ----
void reset() {
    // optional: if tracer supports reset
}

// ------------------------------------------------------------
// 1. BASIC CONTRACT TEST
// ------------------------------------------------------------
void test_basic_alloc_free() {
    reset();

    void *p = malloc(100);
    ASSERT_EQ(ht_total_allocated(), 100);

    free(p);
    ASSERT_EQ(ht_total_freed(), 100);
    ASSERT_EQ(ht_current_usage(), 0);
}

// ------------------------------------------------------------
// 2. LEAK DETECTION CONTRACT
// ------------------------------------------------------------
void test_leak_detection() {
    reset();

    malloc(64);
    malloc(128);

    ASSERT_EQ(ht_leak_count(), 2);
}

// ------------------------------------------------------------
// 3. REALLOC ACCOUNTING (STRICT)
// ------------------------------------------------------------
void test_realloc_accounting() {
    reset();

    void *p = malloc(100);
    p = realloc(p, 200);

    ASSERT_EQ(ht_total_allocated(), 300); // 100 + 200
    ASSERT_EQ(ht_total_freed(), 100);

    free(p);
    ASSERT_EQ(ht_current_usage(), 0);
}

// ------------------------------------------------------------
// 4. REALLOC SHRINK
// ------------------------------------------------------------
void test_realloc_shrink() {
    reset();

    void *p = malloc(200);
    p = realloc(p, 50);

    ASSERT_EQ(ht_total_allocated(), 250);
    ASSERT_EQ(ht_total_freed(), 200);

    free(p);
    ASSERT_EQ(ht_current_usage(), 0);
}

// ------------------------------------------------------------
// 5. DOUBLE FREE DETECTION
// ------------------------------------------------------------
void test_double_free() {
    reset();

    void *p = malloc(32);
    free(p);
    free(p);  // invalid

    ASSERT_EQ(ht_invalid_frees(), 1);
}

// ------------------------------------------------------------
// 6. INTERIOR POINTER REJECTION
// ------------------------------------------------------------
void test_invalid_pointer_free() {
    reset();

    char *p = malloc(64);
    free(p + 1);

    ASSERT_EQ(ht_invalid_frees(), 1);

    free(p);
}

// ------------------------------------------------------------
// 7. CALLOC OVERFLOW SAFETY
// ------------------------------------------------------------
void test_calloc_overflow() {
    reset();

    void *p = calloc(SIZE_MAX / 2, 3);
    ASSERT_EQ(p, NULL);
}

// ------------------------------------------------------------
// 8. MULTITHREAD CONSISTENCY
// ------------------------------------------------------------
#define THREADS 4
#define OPS 1000

void *worker(void *arg) {
    (void)arg;

    for (int i = 0; i < OPS; i++) {
        void *p = malloc(32);
        free(p);
    }

    return NULL;
}

void test_multithreaded_consistency() {
    reset();

    pthread_t t[THREADS];

    for (int i = 0; i < THREADS; i++)
        pthread_create(&t[i], NULL, worker, NULL);

    for (int i = 0; i < THREADS; i++)
        pthread_join(t[i], NULL);

    ASSERT_EQ(ht_current_usage(), 0);
}

// ------------------------------------------------------------
// 9. STRESS TEST WITH VALIDATION
// ------------------------------------------------------------
void test_stress() {
    reset();

    const int N = 10000;
    void *ptrs[N];

    for (int i = 0; i < N; i++) {
        ptrs[i] = malloc(i % 128 + 1);
    }

    for (int i = 0; i < N; i++) {
        free(ptrs[i]);
    }

    ASSERT_EQ(ht_current_usage(), 0);
}

// ------------------------------------------------------------
// 10. FORK SAFETY TEST
// ------------------------------------------------------------
void test_fork_behavior() {
    reset();

    pid_t pid = fork();

    if (pid == 0) {
        void *p = malloc(100);
        free(p);
        exit(0);
    } else {
        wait(NULL);
        ASSERT_EQ(ht_current_usage(), 0);
    }
}

// ------------------------------------------------------------
// MAIN RUNNER
// ------------------------------------------------------------
int main() {
    printf("Running heap tracer validation...\n");

    test_basic_alloc_free();
    test_leak_detection();
    test_realloc_accounting();
    test_realloc_shrink();
    test_double_free();
    test_invalid_pointer_free();
    test_calloc_overflow();
    test_multithreaded_consistency();
    test_stress();
    test_fork_behavior();

    printf("All tests passed.\n");
    return 0;
}
