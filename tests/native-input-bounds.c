// Exercise the real queued sender without a network, desktop, or input device.
#include "Limelight-internal.h"
#include "plank_transport_input.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d failed\n", __LINE__); exit(1); } } while (0)
static struct { uint8_t type; size_t size; uint8_t payload[8]; } records[128];
static atomic_uint count;
static atomic_bool holdSender, senderBlocked;

static int sendNative(void* context, uint8_t type, const uint8_t* data, size_t size) {
    (void)context;
    if (atomic_load(&holdSender)) {
        atomic_store(&senderBlocked, true);
        while (atomic_load(&holdSender)) PltSleepMs(1);
    }
    unsigned index = atomic_load(&count);
    CHECK(index < 128 && size <= sizeof(records[index].payload));
    records[index].type = type;
    records[index].size = size;
    memcpy(records[index].payload, data, size);
    atomic_store(&count, index + 1);
    return 0;
}

static void waitFor(unsigned total) {
    for (unsigned i = 0; atomic_load(&count) < total && i < 1000; ++i) PltSleepMs(1);
    CHECK(atomic_load(&count) == total);
}

static void position(unsigned index, short x, short y, short width, short height) {
    CHECK(records[index].type == PLANK_TRANSPORT_INPUT_ABSOLUTE_MOUSE);
    CHECK(records[index].size == PLANK_TRANSPORT_INPUT_ABSOLUTE_MOUSE_SIZE);
    unsigned actualX = plank_transport_input_read_u16(records[index].payload);
    unsigned actualY = plank_transport_input_read_u16(records[index].payload + 2);
    unsigned maximumX = plank_transport_input_read_u16(records[index].payload + 4);
    unsigned maximumY = plank_transport_input_read_u16(records[index].payload + 6);
    CHECK(actualX == (unsigned)x && actualY == (unsigned)y);
    CHECK(maximumX == (unsigned)(width - 1) && maximumY == (unsigned)(height - 1));
    CHECK(maximumX && maximumY && actualX <= maximumX && actualY <= maximumY);
}

int main(void) {
    CHECK(initializePlatform() == 0);
    LiSetPlankNativeInputSender(sendNative, NULL);
    CHECK(initializeInputStream() == 0 && startInputStream() == 0);
    const short dimensions[][2] = {{2, 2}, {1920, 1080}, {3840, 2160},
        {5120, 2160}, {8192, 4320}, {SHRT_MAX, SHRT_MAX}};
    unsigned total = 0;
    for (unsigned i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); ++i) {
        short w = dimensions[i][0], h = dimensions[i][1];
        const short samples[][4] = {
            {0, 0, 0, 0}, {w - 1, h - 1, w - 1, h - 1},
            {w, 0, w - 1, 0}, {0, h, 0, h - 1}, {w, h, w - 1, h - 1},
            {-1, -1, 0, 0}, {SHRT_MIN, SHRT_MIN, 0, 0},
            {SHRT_MAX, SHRT_MAX, w - 1, h - 1}, {w / 2, h / 2, w / 2, h / 2}
        };
        for (unsigned j = 0; j < sizeof(samples) / sizeof(samples[0]); ++j) {
            CHECK(LiSendMousePositionEvent(samples[j][0], samples[j][1], w, h) == 0);
            waitFor(++total);
            position(total - 1, samples[j][2], samples[j][3], w, h);
        }
    }
    const short invalid[][2] = {{0, 1080}, {1920, 0}, {1, 1080}, {1920, 1}, {-1, 1080}, {1920, -1}};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        CHECK(LiSendMousePositionEvent(0, 0, invalid[i][0], invalid[i][1]) != 0);

    // Hold the actual sender while queuing an out-of-image position before a
    // press. Preserve that position (clamped), then the drag and release order.
    atomic_store(&holdSender, true);
    CHECK(LiSendHighResScrollEvent(1) == 0);
    for (unsigned i = 0; !atomic_load(&senderBlocked) && i < 1000; ++i) PltSleepMs(1);
    CHECK(atomic_load(&senderBlocked));
    CHECK(LiSendMousePositionEvent(5120, 2160, 5120, 2160) == 0);
    CHECK(LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT) == 0);
    CHECK(LiSendMousePositionEvent(100, 100, 5120, 2160) == 0);
    CHECK(LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT) == 0);
    atomic_store(&holdSender, false);
    waitFor(total + 5);
    CHECK(records[total].type == PLANK_TRANSPORT_INPUT_VERTICAL_SCROLL);
    position(total + 1, 5119, 2159, 5120, 2160);
    CHECK(records[total + 2].type == PLANK_TRANSPORT_INPUT_MOUSE_BUTTON && records[total + 2].payload[1] == 1);
    position(total + 3, 100, 100, 5120, 2160);
    CHECK(records[total + 4].type == PLANK_TRANSPORT_INPUT_MOUSE_BUTTON && records[total + 4].payload[1] == 0);
    CHECK(stopInputStream() == 0);
    destroyInputStream();
    cleanupPlatform();
    puts("native_input_bounds=pass dimensions=6 samples=54 invalid_dimensions=6 ordered_edge_drag=1");
    return 0;
}
