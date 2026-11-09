#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// Runtime dumper of IL2CPP "<Type>_TypeInfo" metadata usage slots (il2cpp v27+).
// Doesn't need global-metadata.dat from romfs and doesn't parse metadata format:
//  - slots live in one contiguous block in main's .data; uninitialized slot holds
//    (1 << 29) | (typeIndex << 1) | 1, initialized slot holds Il2CppClass*
//  - types[] taken from Il2CppMetadataRegistration found in main's .data
//  - names come from runtime Il2CppClass / Il2CppType / Il2CppGenericClass structures;
//    from metadata in RAM only nameIndex/namespaceIndex (first two u32 of type definition)
//    are read, string base and declaringTypeIndex field are calibrated from loaded classes.
// Names follow Il2CppDumper's script.json convention.

typedef int (*TypeInfoReadFn)(uint64_t address, void* buffer, size_t size);

struct TypeInfoEntry {
	uint64_t offset;     // slot offset relative to main base
	std::string name;    // e.g. "DynamicResolution_TypeInfo"
	bool initialized;    // slot already holds Il2CppClass* (pointer is usable)
};

// base      - main base
// dataStart - start of main's RW segment (.data)
// bssStart  - start of main's .bss (from MOD0)
// moduleEnd - end of main's RW segment (.data + .bss)
bool dumpTypeInfos(uint64_t base, uint64_t dataStart, uint64_t bssStart, uint64_t moduleEnd,
                   TypeInfoReadFn read, std::vector<TypeInfoEntry>& out,
                   int (*log)(const char*, ...) = nullptr);
