#include "Limelight-internal.h"
#include "plank_transport_pointer.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "pointer line %d\n", __LINE__); exit(1); } } while (0)
static atomic_uint count;
static unsigned char types[8], operations[8];
static int sendNative(void* context, uint8_t type, const uint8_t* data, size_t size) {
    (void)context;
    unsigned i = atomic_load(&count); CHECK(i < 8);
    types[i] = type;
    if (type == PLANK_TRANSPORT_INPUT_POINTER_CAPTURE) {
        PlankPointerPacket packet; CHECK(plank_pointer_decode(data, size, &packet) == 0);
        operations[i] = packet.operation;
    }
    atomic_store(&count, i + 1); return 0;
}
static void pointer(unsigned operation, unsigned sequence, float x, float y) {
    PlankPointerPacket packet = {operation, 1, sequence, 1920, 1080, x, y};
    uint8_t bytes[PLANK_POINTER_PACKET_SIZE]; plank_pointer_encode(bytes, &packet);
    CHECK(LiSendPlankPointerCapture(bytes, sizeof(bytes)) == 0);
}
int main(void) {
    PlankPointerPacket packet = {PLANK_POINTER_MOVE, 1, 2, 1920, 1080, .125f, -.25f}, decoded;
    uint8_t bytes[PLANK_POINTER_PACKET_SIZE]; plank_pointer_encode(bytes, &packet);
    CHECK(plank_pointer_decode(bytes, sizeof(bytes), &decoded) == 0);
    CHECK(decoded.x == .125f && decoded.y == -.25f && decoded.sequence == 2);
    CHECK(plank_pointer_decode(bytes, sizeof(bytes)-1, &decoded) != 0);
    bytes[2] = 1; CHECK(plank_pointer_decode(bytes, sizeof(bytes), &decoded) != 0); bytes[2] = 0;
    packet.x = NAN; plank_pointer_encode(bytes, &packet);
    CHECK(plank_pointer_decode(bytes, sizeof(bytes), &decoded) != 0);
    CHECK(initializePlatform() == 0);
    LiSetPlankNativeInputSender(sendNative, NULL);
    CHECK(initializeInputStream() == 0 && startInputStream() == 0);
    pointer(PLANK_POINTER_BEGIN, 1, 100, 200);
    CHECK(LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT) == 0);
    pointer(PLANK_POINTER_MOVE, 2, .125f, -.25f);
    pointer(PLANK_POINTER_MOVE, 3, .5f, 1.25f);
    CHECK(LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT) == 0);
    pointer(PLANK_POINTER_END, 4, 0, 0);
    for (unsigned i=0; atomic_load(&count)<6 && i<200; ++i) PltSleepMs(5);
    CHECK(atomic_load(&count) == 6);
    CHECK(types[0] == PLANK_TRANSPORT_INPUT_POINTER_CAPTURE && operations[0] == PLANK_POINTER_BEGIN);
    CHECK(types[1] == PLANK_TRANSPORT_INPUT_MOUSE_BUTTON);
    CHECK(operations[2] == PLANK_POINTER_MOVE && operations[3] == PLANK_POINTER_MOVE);
    CHECK(types[4] == PLANK_TRANSPORT_INPUT_MOUSE_BUTTON && operations[5] == PLANK_POINTER_END);
    CHECK(stopInputStream() == 0); destroyInputStream(); cleanupPlatform();
    puts("pointer packet validation and ordered gesture=pass");
}
