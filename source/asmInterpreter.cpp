#include "asmInterpreter.hpp"
#include "UnityDumps.hpp"

void wrongOperand(ad_insn* insn) {
	printf("Unsupported instruction! %s\n", insn -> decoded);
	printf("Operands:\n");
	for (int i = 0; i < insn -> num_operands; i++) {
		printf("%d. type: ", i);
		if (insn -> operands[i].type == AD_OP_REG) {
			printf("REG: %s\n", insn -> operands[i].op_reg.rtbl[0]);
		}
		else if (insn -> operands[i].type == AD_OP_IMM) {
			printf("IMM: 0x%lx\n", insn -> operands[i].op_imm.bits);
		}
		else if (insn -> operands[i].type == AD_OP_SHIFT) {
			printf("SHIFT: type: %d, amt: %d\n", insn -> operands[i].op_shift.type, insn -> operands[i].op_shift.amt);
		}
		else printf("MEM\n");
	}
	ArmadilloDone(&insn);
}

MachineState machineState_copy = {0};
int registerPointer = 0;
long immediate_offset = 0;

void dumpPointers(const std::vector<std::string> UnityNames, const std::vector<uint32_t> UnityOffsets, const DmntCheatProcessMetadata cheatMetadata) {
	ad_insn *insn = NULL;
	MachineState machineState = {0};
	for (size_t i = 0; i < unityDataStruct.size(); i++) {
		auto itr = std::find(UnityNames.begin(), UnityNames.end(), unityDataStruct[i].search_name);
		if (itr == UnityNames.end()) {
			printf("%s was not found!\n", unityDataStruct[i].search_name);
			consoleUpdate(NULL);
			continue;
		}
		else {
			printf("Searching: %s...\n", unityDataStruct[i].search_name);
			consoleUpdate(NULL);
		}
		size_t index = itr - UnityNames.begin();
		uint64_t start_address = cheatMetadata.main_nso_extents.base + UnityOffsets[index];
		uint32_t instruction = 0;
		std::vector<uint64_t> returns;
		while(true) {
			dmntchtReadCheatProcessMemory(start_address, (void*)&instruction, sizeof(uint32_t));
			if (returns.size() == 0 && instruction == 0xD65F03C0)
				break;
			if (insn) {
				ArmadilloDone(&insn);
			}
			int rc = ArmadilloDisassemble(instruction, start_address, &insn);
			if (rc) {
				printf("Disassembler error! 0x%x/%d\n", rc, rc);
				ArmadilloDone(&insn);
				return;
			}
			//else printf("%s\n", insn -> decoded);
			switch(insn -> instr_id) {
				case AD_INSTR_ADD: {
					if (insn -> operands[2].type == AD_OP_REG) {
						machineState.X[insn -> operands[0].op_reg.rn] = machineState.X[insn -> operands[1].op_reg.rn] + machineState.X[insn -> operands[2].op_reg.rn];
					}
					else if (insn -> operands[2].type == AD_OP_IMM) {
						machineState.X[insn -> operands[0].op_reg.rn] = machineState.X[insn -> operands[1].op_reg.rn] + insn -> operands[2].op_imm.bits;
					}
					else {
						wrongOperand(insn);
						return;						
					}
					break;
				}
				case AD_INSTR_ADRP: {
					machineState.X[insn -> operands[0].op_reg.rn] = insn -> operands[1].op_imm.bits;
					break;
				}
				case AD_INSTR_B: {
					start_address = insn -> operands[0].op_imm.bits - 4;
					break;
				}
				case AD_INSTR_BL: {
					if (insn -> num_operands != 1) {
						wrongOperand(insn);
						return;									
					}
					returns.push_back(start_address);
					start_address = insn -> operands[0].op_imm.bits - 4;
					break;
				}
				case AD_INSTR_LDP: {
					break;
				}
				case AD_INSTR_LDR: {
					if (insn -> num_operands == 4) {
						if (insn -> operands[0].op_reg.fp) {
							if ((insn -> operands[3].type != AD_OP_IMM) || (insn -> operands[3].op_imm.bits < 0)) {
								wrongOperand(insn);
								return;
							}
							if (insn -> operands[0].op_reg.rtbl[0][0] == 's') {
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn] + (machineState.X[insn -> operands[2].op_reg.rn] << insn -> operands[3].op_imm.bits), (void*)&machineState.S[insn -> operands[0].op_reg.rn], sizeof(uint64_t));
							}
							else if (insn -> operands[0].op_reg.rtbl[0][0] == 'd') {
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn] + (machineState.X[insn -> operands[2].op_reg.rn] << insn -> operands[3].op_imm.bits), (void*)&machineState.D[insn -> operands[0].op_reg.rn], sizeof(uint64_t));
							}
							else {
								wrongOperand(insn);
								return;
							}
						}
						else {
							if ((insn -> operands[3].type != AD_OP_IMM) || (insn -> operands[3].op_imm.bits < 0)) {
								wrongOperand(insn);
								return;
							}
							if (insn -> operands[0].op_reg.rtbl[0][0] == 'w') {
								machineState.X[insn -> operands[0].op_reg.rn] = 0;
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn] + (machineState.X[insn -> operands[2].op_reg.rn] << insn -> operands[3].op_imm.bits), (void*)&machineState.X[insn -> operands[0].op_reg.rn], sizeof(uint32_t));
							}
							else if (insn -> operands[0].op_reg.rtbl[0][0] == 'x')  {
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn] + (machineState.X[insn -> operands[2].op_reg.rn] << insn -> operands[3].op_imm.bits), (void*)&machineState.X[insn -> operands[0].op_reg.rn], sizeof(uint64_t));
							}
							else {
								wrongOperand(insn);
								return;
							}
						}
					}
					
					else if (insn -> num_operands == 3) {
						if (insn -> operands[0].op_reg.fp) {
							if (insn -> operands[0].op_reg.rtbl[0][0] == 's') {
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn] + (insn -> operands[2].op_imm.bits), (void*)&machineState.S[insn -> operands[0].op_reg.rn], sizeof(uint64_t));
								if (insn -> operands[0].op_reg.rtbl[0][0] == unityDataStruct[i].data_type && insn -> operands[0].op_reg.rn == unityDataStruct[i].data_register_number) {
									memcpy(&machineState_copy, &machineState, sizeof(machineState_copy));
									registerPointer = insn -> operands[1].op_reg.rn;
									immediate_offset = insn -> operands[2].op_imm.bits;
								}
							}
							else if (insn -> operands[0].op_reg.rtbl[0][0] == 'd') {
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn] + (insn -> operands[2].op_imm.bits), (void*)&machineState.D[insn -> operands[0].op_reg.rn], sizeof(uint64_t));
								if (insn -> operands[0].op_reg.rtbl[0][0] == unityDataStruct[i].data_type && insn -> operands[0].op_reg.rn == unityDataStruct[i].data_register_number) {
									memcpy(&machineState_copy, &machineState, sizeof(machineState_copy));
									registerPointer = insn -> operands[1].op_reg.rn;
									immediate_offset = insn -> operands[2].op_imm.bits;
								}
							}
							else {
								wrongOperand(insn);
								return;
							}
						}
						else {
							if (insn -> operands[0].op_reg.rtbl[0][0] == 'w') {
								machineState.X[insn -> operands[0].op_reg.rn] = 0;
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn] + (insn -> operands[2].op_imm.bits), (void*)&machineState.X[insn -> operands[0].op_reg.rn], sizeof(uint32_t));
								if (insn -> operands[0].op_reg.rtbl[0][0] == unityDataStruct[i].data_type && insn -> operands[0].op_reg.rn == unityDataStruct[i].data_register_number) {
									memcpy(&machineState_copy, &machineState, sizeof(machineState_copy));
									registerPointer = insn -> operands[1].op_reg.rn;
									immediate_offset = insn -> operands[2].op_imm.bits;
								}
							}
							else if (insn -> operands[0].op_reg.rtbl[0][0] == 'x')  {
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn] + (insn -> operands[2].op_imm.bits), (void*)&machineState.X[insn -> operands[0].op_reg.rn], sizeof(uint64_t));
								if (insn -> operands[0].op_reg.rtbl[0][0] == unityDataStruct[i].data_type && insn -> operands[0].op_reg.rn == unityDataStruct[i].data_register_number) {
									memcpy(&machineState_copy, &machineState, sizeof(machineState_copy));
									registerPointer = insn -> operands[1].op_reg.rn;
									immediate_offset = insn -> operands[2].op_imm.bits;
								}
							}
							else {
								wrongOperand(insn);
								return;
							}
						}
					}
					else if (insn -> num_operands == 2) {
						if (insn -> operands[0].op_reg.fp) {
							if (insn -> operands[0].op_reg.rtbl[0][0] == 's') {
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn], (void*)&machineState.S[insn -> operands[0].op_reg.rn], sizeof(uint64_t));
								if (insn -> operands[0].op_reg.rtbl[0][0] == unityDataStruct[i].data_type && insn -> operands[0].op_reg.rn == unityDataStruct[i].data_register_number) {
									memcpy(&machineState_copy, &machineState, sizeof(machineState_copy));
									registerPointer = insn -> operands[1].op_reg.rn;
									immediate_offset = 0;
								}
							}
							else if (insn -> operands[0].op_reg.rtbl[0][0] == 'd') {
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn], (void*)&machineState.D[insn -> operands[0].op_reg.rn], sizeof(uint64_t));
								if (insn -> operands[0].op_reg.rtbl[0][0] == unityDataStruct[i].data_type && insn -> operands[0].op_reg.rn == unityDataStruct[i].data_register_number) {
									memcpy(&machineState_copy, &machineState, sizeof(machineState_copy));
									registerPointer = insn -> operands[1].op_reg.rn;
									immediate_offset = 0;
								}
							}
							else {
								wrongOperand(insn);
								return;
							}
						}
						else {
							if (insn -> operands[0].op_reg.rtbl[0][0] == 'w') {
								machineState.X[insn -> operands[0].op_reg.rn] = 0;
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn], (void*)&machineState.X[insn -> operands[0].op_reg.rn], sizeof(uint32_t));
								if (insn -> operands[0].op_reg.rtbl[0][0] == unityDataStruct[i].data_type && insn -> operands[0].op_reg.rn == unityDataStruct[i].data_register_number) {
									memcpy(&machineState_copy, &machineState, sizeof(machineState_copy));
									registerPointer = insn -> operands[1].op_reg.rn;
									immediate_offset = 0;
								}
							}
							else if (insn -> operands[0].op_reg.rtbl[0][0] == 'x') {
								dmntchtReadCheatProcessMemory((uint64_t)machineState.X[insn -> operands[1].op_reg.rn], (void*)&machineState.X[insn -> operands[0].op_reg.rn], sizeof(uint64_t));
								if (insn -> operands[0].op_reg.rtbl[0][0] == unityDataStruct[i].data_type && insn -> operands[0].op_reg.rn == unityDataStruct[i].data_register_number) {
									memcpy(&machineState_copy, &machineState, sizeof(machineState_copy));
									registerPointer = insn -> operands[1].op_reg.rn;
									immediate_offset = 0;
								}
							}
							else {
								wrongOperand(insn);
								return;
							}
						}
					}
					else {
						wrongOperand(insn);
						return;		
					}
					break;
				}
				case AD_INSTR_MOV: {
					if (insn -> num_operands != 2) {
						wrongOperand(insn);
						return;
					}
					if (insn -> operands[1].type == AD_OP_REG) {
						machineState.X[insn -> operands[0].op_reg.rn] = machineState.X[insn -> operands[1].op_reg.rn];
					}
					else if (insn -> operands[1].type == AD_OP_IMM) {
						machineState.X[insn -> operands[0].op_reg.rn] = insn -> operands[1].op_imm.bits;
					}
					else {
						wrongOperand(insn);
						return;					
					}
					break;
				}
				case AD_INSTR_RET: {
					start_address = returns.back();
					returns.pop_back();
					break;
				}
				case AD_INSTR_STP: {
					break;
				}
				default: {
					wrongOperand(insn);
					return;
				}
			}
			start_address += 4;
		}
		printf("%s: Register %c%d=", unityDataStruct[i].output_name, unityDataStruct[i].data_type, unityDataStruct[i].data_register_number);
		switch(unityDataStruct[i].data_type) {
			case 'w':
				printf("%d", (uint32_t)machineState_copy.X[registerPointer]);
				break;
			case 'x':
				printf("%ld", machineState_copy.X[registerPointer]);
				break;
			case 's':
				printf("%f", machineState_copy.S[registerPointer]);
				break;
			case 'd':
				printf("%f", machineState_copy.D[registerPointer]);
				break;
		}
		uint64_t address_to_search = machineState_copy.X[registerPointer];
		uint64_t mappings_count = 0;
		dmntchtGetCheatProcessMappingCount(&mappings_count);
		MemoryInfo* memoryInfoBuffers = new MemoryInfo[mappings_count];
		dmntchtGetCheatProcessMappings(memoryInfoBuffers, mappings_count, 0, &mappings_count);
		for (size_t x = 0; x < mappings_count; x++) {
			if (((memoryInfoBuffers[x].perm & Perm_Rw) != Perm_Rw) || ((memoryInfoBuffers[x].type != MemType_CodeMutable) && (memoryInfoBuffers[x].type != MemType_CodeWritable))) {
				continue;
			}
			if (memoryInfoBuffers[x].size > 200'000'000) {
				continue;
			}
			uint64_t* buffer = new uint64_t[memoryInfoBuffers[x].size / sizeof(uint64_t)];
			dmntchtReadCheatProcessMemory(memoryInfoBuffers[x].addr, (void*)buffer, memoryInfoBuffers[x].size);
			uint64_t* result = 0;
			for (size_t y = 0; y < memoryInfoBuffers[x].size / sizeof(uint64_t); y++) {
				if (buffer[y] == (uint64_t)address_to_search) {
					result = (uint64_t*)(memoryInfoBuffers[x].addr + (sizeof(uint64_t) * y));
					break;
				}
			}
			delete[] buffer;
			if (result) {
				printf(", offset: 0x%lx", (uint64_t)result - cheatMetadata.main_nso_extents.base);
				if (immediate_offset != 0) printf(" + 0x%lx", immediate_offset);
				break;
			}
		}
		printf("\n");
		if (insn)
			ArmadilloDone(&insn);

		delete[] memoryInfoBuffers;
	}
}