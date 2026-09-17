#pragma once

#include "Platform.h"
#include "PlatformThreads.h"

#define LBQ_SUCCESS 0
#define LBQ_INTERRUPTED 1
#define LBQ_BOUND_EXCEEDED 2
#define LBQ_NO_ELEMENT 3
#define LBQ_USER_WAKE 4

typedef struct _LINKED_BLOCKING_QUEUE_ENTRY {
    struct _LINKED_BLOCKING_QUEUE_ENTRY* flink;
    struct _LINKED_BLOCKING_QUEUE_ENTRY* blink;
    void* data;
} LINKED_BLOCKING_QUEUE_ENTRY, *PLINKED_BLOCKING_QUEUE_ENTRY;

typedef struct _LINKED_BLOCKING_QUEUE {
    PLT_MUTEX mutex;
    PLT_COND cond;
    PLINKED_BLOCKING_QUEUE_ENTRY head;
    PLINKED_BLOCKING_QUEUE_ENTRY tail;
    int sizeBound;
    int currentSize;
    int lifetimeSize;
    bool shutdown;
    bool draining;
    bool pendingUserWake;
} LINKED_BLOCKING_QUEUE, *PLINKED_BLOCKING_QUEUE;

int LbqInitializeLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead, int sizeBound);
int LbqOfferQueueItem(PLINKED_BLOCKING_QUEUE queueHead, void* data, PLINKED_BLOCKING_QUEUE_ENTRY entry);
// Atomically replace a matching tail or enqueue. On replacement, the caller
// owns *replacedData and must dispose of it after this function returns.
int LbqOfferQueueItemReplacingTail(PLINKED_BLOCKING_QUEUE queueHead, void* data,
                                   PLINKED_BLOCKING_QUEUE_ENTRY entry,
                                   bool (*canReplaceTail)(const void* tailData),
                                   void** replacedData);
int LbqWaitForQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);
int LbqPollQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);
// Inspect and remove the head atomically. The predicate runs under the queue
// mutex and must not retain headData or re-enter the queue. On success the
// caller owns *data; a nonmatching head returns LBQ_NO_ELEMENT without removal.
int LbqPollQueueElementIf(PLINKED_BLOCKING_QUEUE queueHead, void** data,
                          bool (*matches)(const void* headData, const void* context),
                          const void* context);
// Borrowed pointer: callers must exclude concurrent removal, flush, and tail
// replacement until they finish inspecting it. Use the conditional poll above
// when a producer can replace queued entries.
int LbqPeekQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data);
PLINKED_BLOCKING_QUEUE_ENTRY LbqDestroyLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead);
PLINKED_BLOCKING_QUEUE_ENTRY LbqFlushQueueItems(PLINKED_BLOCKING_QUEUE queueHead);
void LbqSignalQueueShutdown(PLINKED_BLOCKING_QUEUE queueHead);
void LbqSignalQueueDrain(PLINKED_BLOCKING_QUEUE queueHead);
void LbqSignalQueueUserWake(PLINKED_BLOCKING_QUEUE queueHead);
int LbqGetItemCount(PLINKED_BLOCKING_QUEUE queueHead);
