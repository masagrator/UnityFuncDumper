#pragma once
#include <array>
#include <cstdint>

struct MachineState {
    long X[30];
    float S[31];
    double D[31];
};

struct UnityData {
    const char* search_name;
    const char* output_name;
    char data_type; // W (int), X (int64), S (float), D (double)
    uint8_t data_register_number;
};

std::array unityDataStruct {
    UnityData{"UnityEngine.Time::get_fixedDeltaTime", "UnityEngine.Time::fixedDeltaTime", 's', 0}
};