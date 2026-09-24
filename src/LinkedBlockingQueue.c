#include "LinkedBlockingQueue.h"

// Destroy the linked blocking queue and associated mutex and event
PLINKED_BLOCKING_QUEUE_ENTRY LbqDestroyLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead) {
    LC_ASSERT(queueHead->shutdown || queueHead->draining || queueHead->lifetimeSize == 0);
    
    PltDeleteMutex(&queueHead->mutex);
    PltDeleteConditionVariable(&queueHead->cond);

    return queueHead->head;
}

// Flush the queue
PLINKED_BLOCKING_QUEUE_ENTRY LbqFlushQueueItems(PLINKED_BLOCKING_QUEUE queueHead) {
    PLINKED_BLOCKING_QUEUE_ENTRY head;

    PltLockMutex(&queueHead->mutex);

    // Save the old head
    head = queueHead->head;

    // Reinitialize the queue to empty
    if (head != NULL) {
        queueHead->head = NULL;
        queueHead->tail = NULL;
        queueHead->currentSize = 0;
    }
    else {
        LC_ASSERT(queueHead->tail == NULL);
        LC_ASSERT(queueHead->currentSize == 0);
    }

    PltUnlockMutex(&queueHead->mutex);

    return head;
}

// Linked blocking queue init
int LbqInitializeLinkedBlockingQueue(PLINKED_BLOCKING_QUEUE queueHead, int sizeBound) {
    int err;

    memset(queueHead, 0, sizeof(*queueHead));

    err = PltCreateMutex(&queueHead->mutex);
    if (err != 0) {
        return err;
    }

    err = PltCreateConditionVariable(&queueHead->cond, &queueHead->mutex);
    if (err != 0) {
        PltDeleteMutex(&queueHead->mutex);
        return err;
    }

    queueHead->sizeBound = sizeBound;

    return 0;
}

void LbqSignalQueueShutdown(PLINKED_BLOCKING_QUEUE queueHead) {
    PltLockMutex(&queueHead->mutex);
    queueHead->shutdown = true;
    PltUnlockMutex(&queueHead->mutex);
    PltSignalConditionVariable(&queueHead->cond);
}

void LbqSignalQueueDrain(PLINKED_BLOCKING_QUEUE queueHead) {
    PltLockMutex(&queueHead->mutex);
    queueHead->draining = true;
    PltUnlockMutex(&queueHead->mutex);
    PltSignalConditionVariable(&queueHead->cond);
}

void LbqSignalQueueUserWake(PLINKED_BLOCKING_QUEUE queueHead) {
    PltLockMutex(&queueHead->mutex);
    queueHead->pendingUserWake = true;
    PltUnlockMutex(&queueHead->mutex);
    PltSignalConditionVariable(&queueHead->cond);
}

int LbqGetItemCount(PLINKED_BLOCKING_QUEUE queueHead) {
    return queueHead->currentSize;
}

static int offerQueueItem(PLINKED_BLOCKING_QUEUE queueHead, void* data,
                          PLINKED_BLOCKING_QUEUE_ENTRY entry,
                          bool (*canReplaceTail)(const void* tailData),
                          void** replacedData) {
    bool wasEmpty;

    if (replacedData != NULL) {
        *replacedData = NULL;
    }
    entry->flink = NULL;
    entry->data = data;

    PltLockMutex(&queueHead->mutex);

    if (queueHead->shutdown || queueHead->draining) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_INTERRUPTED;
    }

    // Replace the tail while holding the same lock used by the consumer and
    // other producers. An intervening button or key is therefore a barrier.
    if (canReplaceTail != NULL && queueHead->tail != NULL &&
        canReplaceTail(queueHead->tail->data)) {
        PLINKED_BLOCKING_QUEUE_ENTRY oldTail = queueHead->tail;

        LC_ASSERT(replacedData != NULL);
        *replacedData = oldTail->data;
        entry->blink = oldTail->blink;
        if (entry->blink != NULL) {
            entry->blink->flink = entry;
        }
        else {
            queueHead->head = entry;
        }
        queueHead->tail = entry;
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_SUCCESS;
    }

    if (queueHead->currentSize == queueHead->sizeBound) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_BOUND_EXCEEDED;
    }

    wasEmpty = queueHead->head == NULL;
    if (wasEmpty) {
        LC_ASSERT(queueHead->currentSize == 0);
        LC_ASSERT(queueHead->tail == NULL);
        queueHead->head = entry;
        queueHead->tail = entry;
        entry->blink = NULL;
    }
    else {
        LC_ASSERT(queueHead->currentSize >= 1);
        LC_ASSERT(queueHead->head != NULL);
        queueHead->tail->flink = entry;
        entry->blink = queueHead->tail;
        queueHead->tail = entry;
    }

    queueHead->currentSize++;
    queueHead->lifetimeSize++;

    PltUnlockMutex(&queueHead->mutex);

    if (wasEmpty) {
        // Only call PltSignalConditionVariable() when transitioning from
        // empty -> non-empty to avoid a useless syscall for each new entry.
        PltSignalConditionVariable(&queueHead->cond);
    }

    return LBQ_SUCCESS;
}

int LbqOfferQueueItem(PLINKED_BLOCKING_QUEUE queueHead, void* data,
                      PLINKED_BLOCKING_QUEUE_ENTRY entry) {
    return offerQueueItem(queueHead, data, entry, NULL, NULL);
}

int LbqOfferQueueItemReplacingTail(PLINKED_BLOCKING_QUEUE queueHead, void* data,
                                   PLINKED_BLOCKING_QUEUE_ENTRY entry,
                                   bool (*canReplaceTail)(const void* tailData),
                                   void** replacedData) {
    LC_ASSERT(canReplaceTail != NULL && replacedData != NULL);
    return offerQueueItem(queueHead, data, entry, canReplaceTail, replacedData);
}

// This must be synchronized with LbqFlushQueueItems by the caller
int LbqPeekQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data) {
    PltLockMutex(&queueHead->mutex);

    if (queueHead->shutdown) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_INTERRUPTED;
    }

    if (queueHead->head == NULL) {
        if (queueHead->draining) {
            PltUnlockMutex(&queueHead->mutex);
            return LBQ_INTERRUPTED;
        }
        else {
            PltUnlockMutex(&queueHead->mutex);
            return LBQ_NO_ELEMENT;
        }
    }

    *data = queueHead->head->data;

    PltUnlockMutex(&queueHead->mutex);

    return LBQ_SUCCESS;
}

static int pollQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data,
                            bool (*matches)(const void* headData, const void* context),
                            const void* context) {
    PLINKED_BLOCKING_QUEUE_ENTRY entry;

    PltLockMutex(&queueHead->mutex);

    if (queueHead->shutdown) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_INTERRUPTED;
    }

    if (queueHead->head == NULL) {
        if (queueHead->draining) {
            PltUnlockMutex(&queueHead->mutex);
            return LBQ_INTERRUPTED;
        }
        else {
            PltUnlockMutex(&queueHead->mutex);
            return LBQ_NO_ELEMENT;
        }
    }

    entry = queueHead->head;
    if (matches != NULL && !matches(entry->data, context)) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_NO_ELEMENT;
    }

    queueHead->head = entry->flink;
    queueHead->currentSize--;
    if (queueHead->head == NULL) {
        LC_ASSERT(queueHead->currentSize == 0);
        queueHead->tail = NULL;
    }
    else {
        LC_ASSERT(queueHead->currentSize != 0);
        queueHead->head->blink = NULL;
    }

    *data = entry->data;

    PltUnlockMutex(&queueHead->mutex);

    return LBQ_SUCCESS;
}

int LbqPollQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data) {
    return pollQueueElement(queueHead, data, NULL, NULL);
}

int LbqPollQueueElementIf(PLINKED_BLOCKING_QUEUE queueHead, void** data,
                          bool (*matches)(const void* headData, const void* context),
                          const void* context) {
    LC_ASSERT(matches != NULL);
    return pollQueueElement(queueHead, data, matches, context);
}

int LbqWaitForQueueElement(PLINKED_BLOCKING_QUEUE queueHead, void** data) {
    PLINKED_BLOCKING_QUEUE_ENTRY entry;

    PltLockMutex(&queueHead->mutex);

    // Wait for a waking condition: either data available or rundown
    while (queueHead->head == NULL && !queueHead->draining && !queueHead->shutdown && !queueHead->pendingUserWake) {
        PltWaitForConditionVariable(&queueHead->cond, &queueHead->mutex);
    }

    // If we're shutting down, abort immediately, even if there's data available
    if (queueHead->shutdown) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_INTERRUPTED;
    }

    // If this is a user requested wake, process it now
    if (queueHead->pendingUserWake) {
        queueHead->pendingUserWake = false;
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_USER_WAKE;
    }

    // If we're draining, only abort if we have no data available
    if (queueHead->draining && queueHead->head == NULL) {
        PltUnlockMutex(&queueHead->mutex);
        return LBQ_INTERRUPTED;
    }

    // We should have bailed by this point if there was no data
    LC_ASSERT(queueHead->head != NULL);

    entry = queueHead->head;
    queueHead->head = entry->flink;
    queueHead->currentSize--;
    if (queueHead->head == NULL) {
        LC_ASSERT(queueHead->currentSize == 0);
        queueHead->tail = NULL;
    }
    else {
        LC_ASSERT(queueHead->currentSize != 0);
        queueHead->head->blink = NULL;
    }

    *data = entry->data;

    PltUnlockMutex(&queueHead->mutex);

    return LBQ_SUCCESS;
}
