#include "LinkedBlockingQueue.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed at %d\n", __LINE__); exit(1); } } while (0)
enum { MOTION = 1, BARRIER = 2 };
typedef struct {
    LINKED_BLOCKING_QUEUE_ENTRY entry;
    int kind;
    int value;
} ITEM;

static bool isMotion(const void *data) {
    return ((const ITEM *)data)->kind == MOTION;
}

static bool matchesKind(const void *data, const void *context) {
    return ((const ITEM *)data)->kind == *(const int *)context;
}

static void offer(LINKED_BLOCKING_QUEUE *queue, ITEM *item) {
    CHECK(LbqOfferQueueItem(queue, item, &item->entry) == LBQ_SUCCESS);
}

static void testBoundaries(void) {
    LINKED_BLOCKING_QUEUE queue;
    ITEM first = { .kind = MOTION, .value = 1 };
    ITEM second = { .kind = MOTION, .value = 2 };
    ITEM third = { .kind = MOTION, .value = 3 };
    ITEM barrier = { .kind = BARRIER };
    const int motionKind = MOTION;
    const int barrierKind = BARRIER;
    void *out = &barrier;
    void *replaced = &barrier;

    CHECK(LbqInitializeLinkedBlockingQueue(&queue, 2) == 0);
    CHECK(LbqPollQueueElementIf(&queue, &out, matchesKind, &motionKind) == LBQ_NO_ELEMENT);
    CHECK(out == &barrier);
    offer(&queue, &first);
    CHECK(LbqOfferQueueItemReplacingTail(&queue, &second, &second.entry,
                                       isMotion, &replaced) == LBQ_SUCCESS);
    CHECK(replaced == &first && queue.currentSize == 1);
    CHECK(queue.head == &second.entry && queue.tail == &second.entry);
    CHECK(LbqPollQueueElementIf(&queue, &out, matchesKind, &barrierKind) == LBQ_NO_ELEMENT);
    CHECK(out == &barrier && queue.currentSize == 1);
    CHECK(LbqPollQueueElementIf(&queue, &out, matchesKind, &motionKind) == LBQ_SUCCESS);
    CHECK(out == &second && queue.currentSize == 0);
    CHECK(queue.head == NULL && queue.tail == NULL);

    offer(&queue, &first);
    offer(&queue, &barrier);
    CHECK(LbqOfferQueueItemReplacingTail(&queue, &second, &second.entry,
                                       isMotion, &replaced) == LBQ_BOUND_EXCEEDED);
    CHECK(replaced == NULL && queue.currentSize == 2);
    CHECK(LbqPollQueueElementIf(&queue, &out, matchesKind, &motionKind) == LBQ_SUCCESS);
    CHECK(out == &first && queue.head == &barrier.entry && barrier.entry.blink == NULL);
    offer(&queue, &second);
    CHECK(LbqOfferQueueItemReplacingTail(&queue, &third, &third.entry,
                                       isMotion, &replaced) == LBQ_SUCCESS);
    CHECK(replaced == &second && queue.currentSize == 2);
    CHECK(barrier.entry.flink == &third.entry && third.entry.blink == &barrier.entry);
    CHECK(LbqPollQueueElementIf(&queue, &out, matchesKind, &motionKind) == LBQ_NO_ELEMENT);
    CHECK(queue.head == &barrier.entry && queue.currentSize == 2);

    LbqSignalQueueDrain(&queue);
    CHECK(LbqPollQueueElementIf(&queue, &out, matchesKind, &barrierKind) == LBQ_SUCCESS);
    CHECK(out == &barrier);
    CHECK(LbqPollQueueElementIf(&queue, &out, matchesKind, &motionKind) == LBQ_SUCCESS);
    CHECK(out == &third);
    CHECK(LbqPollQueueElementIf(&queue, &out, matchesKind, &motionKind) == LBQ_INTERRUPTED);
    CHECK(LbqOfferQueueItemReplacingTail(&queue, &first, &first.entry,
                                       isMotion, &replaced) == LBQ_INTERRUPTED);
    CHECK(replaced == NULL && LbqDestroyLinkedBlockingQueue(&queue) == NULL);

    CHECK(LbqInitializeLinkedBlockingQueue(&queue, 2) == 0);
    offer(&queue, &first);
    LbqSignalQueueShutdown(&queue);
    CHECK(LbqPollQueueElementIf(&queue, &out, matchesKind, &motionKind) == LBQ_INTERRUPTED);
    CHECK(queue.currentSize == 1);
    CHECK(LbqDestroyLinkedBlockingQueue(&queue) == &first.entry);
}

typedef struct {
    LINKED_BLOCKING_QUEUE queue;
    ITEM first;
    ITEM next;
    atomic_bool inspectStarted;
    atomic_bool producerStarted;
    void *replaced;
    int result;
} CONCURRENT_CASE;

static void producer(void *context) {
    CONCURRENT_CASE *test = context;
    while (!atomic_load(&test->inspectStarted)) PltSleepMs(1);
    atomic_store(&test->producerStarted, true);
    test->result = LbqOfferQueueItemReplacingTail(&test->queue, &test->next,
                                                 &test->next.entry, isMotion,
                                                 &test->replaced);
    if (test->replaced != NULL) ((ITEM *)test->replaced)->value = -1;
}

static bool inspectWhileProducerRuns(const void *data, const void *context) {
    CONCURRENT_CASE *test = (CONCURRENT_CASE *)context;
    CHECK(data == &test->first && test->first.value == 123);
    atomic_store(&test->inspectStarted, true);
    while (!atomic_load(&test->producerStarted)) PltSleepMs(1);
    // Give the producer a chance to contend while inspection owns the lock.
    PltSleepMs(5);
    CHECK(((const ITEM *)data)->value == 123);
    return true;
}

static void testConcurrentOwnership(void) {
    CONCURRENT_CASE test = { .first = { .kind = MOTION, .value = 123 },
                             .next = { .kind = MOTION, .value = 456 } };
    PLT_THREAD thread;
    void *out = NULL;
    CHECK(LbqInitializeLinkedBlockingQueue(&test.queue, 2) == 0);
    offer(&test.queue, &test.first);
    CHECK(PltCreateThread("QueueTest", producer, &test, &thread) == 0);
    CHECK(LbqPollQueueElementIf(&test.queue, &out, inspectWhileProducerRuns, &test) == LBQ_SUCCESS);
    CHECK(out == &test.first && test.first.value == 123);
    PltJoinThread(&thread);
    // Producer enqueued a new item, never replaced the consumer-owned item.
    CHECK(test.result == LBQ_SUCCESS && test.replaced == NULL);
    CHECK(test.first.value == 123 && test.queue.currentSize == 1);
    CHECK(LbqPollQueueElement(&test.queue, &out) == LBQ_SUCCESS && out == &test.next);
    LbqSignalQueueDrain(&test.queue);
    CHECK(LbqDestroyLinkedBlockingQueue(&test.queue) == NULL);
}

int main(void) {
    CHECK(initializePlatform() == 0);
    testBoundaries();
    testConcurrentOwnership();
    cleanupPlatform();
    puts("queue_ownership=pass mismatch_barriers=1 full_queue=1 drain=1 shutdown=1 concurrent_replacement=1");
    return 0;
}
