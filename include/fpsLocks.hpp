#pragma once
#include <cstdint>
#include <cstddef>

// Unity 6 vsync / FPS lock finder.
//
// Chain:
//   1. Resolve PLT thunks of nn::vi::GetDisplayVsyncEvent, nn::os::SignalEvent,
//      nn::os::WaitSystemEvent through MOD0 -> .dynamic -> JMPREL/DYNSYM.
//   2. Single BL to GetDisplayVsyncEvent thunk -> resolve X0 = vsync SystemEventType.
//   3. Find "BL SignalEvent ... BL WaitSystemEvent(X0 == vsync event)" followed by
//      "LDR Wn, [Xm, #imm] ; CBZ/CBNZ/CMP Wn" -> Xm+imm = vsync lock (int32).
//   4. Find "STR Wx, [vsync lock]" followed by "MOV Wy, #60.0f ; FDIV Sd ; STR Sd, [Xz]"
//      -> Xz = FPS lock (float, 1/60 by default = 0x3C888889).
//      Fallback: scan forward from vsync lock for 0x3C888889.

// Reads `size` bytes from target memory at absolute `address`. Returns 0 on success.
typedef int (*FpsLockReadFn)(uint64_t address, void* buffer, size_t size);

struct FpsLockResult {
	bool     thunksFound;
	uint64_t thunkVsyncEvent;   // offsets relative to main base
	uint64_t thunkSignalEvent;
	uint64_t thunkWaitSystemEvent;
	uint64_t vsyncEventCall;    // BL to GetDisplayVsyncEvent
	uint64_t vsyncEvent;        // X0 passed to GetDisplayVsyncEvent
	uint64_t waitLoop;          // BL WaitSystemEvent inside vsync wait loop
	bool     vsyncLockFound;
	uint64_t vsyncLock;
	bool     fpsLockFound;
	bool     fpsLockFromPattern; // false = found by value scan fallback
	uint64_t fpsLock;
};

// base     - main NSO base address in target memory
// textSize - size of main's .text (R-X mapping at base)
// log      - optional printf-like logger (may be nullptr)
bool findFpsLocks(uint64_t base, size_t textSize, FpsLockReadFn read, FpsLockResult* out,
                  int (*log)(const char*, ...) = nullptr);
