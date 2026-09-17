# Native input queue regression tests

These tests run the actual common-C input worker with an in-memory sender.
They do not open sockets, access input devices, or connect to a Host.
Production builds leave `PLANK_BUILD_TESTS=OFF` (the default).

From this repository, with `PLANK_ROOT` pointing to a current PLANK root checkout:

```sh
cmake -S . -B build/input-tests \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=OFF \
  -DPLANK_BUILD_TESTS=ON \
  -DPLANK_TRANSPORT_DIR="$PLANK_ROOT/protocol/plank-transport"
cmake --build build/input-tests --parallel
ctest --test-dir build/input-tests --output-on-failure
ctest --test-dir build/input-tests --repeat until-fail:100 --output-on-failure
```

For Clang AddressSanitizer/UndefinedBehaviorSanitizer, use a separate build
directory and add `-DCMAKE_C_COMPILER=clang` and
`-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`.

- `native-input-wire.c`: coordinates, button/key barriers, modifiers, scrolling,
  150-move saturation followed by releases, and pen tip/button/motion barriers.
  The original ordering/saturation fixture is from Christopher Noellert's
  PLANK integration PR #4 (`tests/session/native-input-wire.c`, blob
  `e3721d1ecc9562e36f601420d30a2f66e56b2a32`); it lives beside the input worker
  here so these regressions can be tested without a whole Client build.
- `queue-ownership.c`: empty, matching/nonmatching heads, barriers, full-queue
  replacement, link/count integrity, shutdown/drain, and concurrent replacement
  while the consumer inspects and acquires ownership.
- `native-input-replacement.c`: deterministic interleaving after the pen worker
  inspects a pending absolute position, while the producer replaces its holder
  and recycles it into a subsequent pen event. Both pen events and the absolute
  position must survive. Linux static-library tests use ELF linker wrappers,
  without runtime test hooks or changing any queue data.

The replacement fixture also supports the pre-fix worker as a negative control:
compile it against PR head `b2b2b290bb5aa8e473cfffacb39bffc271ada7a4`, omitting
`PLANK_TEST_MATCHED_POLL` and wrapping only `LbqPeekQueueElement`. It reports
`absolute=0 pen=1` and fails. Against the repaired worker it reports
`absolute=1 pen=2` and passes. The old worker returns a borrowed pointer from
peek; the repaired worker atomically checks and removes a matching head, and
returns no borrowed pointer for a nonmatching barrier. The wrappers pause
after either operation, allowing the same producer interleaving in both cases.

Standalone tests are not full Linux/macOS Client or physical Wacom acceptance.
They do not justify changing the Client's selected common-C gitlink or merging
the contribution's other feature branches without their own review.
