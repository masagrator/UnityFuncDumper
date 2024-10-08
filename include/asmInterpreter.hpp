#pragma once
#include <vector>
#include <cstddef>
#include <string>
#include <cstring>
#include <algorithm>
#include "dmntcht.h"
extern "C" {
#include "armadillo.h"
#include "strext.h"
}

void dumpPointers(std::vector<std::string> UnityNames, std::vector<uint32_t> UnityOffsets, DmntCheatProcessMetadata cheatMetadata, std::string unity_sdk);