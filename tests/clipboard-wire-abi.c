// Compile the same ABI contract as C11 and C++17. No serialization or I/O
// backend is involved; sizeof/offsetof do not assume host byte order.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "plank_transport_input.h"

#ifdef __cplusplus
#define ABI_ASSERT static_assert
#define ABI_ALIGNOF alignof
#else
#define ABI_ASSERT _Static_assert
#define ABI_ALIGNOF _Alignof
#endif

struct NativePackingBefore { uint8_t tag; uint64_t value; };

// Check restoration of a caller's non-default packing, not just a reset to
// native alignment. Also verify native alignment after the caller pops it.
#pragma pack(push, 2)
struct CallerPackingBefore { uint8_t tag; uint64_t value; };
#include "plank.h"
struct CallerPackingAfter { uint8_t tag; uint64_t value; };
#pragma pack(pop)
struct NativePackingAfter { uint8_t tag; uint64_t value; };

ABI_ASSERT(sizeof(PLANK_CLIPBOARD_WIRE_HEADER) == 32, "clipboard header size");
ABI_ASSERT(ABI_ALIGNOF(PLANK_CLIPBOARD_WIRE_HEADER) == 1, "clipboard header alignment");

#define ABI_OFFSET(member, expected) \
    ABI_ASSERT(offsetof(PLANK_CLIPBOARD_WIRE_HEADER, member) == (expected), \
               "clipboard " #member " offset")
ABI_OFFSET(magic, 0);
ABI_OFFSET(version, 4);
ABI_OFFSET(reserved, 6);
ABI_OFFSET(flags, 8);
ABI_OFFSET(generation, 12);
ABI_OFFSET(totalSize, 20);
ABI_OFFSET(chunkOffset, 24);
ABI_OFFSET(chunkSize, 28);

#define ABI_PACKING(before, after) \
    ABI_ASSERT(sizeof(struct before) == sizeof(struct after), "packing size restored"); \
    ABI_ASSERT(ABI_ALIGNOF(struct before) == ABI_ALIGNOF(struct after), "alignment restored"); \
    ABI_ASSERT(offsetof(struct before, value) == offsetof(struct after, value), \
               "member packing restored")
ABI_PACKING(CallerPackingBefore, CallerPackingAfter);
ABI_PACKING(NativePackingBefore, NativePackingAfter);

ABI_ASSERT(PLANK_CLIPBOARD_WIRE_MAGIC == 0x504c4342U, "clipboard magic");
ABI_ASSERT(PLANK_CLIPBOARD_WIRE_VERSION == 1U, "clipboard wire version");
ABI_ASSERT(PLANK_CLIPBOARD_FLAG_FIRST_CHUNK == 1U, "first chunk flag");
ABI_ASSERT(PLANK_CLIPBOARD_FLAG_LAST_CHUNK == 2U, "last chunk flag");
ABI_ASSERT(PLANK_CLIPBOARD_MAX_TEXT_SIZE == 512U * 1024U, "512 KiB text limit");
ABI_ASSERT(PLANK_CLIPBOARD_MAX_EVENT_CHUNK_SIZE == 48U * 1024U, "event chunk limit");
ABI_ASSERT(PLANK_CLIPBOARD_MAX_INPUT_CHUNK_SIZE == 8160U, "input chunk limit");
ABI_ASSERT(sizeof(PLANK_CLIPBOARD_WIRE_HEADER) + PLANK_CLIPBOARD_MAX_INPUT_CHUNK_SIZE
               == 8192U, "clipboard input packet size");
ABI_ASSERT(sizeof(PLANK_CLIPBOARD_WIRE_HEADER) + PLANK_CLIPBOARD_MAX_INPUT_CHUNK_SIZE
               == PLANK_TRANSPORT_INPUT_MAX_PAYLOAD_SIZE, "transport input payload limit");

int main(void) {
    puts("clipboard_wire_abi=pass header_bytes=32 packing_restored=1 input_payload_bytes=8192");
    return 0;
}
