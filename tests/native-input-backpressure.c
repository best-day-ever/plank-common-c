// Exercise actual input/callback workers, not a second queue implementation.
#include "Limelight-internal.h"
#include "plank_transport_input.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d failed\n", __LINE__); exit(1); } } while (0)
static atomic_uint attempts, accepted, failures, callbacks;
static atomic_bool busy, holdCallback, callbackEntered;
static uint8_t received[200][32];
static size_t lengths[200];

static int sender(void* context, uint8_t type, const uint8_t* data, size_t size) {
    (void)context; (void)type;
    atomic_fetch_add(&attempts, 1);
    if (atomic_load(&busy)) return LI_PLANK_INPUT_WOULD_BLOCK;
    unsigned index = atomic_load(&accepted);
    CHECK(index < 200 && size <= 32);
    memcpy(received[index], data, size); lengths[index] = size;
    atomic_store(&accepted, index + 1);
    return 0;
}
static void terminated(int error) { CHECK(error < 0); atomic_fetch_add(&failures, 1); }
static void waitCount(atomic_uint* count, unsigned minimum) {
    uint64_t deadline = PltGetMillis() + 3000;
    while (atomic_load(count) < minimum && PltGetMillis() < deadline) PltSleepMs(1);
    CHECK(atomic_load(count) >= minimum);
}
static int control(void* context, uint32_t type, uint32_t a, uint32_t b) {
    (void)context; (void)type; (void)a; (void)b; return 0;
}
static void rawCallback(const unsigned char* data, unsigned int length) {
    (void)data; CHECK(length == sizeof(PLANK_RAW_HID_WIRE_HEADER));
    atomic_store(&callbackEntered, true);
    while (atomic_load(&holdCallback)) PltSleepMs(1);
    atomic_fetch_add(&callbacks, 1);
}
static void cursorCallback(const unsigned char* data, unsigned int length) {
    (void)data; CHECK(length == sizeof(PLANK_CURSOR_POSITION_WIRE_MESSAGE));
}

int main(void) {
    CHECK(initializePlatform() == 0);
    ListenerCallbacks.connectionTerminated = terminated;
    SunshineFeatureFlags = LI_FF_RAW_HID_TABLET;
    LiSetPlankNativeInputSender(sender, NULL);
    CHECK(initializeInputStream() == 0 && startInputStream() == 0);
    atomic_store(&busy, true);
    unsigned char raw[sizeof(PLANK_RAW_HID_WIRE_HEADER)] = {0};
    CHECK(LiSendRawHidEvent(raw, sizeof(raw)) == 0);
    waitCount(&attempts, 10);
    CHECK(atomic_load(&accepted) == 0);
    CHECK(LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT) == 0);
    CHECK(LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT) == 0);
    atomic_store(&busy, false);
    waitCount(&accepted, 3);
    CHECK(lengths[0] == sizeof(raw) && memcmp(raw, received[0], sizeof(raw)) == 0);
    CHECK(received[1][1] == 1 && received[2][1] == 0);
    CHECK(atomic_load(&failures) == 0);

    // Disconnect while pressure persists must cancel, not wait two seconds.
    atomic_store(&busy, true);
    unsigned before = atomic_load(&attempts);
    CHECK(LiSendRawHidEvent(raw, sizeof(raw)) == 0);
    waitCount(&attempts, before + 3);
    uint64_t start = PltGetMillis();
    CHECK(stopInputStream() == 0 && PltGetMillis() - start < 250);
    destroyInputStream();
    CHECK(atomic_load(&failures) == 0);

    // Bounded ingress exhaustion must terminate explicitly, never report
    // success for an event that was silently discarded.
    CHECK(initializeInputStream() == 0 && startInputStream() == 0);
    before = atomic_load(&attempts);
    CHECK(LiSendRawHidEvent(raw, sizeof(raw)) == 0);
    waitCount(&attempts, before + 3);
    unsigned queued = 0;
    while (queued < 160 && LiSendRawHidEvent(raw, sizeof(raw)) == 0) ++queued;
    CHECK(queued == 150);
    waitCount(&failures, 1);
    CHECK(stopInputStream() == 0); destroyInputStream();
    CHECK(atomic_load(&failures) == 1);

    // A cursor burst must not fill the callback queue ahead of a tablet
    // acknowledgement. Coalescing is atomic with consumer ownership.
    LiSetPlankNativeControlSender(control, NULL);
    ListenerCallbacks.rawHidControl = rawCallback;
    ListenerCallbacks.cursorPosition = cursorCallback;
    CHECK(initializeControlStream() == 0 && startControlStream() == 0);
    atomic_store(&holdCallback, true);
    LiNotifyPlankRawHidControl(raw, sizeof(raw));
    start = PltGetMillis();
    while (!atomic_load(&callbackEntered) && PltGetMillis() - start < 1000) PltSleepMs(1);
    CHECK(atomic_load(&callbackEntered));
    unsigned char position[sizeof(PLANK_CURSOR_POSITION_WIRE_MESSAGE)] = {0};
    for (unsigned i = 0; i < 1000; ++i) LiNotifyPlankCursorPosition(position, sizeof(position));
    LiNotifyPlankRawHidControl(raw, sizeof(raw));
    CHECK(atomic_load(&failures) == 1);
    atomic_store(&holdCallback, false);
    waitCount(&callbacks, 2);
    CHECK(stopControlStream() == 0); destroyControlStream();
    cleanupPlatform();
    puts("native input backpressure, ordered transitions, cancellation, overflow and cursor/control isolation: pass");
    return 0;
}
