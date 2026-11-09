#include "fpsLocks.hpp"
#include <cstring>
#include <cstdlib>
#include <vector>
#include <map>

namespace {

const char* const SYM_VSYNC  = "_ZN2nn2vi20GetDisplayVsyncEventEPNS_2os15SystemEventTypeEPNS0_7DisplayE";
const char* const SYM_SIGNAL = "_ZN2nn2os11SignalEventEPNS0_9EventTypeE";
const char* const SYM_WAIT   = "_ZN2nn2os15WaitSystemEventEPNS0_15SystemEventTypeE";

const uint32_t FLOAT_60      = 0x42700000; // 60.0f
const uint32_t FLOAT_1_60    = 0x3C888889; // 1/60.0f

// .text is scanned in windows to keep memory usage low (some games have ~180 MB of code)
const size_t WIN    = 0x200000; // instructions per window (8 MB)
const size_t MARGIN = 0x400;    // extra instructions before/after window for look-back/ahead

struct Ctx {
	uint64_t base;
	FpsLockReadFn read;
	size_t count; // instructions in text
	int (*log)(const char*, ...);
	std::vector<uint32_t> win;
	size_t winStart = 0;
	bool readFailed = false;

	// Instruction at absolute index j; 0 (udf) outside loaded window
	uint32_t at(size_t j) const {
		return (j >= winStart && j - winStart < win.size()) ? win[j - winStart] : 0;
	}
	// Loads window covering [w, w + WIN) plus margins
	bool load(size_t w) {
		size_t from = w > MARGIN ? w - MARGIN : 0;
		size_t to = w + WIN + MARGIN < count ? w + WIN + MARGIN : count;
		if (winStart == from && win.size() == to - from) return true;
		win.resize(to - from);
		winStart = from;
		if (read(base + from * 4, win.data(), (to - from) * 4)) {
			win.clear();
			readFailed = true;
			return false;
		}
		return true;
	}
};

#define LOG(...) do { if (c.log) c.log(__VA_ARGS__); } while (0)

inline uint32_t rd(uint32_t i) { return i & 31; }
inline uint32_t rn(uint32_t i) { return (i >> 5) & 31; }

inline int64_t sext(uint64_t v, int bits) {
	uint64_t m = 1ULL << (bits - 1);
	return (int64_t)((v ^ m) - m);
}

inline bool isADRP(uint32_t i) { return (i & 0x9F000000) == 0x90000000; }
inline bool isADR(uint32_t i)  { return (i & 0x9F000000) == 0x10000000; }
inline uint64_t adrTarget(uint32_t i, uint64_t pc) {
	uint64_t imm = ((i >> 29) & 3) | (((uint64_t)(i >> 5) & 0x7FFFF) << 2);
	if (isADRP(i)) return (pc & ~0xFFFULL) + (sext(imm, 21) << 12);
	return pc + sext(imm, 21);
}
// ADD Xd, Xn, #imm{, lsl #12}
inline bool isADDimm64(uint32_t i) { return (i & 0xFF800000) == 0x91000000; }
inline uint64_t addImm(uint32_t i) { return (uint64_t)((i >> 10) & 0xFFF) << (((i >> 22) & 1) ? 12 : 0); }
// LDR Xt, [Xn, #uimm]
inline bool isLDRx(uint32_t i) { return (i & 0xFFC00000) == 0xF9400000; }
// LDR Wt, [Xn, #uimm]
inline bool isLDRw(uint32_t i) { return (i & 0xFFC00000) == 0xB9400000; }
// STR Wt, [Xn, #uimm]
inline bool isSTRw(uint32_t i) { return (i & 0xFFC00000) == 0xB9000000; }
// STUR Wt, [Xn, #simm]
inline bool isSTURw(uint32_t i) { return (i & 0xFFE00C00) == 0xB8000000; }
// STR St, [Xn, #uimm]
inline bool isSTRs(uint32_t i) { return (i & 0xFFC00000) == 0xBD000000; }
// STUR St, [Xn, #simm]
inline bool isSTURs(uint32_t i) { return (i & 0xFFE00C00) == 0xBC000000; }
inline int64_t uimm(uint32_t i, int scale) { return (int64_t)((i >> 10) & 0xFFF) * scale; }
inline int64_t simm9(uint32_t i) { return sext((i >> 12) & 0x1FF, 9); }
// MOV Xd, Xm (ORR Xd, XZR, Xm)
inline bool isMOVx(uint32_t i) { return (i & 0xFFE0FFE0) == 0xAA0003E0; }
inline bool isBL(uint32_t i) { return (i & 0xFC000000) == 0x94000000; }
inline uint64_t blTarget(uint32_t i, uint64_t pc) { return pc + (sext(i & 0x3FFFFFF, 26) << 2); }
// CBZ/CBNZ Wt
inline bool isCBw(uint32_t i) { return (i & 0xFE000000) == 0x34000000; }
// CMP Wn, #imm  /  CMP Wn, Wm
inline bool isCMPw(uint32_t i) { return (i & 0xFF80001F) == 0x7100001F || (i & 0xFF20001F) == 0x6B00001F; }
// MOV Wd, #0x42700000 (MOVZ Wd, #0x4270, LSL #16)
inline bool isMOV60f(uint32_t i) { return (i & 0xFFFFFFE0) == 0x52A84E00; }
// FMOV Sd, Wn
inline bool isFMOVsw(uint32_t i) { return (i & 0xFFFFFC00) == 0x1E270000; }
// FDIV Sd, Sn, Sm
inline bool isFDIVs(uint32_t i) { return (i & 0xFFE0FC00) == 0x1E201800; }

inline bool isLoadStore(uint32_t i) { return (i & 0x0A000000) == 0x08000000; }
inline bool isBranch(uint32_t i) { return (i & 0x1C000000) == 0x14000000; }

uint64_t pcOf(const Ctx&, size_t idx) { return idx * 4; }

// Walks backwards from instruction idx (exclusive) and computes value of `reg`
// as an offset relative to main base. Handles ADRP/ADR + ADD/LDR(GOT) chains
// and MOV Xd, Xm. Returns false if anything unexpected writes to the register.
bool resolveReg(const Ctx& c, size_t idx, uint32_t reg, size_t maxBack, uint64_t* out) {
	struct Op { bool load; int64_t imm; };
	Op ops[16];
	int nops = 0;
	size_t stop = idx > maxBack ? idx - maxBack : 0;
	for (size_t j = idx; j-- > stop; ) {
		uint32_t i = c.at(j);
		if (isADRP(i) || isADR(i)) {
			if (rd(i) != reg) continue;
			uint64_t v = adrTarget(i, pcOf(c, j));
			while (nops > 0) {
				Op& op = ops[--nops];
				v += op.imm;
				if (op.load) {
					uint64_t ptr = 0;
					if (c.read(c.base + v, &ptr, 8)) return false;
					// GOT holds absolute runtime pointers; normalize to offset
					v = ptr >= c.base ? ptr - c.base : ptr;
				}
			}
			*out = v;
			return true;
		}
		if (isADDimm64(i) && rd(i) == reg) {
			if (nops == 16) return false;
			ops[nops++] = {false, (int64_t)addImm(i)};
			reg = rn(i);
			continue;
		}
		if (isLDRx(i) && rd(i) == reg) {
			if (nops == 16) return false;
			ops[nops++] = {true, uimm(i, 8)};
			reg = rn(i);
			continue;
		}
		if (isMOVx(i) && rd(i) == reg) {
			reg = (i >> 16) & 31;
			continue;
		}
		if (isBranch(i)) {
			// BL/BLR clobber caller-saved registers
			bool call = isBL(i) || (i & 0xFFFFFC1F) == 0xD63F0000;
			if (call && reg < 19) return false;
			continue;
		}
		if (rd(i) != reg) continue;
		// FP/SIMD data processing writes V registers, except moves/conversions to general registers
		if ((i & 0x0E000000) == 0x0E000000) {
			bool fpToInt = (i & 0x5F203C00) == 0x1E200000 && ((i >> 16) & 7) != 2 && ((i >> 16) & 7) != 3 && ((i >> 16) & 7) != 7;
			bool simdToInt = (i & 0x9FE08400) == 0x0E000400 && (((i >> 11) & 0xF) == 0x5 || ((i >> 11) & 0xF) == 0x7); // SMOV/UMOV
			if (!fpToInt && !simdToInt) continue;
			return false;
		}
		if (isLoadStore(i)) {
			if ((i >> 26) & 1) continue; // load/store of FP/SIMD register
			// store: Rt is a source -> fine. load: writes Rt -> unknown value
			bool load = (i >> 22) & 1;
			if (!load) continue;
			return false;
		}
		if (isCMPw(i)) continue;
		return false;
	}
	return false;
}

bool readStr(const Ctx& c, uint64_t addr, char* buf, size_t size) {
	if (c.read(addr, buf, size)) return false;
	buf[size - 1] = 0;
	return true;
}

bool findThunks(Ctx& c, FpsLockResult* r) {
	uint32_t mod0 = 0;
	if (c.read(c.base + 4, &mod0, 4)) return false;
	int32_t dynRel = 0;
	if (c.read(c.base + mod0 + 4, &dynRel, 4)) return false;
	uint64_t dyn = c.base + mod0 + dynRel;

	uint64_t symtab = 0, strtab = 0, jmprel = 0, pltsz = 0, syment = 24;
	for (int k = 0; k < 128; k++) {
		int64_t ent[2];
		if (c.read(dyn + k * 16, ent, 16)) return false;
		if (ent[0] == 0) break;
		uint64_t v = (uint64_t)ent[1];
		if (v >= c.base) v -= c.base;
		switch (ent[0]) {
			case 6:  symtab = v; break;
			case 5:  strtab = v; break;
			case 23: jmprel = v; break;
			case 2:  pltsz  = v; break;
			case 11: syment = v; break;
		}
	}
	if (!symtab || !strtab || !jmprel || !pltsz) {
		LOG("FPS: dynamic section incomplete\n");
		return false;
	}

	std::vector<uint64_t> rela(pltsz / 8);
	if (c.read(c.base + jmprel, rela.data(), rela.size() * 8)) return false;

	uint64_t gotVsync = 0, gotSignal = 0, gotWait = 0;
	size_t nameLen = strlen(SYM_VSYNC) + 1;
	char name[128];
	for (size_t k = 0; k + 2 < rela.size(); k += 3) {
		uint64_t off = rela[k], info = rela[k + 1];
		uint32_t sym = info >> 32;
		if (!sym) continue;
		uint32_t st_name = 0;
		if (c.read(c.base + symtab + sym * syment, &st_name, 4)) continue;
		if (!readStr(c, c.base + strtab + st_name, name, nameLen + 1)) continue;
		if (!strcmp(name, SYM_VSYNC))  gotVsync  = off;
		else if (!strcmp(name, SYM_SIGNAL)) gotSignal = off;
		else if (!strcmp(name, SYM_WAIT))   gotWait   = off;
	}
	if (!gotVsync || !gotSignal || !gotWait) {
		LOG("FPS: imports not found (vsync %d, signal %d, wait %d)\n", !!gotVsync, !!gotSignal, !!gotWait);
		return false;
	}

	// PLT stub: ADRP X16, page ; LDR X17, [X16, #off] ; ADD X16, X16, #off ; BR X17
	for (size_t w = 0; w < c.count; w += WIN) {
	if (!c.load(w)) return false;
	size_t wEnd = w + WIN < c.count ? w + WIN : c.count;
	for (size_t j = w; j < wEnd && j + 3 < c.count; j++) {
		uint32_t i = c.at(j);
		if (!isADRP(i) || rd(i) != 16) continue;
		uint32_t i2 = c.at(j + 1);
		if ((i2 & 0xFFC003FF) != 0xF9400211 || c.at(j + 3) != 0xD61F0220) continue;
		uint64_t slot = adrTarget(i, pcOf(c, j)) + uimm(i2, 8);
		if (slot == gotVsync)  r->thunkVsyncEvent = pcOf(c, j);
		if (slot == gotSignal) r->thunkSignalEvent = pcOf(c, j);
		if (slot == gotWait)   r->thunkWaitSystemEvent = pcOf(c, j);
	}
	}
	return r->thunkVsyncEvent && r->thunkSignalEvent && r->thunkWaitSystemEvent;
}

bool findVsyncLock(Ctx& c, FpsLockResult* r) {
	// 1. BL to GetDisplayVsyncEvent thunk, resolve X0
	size_t calls = 0;
	for (size_t w = 0; w < c.count; w += WIN) {
	if (!c.load(w)) return false;
	size_t wEnd = w + WIN < c.count ? w + WIN : c.count;
	for (size_t j = w; j < wEnd; j++) {
		uint32_t i = c.at(j);
		if (!isBL(i) || blTarget(i, pcOf(c, j)) != r->thunkVsyncEvent) continue;
		calls++;
		uint64_t ev = 0;
		if (!r->vsyncEvent && resolveReg(c, j, 0, 64, &ev)) {
			r->vsyncEventCall = pcOf(c, j);
			r->vsyncEvent = ev;
		}
	}
	}
	if (calls != 1) LOG("FPS: warning, %ld calls to GetDisplayVsyncEvent\n", (long)calls);
	if (!r->vsyncEvent) {
		LOG("FPS: couldn't resolve X0 of GetDisplayVsyncEvent\n");
		return false;
	}

	// 2. BL SignalEvent ... BL WaitSystemEvent(vsyncEvent) ; LDR Wn, [Xm, #imm] ; CBZ/CMP Wn
	for (size_t w = 0; w < c.count; w += WIN) {
	if (!c.load(w)) return false;
	size_t wEnd = w + WIN < c.count ? w + WIN : c.count;
	for (size_t j = w; j < wEnd && j + 8 < c.count; j++) {
		uint32_t i = c.at(j);
		if (!isBL(i) || blTarget(i, pcOf(c, j)) != r->thunkWaitSystemEvent) continue;
		uint64_t ev = 0;
		if (!resolveReg(c, j, 0, 32, &ev) || ev != r->vsyncEvent) continue;
		bool signal = false;
		for (size_t k = (j > 8 ? j - 8 : 0); k < j; k++)
			if (isBL(c.at(k)) && blTarget(c.at(k), pcOf(c, k)) == r->thunkSignalEvent) signal = true;
		if (!signal) continue;
		for (size_t k = j + 1; k < j + 5; k++) {
			uint32_t ld = c.at(k);
			if (!isLDRw(ld)) continue;
			bool tested = false;
			for (size_t t = k + 1; t < k + 4; t++) {
				uint32_t ti = c.at(t);
				if ((isCBw(ti) && rd(ti) == rd(ld)) || (isCMPw(ti) && rn(ti) == rd(ld))) tested = true;
			}
			if (!tested) continue;
			uint64_t ptr = 0;
			if (!resolveReg(c, k, rn(ld), 64, &ptr)) continue;
			r->waitLoop = pcOf(c, j);
			r->vsyncLock = ptr + uimm(ld, 4);
			r->vsyncLockFound = true;
			return true;
		}
	}
	}
	LOG("FPS: vsync wait loop not found\n");
	return false;
}

// Returns address written by STR Wx at idx, if it's a store to memory.
bool storeTarget(const Ctx& c, size_t idx, bool fp, uint64_t* out) {
	uint32_t i = c.at(idx);
	int64_t off;
	if (fp ? isSTRs(i) : isSTRw(i)) off = uimm(i, 4);
	else if (fp ? isSTURs(i) : isSTURw(i)) off = simm9(i);
	else return false;
	uint64_t b = 0;
	if (!resolveReg(c, idx, rn(i), 32, &b)) return false;
	*out = b + off;
	return true;
}

bool findFpsLock(Ctx& c, FpsLockResult* r) {
	// fpsLock = (float)vsync / 60.0f
	std::map<uint64_t, int> votes;
	for (size_t w = 0; w < c.count; w += WIN) {
	if (!c.load(w)) return false;
	size_t wEnd = w + WIN < c.count ? w + WIN : c.count;
	for (size_t j = (w > 24 ? w : 24); j < wEnd && j + 12 < c.count; j++) {
		if (!isMOV60f(c.at(j))) continue;
		bool fmov = false, fdiv = false;
		uint64_t target = 0;
		bool have = false;
		for (size_t k = j + 1; k < j + 12 && !have; k++) {
			uint32_t i = c.at(k);
			if (isFMOVsw(i) && rn(i) == rd(c.at(j))) fmov = true;
			else if (isFDIVs(i)) fdiv = true;
			else if (fmov && fdiv && storeTarget(c, k, true, &target)) have = true;
		}
		if (!have) continue;
		bool nearVsync = false;
		for (size_t k = j - 24; k < j && !nearVsync; k++) {
			uint64_t t = 0;
			if (storeTarget(c, k, false, &t) && t == r->vsyncLock) nearVsync = true;
		}
		if (nearVsync) votes[target]++;
	}
	}
	int best = 0;
	for (auto& v : votes) {
		if (v.second > best) { best = v.second; r->fpsLock = v.first; }
	}
	if (best) {
		r->fpsLockFound = r->fpsLockFromPattern = true;
		if (votes.size() > 1) LOG("FPS: warning, %ld FPS lock candidates\n", (long)votes.size());
		return true;
	}
	// Fallback: default value near vsync lock (only works if game didn't change it)
	uint32_t buf[0x100 / 4];
	if (!c.read(c.base + r->vsyncLock, buf, sizeof(buf))) {
		for (size_t k = 1; k < sizeof(buf) / 4; k++) {
			if (buf[k] == FLOAT_1_60) {
				r->fpsLock = r->vsyncLock + k * 4;
				r->fpsLockFound = true;
				return true;
			}
		}
	}
	LOG("FPS: FPS lock not found\n");
	return false;
}

} // namespace

bool findFpsLocks(uint64_t base, size_t textSize, FpsLockReadFn read, FpsLockResult* out,
                  int (*log)(const char*, ...)) {
	memset(out, 0, sizeof(*out));
	Ctx c;
	c.base = base;
	c.read = read;
	c.count = textSize / 4;
	c.log = log;
	c.win.reserve(WIN + 2 * MARGIN);
	bool ok = (out->thunksFound = findThunks(c, out)) && findVsyncLock(c, out) && findFpsLock(c, out);
	if (c.readFailed) LOG("FPS: failed to read .text\n");
	return ok;
}
