/* Concurrency test for irqvec: notices from a producer thread, takes from one
 * or two consumer threads. Invariant: taken + pending == notices emitted. */
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "irqvec.h"

#define NOTICES 1000000u

static volatile int producing = 1;
static uint32_t vec;
static uint64_t taken[2];

static void *producer(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < NOTICES; i++) irq_vector_notice(vec);
    __atomic_store_n(&producing, 0, __ATOMIC_SEQ_CST);
    return NULL;
}

static void *consumer(void *arg) {
    uint64_t *acc = arg;
    while (__atomic_load_n(&producing, __ATOMIC_SEQ_CST)) {
        *acc += irq_vector_take(vec);
    }
    return NULL;
}

static int run(int consumers) {
    pthread_t p, c[2];
    taken[0] = taken[1] = 0;
    __atomic_store_n(&producing, 1, __ATOMIC_SEQ_CST);

    int v = irq_vector_alloc(NULL, NULL);
    if (v < 0) return 1;
    vec = (uint32_t)v;

    pthread_create(&p, NULL, producer, NULL);
    for (int i = 0; i < consumers; i++) pthread_create(&c[i], NULL, consumer, &taken[i]);
    pthread_join(p, NULL);
    for (int i = 0; i < consumers; i++) pthread_join(c[i], NULL);

    /* producer finished: drain what is left */
    uint64_t total = taken[0] + taken[1] + irq_vector_take(vec);
    irq_vector_free(vec);

    if (total != NOTICES) {
        printf("FAIL consumers=%d: total=%llu expected=%u\n",
               consumers, (unsigned long long)total, NOTICES);
        return 1;
    }
    printf("ok consumers=%d: %u notices accounted for\n", consumers, NOTICES);
    return 0;
}

int main(void) {
    int bad = 0;
    bad |= run(1);
    bad |= run(2);
    printf(bad ? "irqvec_mt: FAILURES\n" : "irqvec_mt: 0 failures\n");
    return bad;
}
