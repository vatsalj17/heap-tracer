#define _GNU_SOURCE
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <malloc.h>

// ─── 1. MULTITHREADED HAMMER ───────────────────────────────────────────────
#define THREADS 8
#define OPS_PER_THREAD 200

void* thread_alloc(void* arg) {
    (void)arg;
    void* ptrs[OPS_PER_THREAD];
    for (int i = 0; i < OPS_PER_THREAD; i++)
        ptrs[i] = malloc(rand() % 256 + 1);
    for (int i = 0; i < OPS_PER_THREAD; i++)
        if (i % 3 != 0) free(ptrs[i]);
    return NULL;
}

void test_multithreaded() {
    pthread_t t[THREADS];
    for (int i = 0; i < THREADS; i++)
        pthread_create(&t[i], NULL, thread_alloc, NULL);
    for (int i = 0; i < THREADS; i++)
        pthread_join(t[i], NULL);
}

// ─── 2. SIZE CLASSES (overflow probe) ─────────────────────────────────────
void test_size_classes() {
    size_t sizes[] = {1, 2, 4, 7, 8, 9, 15, 16, 17,
                      31, 32, 64, 128, 256, 512, 1024,
                      4096, 65536, 1048576, 67108864};
    int n = sizeof(sizes) / sizeof(sizes[0]);
    void* ptrs[n];
    for (int i = 0; i < n; i++)
        ptrs[i] = malloc(sizes[i]);
    for (int i = n - 1; i >= 0; i--)
        free(ptrs[i]);
}

// ─── 3. REALLOC CHAIN ──────────────────────────────────────────────────────
void test_realloc_chain() {
    void* p = malloc(1);
    for (size_t s = 2; s <= 65536; s *= 2)
        p = realloc(p, s);
    free(p);
}

// ─── 4. REALLOC TO ZERO ────────────────────────────────────────────────────
// glibc treats realloc(ptr, 0) as free — tool must not double-count
void test_realloc_zero() {
    void* p = malloc(128);
    p = realloc(p, 0);
}

// ─── 5. CALLOC OVERFLOW ────────────────────────────────────────────────────
void test_calloc_overflow() {
    void* p = calloc(SIZE_MAX / 2, 3);
    if (p) free(p);
}

// ─── 6. INTERIOR POINTER FREE ──────────────────────────────────────────────
void test_interior_pointer() {
    char* p = malloc(64);
    free(p + 16);
    free(p);
}

// ─── 7. USE AFTER FREE READ ────────────────────────────────────────────────
void test_use_after_free() {
    int* p = malloc(sizeof(int));
    *p = 42;
    free(p);
    volatile int x = *p;
    (void)x;
}

// ─── 8. MMAP BLIND SPOT ────────────────────────────────────────────────────
void test_mmap_blind_spot() {
    void* p = malloc(4 * 1024 * 1024);
    free(p);
    void* raw = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    munmap(raw, 4096);
}

// ─── 9. STRDUP / STRNDUP ───────────────────────────────────────────────────
void test_strdup_tracking() {
    char* s1 = strdup("heap tracer test string");
    char* s2 = strndup("another one", 7);
    free(s1);
    // s2 intentionally leaked
}

// ─── 10. ALIGNED ALLOCS ────────────────────────────────────────────────────
// posix_memalign and aligned_alloc — shim must intercept these
void test_aligned_allocs() {
    void* p1;
    posix_memalign(&p1, 64, 256);
    void* p2 = aligned_alloc(128, 512);
    free(p1);
    free(p2);
}

// ─── 11. ALLOCATOR CHURN ───────────────────────────────────────────────────
void test_churn() {
    #define CHURN_N 500
    void* live[CHURN_N];
    memset(live, 0, sizeof(live));
    for (int round = 0; round < 4; round++) {
        for (int i = 0; i < CHURN_N; i++) {
            if (live[i]) { free(live[i]); live[i] = NULL; }
            else live[i] = malloc((i % 32 + 1) * 8);
        }
    }
    for (int i = 0; i < CHURN_N; i++)
        if (live[i]) free(live[i]);
}

// ─── 12. FORK — child inherits shim state ──────────────────────────────────
// without pthread_atfork reset in shim, child will print duplicate summary
// and parent+child alloc counts will be wrong
void test_fork() {
    void* pre_fork = malloc(64);

    pid_t pid = fork();
    if (pid == 0) {
        // child: allocates its own memory
        void* child_alloc = malloc(128);
        free(child_alloc);
        // pre_fork is NOT freed in child — tests whether child summary
        // incorrectly reports parent's allocations as its own leaks
        exit(0);
    } else {
        // parent: frees its own allocation
        free(pre_fork);
        void* post_fork = malloc(32);
        free(post_fork);
        waitpid(pid, NULL, 0);
    }
}

// ─── 13. FORK + EXEC ───────────────────────────────────────────────────────
// shim reloads fresh in child after exec — previous tracking context is gone
// tests whether tool handles the exec boundary cleanly
void test_fork_exec() {
    pid_t pid = fork();
    if (pid == 0) {
        char* args[] = {"/bin/ls", "/tmp", NULL};
        execvp(args[0], args);
        exit(1);
    } else {
        waitpid(pid, NULL, 0);
    }
}

// ─── 14. VFORK ─────────────────────────────────────────────────────────────
// shares address space with parent until exec/exit
// any alloc in child between vfork and exec corrupts parent's shim state
void test_vfork() {
    pid_t pid = vfork();
    if (pid == 0) {
        char* args[] = {"/bin/true", NULL};
        execvp(args[0], args);
        _exit(1);
    } else {
        waitpid(pid, NULL, 0);
    }
}

// ─── 15. SIGTERM HANDLER ───────────────────────────────────────────────────
// destructor won't fire on SIGTERM unless catch it
// this tests whether shim registers a handler and prints summary
void test_sigterm() {
    void* p = malloc(256);
    (void)p;
    // leak p, then send SIGTERM to self
    // if shim catches SIGTERM → summary prints
    // if not → summary is lost silently
    raise(SIGTERM);
}

// ─── 16. SIGINT HANDLER ────────────────────────────────────────────────────
void test_sigint() {
    void* p = malloc(128);
    (void)p;
    raise(SIGINT);
}

// ─── 17. MALLOC_USABLE_SIZE MISMATCH ──────────────────────────────────────
// glibc may allocate more than requested due to alignment/header overhead
// tool tracks requested size, not actual usable size — they differ
void test_usable_size() {
    void* p = malloc(7);
    size_t actual = malloc_usable_size(p);
    // actual is likely 24 or 16, not 7
    // your tool will report 7 freed, but glibc gave back `actual` bytes
    (void)actual;
    free(p);
}

// ─── 18. THREAD-LOCAL STORAGE ──────────────────────────────────────────────
void* tls_thread(void* arg) {
    (void)arg;
    static __thread int tls_var = 0;
    tls_var++;
    void* p = malloc(64);
    free(p);
    return NULL;
}

void test_tls_alloc() {
    pthread_t t;
    pthread_create(&t, NULL, tls_thread, NULL);
    pthread_join(t, NULL);
}

// ─── 19. RAPID THREAD SPAWN (destructor race) ──────────────────────────────
// threads finishing while other threads still allocating
// tests whether your destructor fires at the right time
// and whether thread exit triggers partial summaries
void* rapid_thread(void* arg) {
    (void)arg;
    void* p = malloc(32);
    free(p);
    void* leak = malloc(16);
    (void)leak;
    return NULL;
}

void test_rapid_thread_spawn() {
    #define RAPID_T 20
    pthread_t t[RAPID_T];
    for (int i = 0; i < RAPID_T; i++)
        pthread_create(&t[i], NULL, rapid_thread, NULL);
    for (int i = 0; i < RAPID_T; i++)
        pthread_join(t[i], NULL);
}

// ─── 20. MALLOC INSIDE DLOPEN ──────────────────────────────────────────────
// dlopen triggers allocations during library load
// tests whether those allocs are visible/invisible and if they corrupt count
void test_dlopen_alloc() {
    void* handle = dlopen("libm.so.6", RTLD_LAZY);
    if (handle) {
        void* p = malloc(64);
        free(p);
        dlclose(handle);
    }
}

// ─── 21. FRAGMENTED LEAK PATTERN ───────────────────────────────────────────
void test_fragmented_leaks() {
    void* ptrs[10];
    for (int i = 0; i < 10; i++)
        ptrs[i] = malloc((i + 1) * 16);
    for (int i = 0; i < 10; i += 2)
        free(ptrs[i]);
    // odd indices leaked intentionally
}

// ─── 22. DOUBLE FREE ───────────────────────────────────────────────────────
void test_double_free() {
    int* p = malloc(16);
    free(p);
    free(p);
}

// ─── 23. LARGE LEAK ────────────────────────────────────────────────────────
void test_large_leak() {
    void* big = malloc(1024 * 1024);
    (void)big;
}

// ─── 24. MANY SMALL LEAKS ──────────────────────────────────────────────────
void test_many_small_leaks() {
    for (int i = 0; i < 20; i++)
        malloc(i + 1);
}

int main() {
    // TODO: add assertions

    srand(time(0));
    test_multithreaded();
    test_size_classes();
    test_realloc_chain();
    test_realloc_zero();
    test_calloc_overflow();
    test_interior_pointer();
    test_use_after_free();
    test_mmap_blind_spot();
    test_strdup_tracking();
    test_aligned_allocs();
    test_churn();
    test_fork();
    test_fork_exec();
    test_vfork();
    test_tls_alloc();
    test_rapid_thread_spawn();
    test_dlopen_alloc();
    test_fragmented_leaks();
    test_double_free();
    test_large_leak();
    test_many_small_leaks();
    // test_malloc_usable_size();

    // these send signals — run last since they may terminate process
    test_sigterm();
    test_sigint();

    return 0;
}
