#include "icallScanner.hpp"
#include <cstring>
#include <map>
#include <unordered_map>
#include <algorithm>

namespace {

const size_t WIN    = 0x200000; // instructions per window (8 MB)
const size_t MARGIN = 0x400;

inline uint32_t rd(uint32_t i) { return i & 31; }
inline uint32_t rn(uint32_t i) { return (i >> 5) & 31; }
inline uint32_t rm(uint32_t i) { return (i >> 16) & 31; }
inline int64_t sext(uint64_t v, int bits) { uint64_t m = 1ULL << (bits - 1); return (int64_t)((v ^ m) - m); }
inline bool isADRP(uint32_t i) { return (i & 0x9F000000) == 0x90000000; }
inline bool isADR(uint32_t i)  { return (i & 0x9F000000) == 0x10000000; }
inline uint64_t adrTarget(uint32_t i, uint64_t pc) {
	uint64_t imm = ((i >> 29) & 3) | (((uint64_t)(i >> 5) & 0x7FFFF) << 2);
	if (isADRP(i)) return (pc & ~0xFFFULL) + (sext(imm, 21) << 12);
	return pc + sext(imm, 21);
}
inline bool isADDimm64(uint32_t i) { return (i & 0xFF800000) == 0x91000000; }
inline uint64_t addImm(uint32_t i) { return (uint64_t)((i >> 10) & 0xFFF) << (((i >> 22) & 1) ? 12 : 0); }
inline bool isLDRx(uint32_t i) { return (i & 0xFFC00000) == 0xF9400000; }
inline int64_t uimm(uint32_t i, int scale) { return (int64_t)((i >> 10) & 0xFFF) * scale; }
inline bool isMOVx(uint32_t i) { return (i & 0xFFE0FFE0) == 0xAA0003E0; }
inline bool isBL(uint32_t i) { return (i & 0xFC000000) == 0x94000000; }
inline uint64_t blTarget(uint32_t i, uint64_t pc) { return pc + (sext(i & 0x3FFFFFF, 26) << 2); }
// LDR Xt, [Xn, Xm{, LSL #3}]
inline bool isLDRxReg(uint32_t i) { return (i & 0xFFE00C00) == 0xF8600800; }
// CMP Xn, #imm
inline bool isCMPx(uint32_t i) { return (i & 0xFF80001F) == 0xF100001F; }
inline bool isLoadStore(uint32_t i) { return (i & 0x0A000000) == 0x08000000; }
inline bool isBranch(uint32_t i) { return (i & 0x1C000000) == 0x14000000; }

struct Ctx {
	uint64_t base, textEnd, moduleEnd;
	ICallReadFn read;
	size_t count;
	std::vector<uint32_t> win;
	size_t winStart = 0;
	bool readFailed = false;
	std::unordered_map<uint64_t, std::vector<uint8_t>> pages;

	uint32_t at(size_t j) const {
		return (j >= winStart && j - winStart < win.size()) ? win[j - winStart] : 0;
	}
	bool load(size_t w) {
		size_t from = w > MARGIN ? w - MARGIN : 0;
		size_t to = std::min(w + WIN + MARGIN, count);
		win.resize(to - from);
		winStart = from;
		if (read(base + from * 4, win.data(), (to - from) * 4)) { win.clear(); readFailed = true; return false; }
		return true;
	}
	bool inModule(uint64_t p) const { return p >= base && p < moduleEnd; }
	bool inText(uint64_t p) const { return p > base && p < textEnd && !(p & 3); }

	// Cached small reads outside .text (strings, GOT)
	bool rdc(uint64_t a, void* out, size_t n) {
		char* o = (char*)out;
		while (n) {
			uint64_t pg = a & ~0xFFFULL;
			size_t off = a - pg, k = std::min(n, (size_t)(0x1000 - off));
			auto it = pages.find(pg);
			if (it == pages.end()) {
				if (pages.size() > 0x1000) pages.clear(); // 16 MB max
				std::vector<uint8_t>& v = pages[pg];
				v.resize(0x1000);
				if (read(pg, v.data(), 0x1000)) v.clear();
				it = pages.find(pg);
			}
			if (it->second.empty()) return false;
			memcpy(o, it->second.data() + off, k);
			o += k; a += k; n -= k;
		}
		return true;
	}
	std::string cstr(uint64_t a, size_t max = 256) {
		std::string s;
		char buf[64];
		while (s.size() < max) {
			if (!rdc(a + s.size(), buf, sizeof(buf))) break;
			size_t l = strnlen(buf, sizeof(buf));
			s.append(buf, l);
			if (l < sizeof(buf)) break;
		}
		return s;
	}
};

// Value of `reg` before instruction idx (absolute address). Handles ADRP/ADR + ADD/LDR(GOT) and MOV.
bool resolveReg(Ctx& c, size_t idx, uint32_t reg, size_t maxBack, uint64_t* out) {
	struct Op { bool load; int64_t imm; };
	Op ops[8];
	int nops = 0;
	size_t stop = idx > maxBack ? idx - maxBack : 0;
	for (size_t j = idx; j-- > stop; ) {
		uint32_t i = c.at(j);
		if (isADRP(i) || isADR(i)) {
			if (rd(i) != reg) continue;
			uint64_t v = c.base + adrTarget(i, j * 4);
			while (nops > 0) {
				Op& op = ops[--nops];
				v += op.imm;
				if (op.load) {
					uint64_t p = 0;
					if (!c.rdc(v, &p, 8)) return false;
					v = p;
				}
			}
			*out = v;
			return true;
		}
		if (isADDimm64(i) && rd(i) == reg) {
			if (nops == 8) return false;
			ops[nops++] = {false, (int64_t)addImm(i)};
			reg = rn(i);
			continue;
		}
		if (isLDRx(i) && rd(i) == reg) {
			if (nops == 8) return false;
			ops[nops++] = {true, uimm(i, 8)};
			reg = rn(i);
			continue;
		}
		if (isMOVx(i) && rd(i) == reg) { reg = rm(i); continue; }
		if (isBranch(i)) {
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
			if ((i >> 22) & 1) return false;
			continue;
		}
		if ((i & 0x1F) == 31) continue;
		return false;
	}
	return false;
}

bool isICallName(const std::string& s) {
	// Registered icall names have no signature; "Name(args)" strings belong to il2cpp icall resolving
	return s.size() > 7 && !strncmp(s.c_str(), "Unity", 5) && s.find("::") != std::string::npos &&
	       s.find('(') == std::string::npos;
}

struct Direct { uint64_t target, name, func; };
struct Loop { uint64_t target, names, funcs, count; };

} // namespace

bool scanInternalCalls(uint64_t base, size_t textSize, uint64_t moduleEnd, ICallReadFn read,
                       std::vector<ICallEntry>& out, int (*log)(const char*, ...)) {
	out.clear();
	Ctx c;
	c.base = base;
	c.textEnd = base + textSize;
	c.moduleEnd = moduleEnd;
	c.read = read;
	c.count = textSize / 4;
	c.win.reserve(WIN + 2 * MARGIN);

	std::vector<Direct> direct;
	std::vector<Loop> loops;
	std::map<uint64_t, size_t> votes;

	for (size_t w = 0; w < c.count; w += WIN) {
		if (!c.load(w)) break;
		size_t wEnd = std::min(w + WIN, c.count);
		for (size_t j = std::max(w, (size_t)4); j < wEnd && j + 8 < c.count; j++) {
			uint32_t i = c.at(j);
			if (!isBL(i)) continue;
			uint64_t target = base + blTarget(i, j * 4);

			// Loop form: LDR X0, [Xa, Xi] ; LDR X1, [Xb, Xi] right before BL
			uint32_t l0 = 0, l1 = 0;
			size_t k0 = 0, k1 = 0;
			for (size_t k = j - 4; k < j; k++) {
				uint32_t x = c.at(k);
				if (isLDRxReg(x) && rd(x) == 0) { l0 = x; k0 = k; }
				if (isLDRxReg(x) && rd(x) == 1) { l1 = x; k1 = k; }
			}
			if (l0 && l1 && rm(l0) == rm(l1)) {
				uint64_t names = 0, funcs = 0;
				if (!resolveReg(c, k0, rn(l0), 24, &names) || !resolveReg(c, k1, rn(l1), 24, &funcs)) continue;
				// ADD Xc, Xc, #step ; CMP Xc, #end
				uint64_t step = 0, end = 0;
				for (size_t k = j + 1; k < j + 6; k++) {
					uint32_t x = c.at(k);
					if (isADDimm64(x) && rd(x) == rn(x) && !step) step = addImm(x);
					else if (isCMPx(x) && step) { end = addImm(x); break; }
				}
				if ((step != 1 && step != 8) || !end) continue;
				uint64_t n = end / step;
				if (!n || n > 0x10000 || !c.inModule(names) || !c.inModule(funcs)) continue;
				uint64_t first = 0;
				if (!c.rdc(names, &first, 8) || !c.inModule(first) || !isICallName(c.cstr(first))) continue;
				loops.push_back({target, names, funcs, n});
				votes[target] += n;
				continue;
			}

			// Direct form
			uint64_t name = 0;
			if (!resolveReg(c, j, 0, 12, &name) || !c.inModule(name)) continue;
			uint64_t first8 = 0;
			if (!c.rdc(name, &first8, 8) || memcmp(&first8, "Unity", 5)) continue;
			if (!isICallName(c.cstr(name))) continue;
			uint64_t func = 0;
			if (!resolveReg(c, j, 1, 12, &func) || !c.inText(func)) continue;
			direct.push_back({target, name, func});
			votes[target]++;
		}
	}
	if (c.readFailed) {
		if (log) log("ICall: failed to read .text\n");
		return false;
	}
	uint64_t reg = 0;
	size_t best = 0;
	for (auto& v : votes) if (v.second > best) { best = v.second; reg = v.first; }
	if (best < 16) {
		if (log) log("ICall: register function not found\n");
		return false;
	}
	size_t nl = 0, nd = 0;
	for (auto& l : loops) {
		if (l.target != reg) continue;
		nl++;
		std::vector<uint64_t> names(l.count), funcs(l.count);
		if (!c.rdc(l.names, names.data(), l.count * 8) || !c.rdc(l.funcs, funcs.data(), l.count * 8)) continue;
		for (size_t k = 0; k < l.count; k++) {
			if (!c.inModule(names[k]) || !c.inText(funcs[k])) continue;
			std::string s = c.cstr(names[k], 1023);
			if (isICallName(s)) out.push_back({s, funcs[k] - base});
		}
	}
	for (auto& d : direct) {
		if (d.target != reg) continue;
		nd++;
		out.push_back({c.cstr(d.name, 1023), d.func - base});
	}
	if (log) log("ICall: register function main+0x%lX, %ld tables, %ld direct calls, %ld icalls\n",
	             (unsigned long)(reg - base), (long)nl, (long)nd, (long)out.size());
	return !out.empty();
}
