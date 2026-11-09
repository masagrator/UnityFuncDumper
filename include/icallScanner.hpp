#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// Fallback search of Unity internal calls (icalls) for builds where the
// name/function pointer arrays can't be found from reference strings.
// Scans .text for calls to the icall register function:
//  - direct:  ADRP X0, name ; ADD X0 ... ; ADRP X1, func ; ADD/LDR X1 ... ; BL register
//  - loop:    LDR X0, [names, Xi] ; LDR X1, [funcs, Xi] ; BL register ; ADD Xc, #step ; CMP Xc, #end
// Register function is the BL target with most "Unity...::..." name arguments.

typedef int (*ICallReadFn)(uint64_t address, void* buffer, size_t size);

struct ICallEntry {
	std::string name;
	uint64_t offset; // function offset relative to main base
};

bool scanInternalCalls(uint64_t base, size_t textSize, uint64_t moduleEnd, ICallReadFn read,
                       std::vector<ICallEntry>& out, int (*log)(const char*, ...) = nullptr);
