#include "Limelight-internal.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed at %d\n", __LINE__); exit(1); } } while (0)
static int calls;
static int64_t times[220];
static int sizes[220];

static void sample(char* data, int length, int64_t pts) {
    CHECK(calls < 220);
    CHECK(data != NULL || length == 0);
    times[calls] = pts;
    sizes[calls++] = length;
}

int main(void) {
    const unsigned char packet[] = {1, 2, 3};
    AudioCallbacks.decodeAndPlaySample = sample;
    CHECK(initializeAudioStream() == 0);
    // A hole before the first media packet has no known source epoch.
    CHECK(LiSubmitPlankAudioPacket(NULL, 0, 240, 480, 0) == 0);
    CHECK(calls == 2 && times[0] == -1 && times[1] == -1);
    CHECK(LiSubmitPlankAudioPacket(packet, 3, 240, 0, 123456789) == 0);
    CHECK(times[2] == 123456789000LL && sizes[2] == 3);
    CHECK(LiSubmitPlankAudioPacket(NULL, 0, 240, 720, 0) == 0);
    CHECK(calls == 6 && times[3] == 123456794000LL && times[5] == 123456804000LL);
    CHECK(sizes[3] == 0 && sizes[5] == 0);
    // Forward/backward capture-clock changes are not flattened into packet count.
    CHECK(LiSubmitPlankAudioPacket(packet, 3, 240, 0, 223456789) == 0);
    CHECK(times[6] == 223456789000LL);
    CHECK(LiSubmitPlankAudioPacket(packet, 3, 240, 0, 5) == 0);
    CHECK(times[7] == 5000);
    CHECK(LiSubmitPlankAudioPacket(NULL, 0, 240, 240, 0) == 0);
    CHECK(times[8] == 10000);
    CHECK(LiSubmitPlankAudioPacket(packet, 3, 240, 0, UINT64_MAX) == -1);
    CHECK(LiSubmitPlankAudioPacket(packet, 3, 0, 0, 1) == -1);
    CHECK(LiSubmitPlankAudioPacket(NULL, 0, 240, UINT32_MAX, 0) == -1);
    CHECK(LiSubmitPlankAudioPacket(NULL, 0, 240, 241, 0) == -1);
    CHECK(LiSubmitPlankAudioPacket(NULL, 0, 1, 1, 0) == -1);
    CHECK(LiSubmitPlankAudioPacket(NULL, 0, 6000, 6000, 0) == -1);
    CHECK(calls == 9);
    // Reconnect must not extrapolate the previous Host's clock.
    CHECK(initializeAudioStream() == 0);
    CHECK(LiSubmitPlankAudioPacket(NULL, 0, 240, 240, 0) == 0);
    CHECK(times[9] == -1);
    CHECK(LiSubmitPlankAudioPacket(packet, 3, 240, 0, 0) == 0);
    CHECK(times[10] == 0); // zero is a valid epoch, not the unknown sentinel
    puts("audio_timestamp_callback=pass");
    return 0;
}
