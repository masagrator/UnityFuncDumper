#include "typeInfoDumper.hpp"
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <map>
#include <algorithm>

namespace {

// Il2CppClass
const uint32_t K_NAME = 0x10, K_NAMESPACE = 0x18;
// il2cpp v24+: byval_arg/this_arg embedded at 0x20/0x30, klass self pointer at 0x78
// il2cpp v23 (Unity 5.5): byval_arg/this_arg are pointers at 0x20/0x28, no self pointer,
//                         element_class/castClass (0x30/0x38) point to class itself
struct ClassLayout { bool byvalPtr; uint32_t thisArg, declaring, handle, self; };
const ClassLayout LAYOUT_V24 = {false, 0x30, 0x50, 0x68, 0x78};
const ClassLayout LAYOUT_V23 = {true,  0x28, 0x40, 0x58, 0x30};
// Il2CppTypeEnum
enum : uint8_t {
	T_PTR = 0x0f, T_VALUETYPE = 0x11, T_CLASS = 0x12, T_VAR = 0x13, T_ARRAY = 0x14,
	T_GENERICINST = 0x15, T_SZARRAY = 0x1d, T_MVAR = 0x1e
};

const char* primName(uint8_t t) {
	switch (t) {
		case 0x01: return "void";    case 0x02: return "bool";   case 0x03: return "char";
		case 0x04: return "sbyte";   case 0x05: return "byte";   case 0x06: return "short";
		case 0x07: return "ushort";  case 0x08: return "int";    case 0x09: return "uint";
		case 0x0a: return "long";    case 0x0b: return "ulong";  case 0x0c: return "float";
		case 0x0d: return "double";  case 0x0e: return "string"; case 0x16: return "TypedReference";
		case 0x18: return "IntPtr";  case 0x19: return "UIntPtr"; case 0x1c: return "object";
	}
	return nullptr;
}

#define LOG(...) do { if (log) log(__VA_ARGS__); } while (0)

class Mem {
public:
	Mem(TypeInfoReadFn r, uint64_t dataStart, const std::vector<uint64_t>& data)
		: read(r), dStart(dataStart), dEnd(dataStart + data.size() * 8), data(data) {}

	bool rd(uint64_t a, void* out, size_t n) {
		if (a >= dStart && a + n <= dEnd) {
			memcpy(out, (const char*)data.data() + (a - dStart), n);
			return true;
		}
		char* o = (char*)out;
		while (n) {
			uint64_t pg = a & ~(uint64_t)(PAGE - 1);
			size_t off = a - pg, k = std::min(n, (size_t)(PAGE - off));
			const uint8_t* p = page(pg);
			if (!p) return false;
			memcpy(o, p + off, k);
			o += k; a += k; n -= k;
		}
		return true;
	}
	uint64_t q(uint64_t a, bool* ok = nullptr) {
		uint64_t v = 0;
		bool r = rd(a, &v, 8);
		if (ok) *ok = r;
		return r ? v : 0;
	}
	uint32_t u32(uint64_t a) { uint32_t v = 0; rd(a, &v, 4); return v; }
	std::string cstr(uint64_t a) {
		std::string s;
		char buf[64];
		while (s.size() < 1024) {
			if (!rd(a + s.size(), buf, sizeof(buf))) break;
			size_t l = strnlen(buf, sizeof(buf));
			s.append(buf, l);
			if (l < sizeof(buf)) break;
		}
		return s;
	}
private:
	static const size_t PAGE = 0x1000;
	static const size_t MAX_PAGES = 0x2000; // 32 MB
	const uint8_t* page(uint64_t pg) {
		auto it = cache.find(pg);
		if (it != cache.end()) return it->second.empty() ? nullptr : it->second.data();
		if (cache.size() >= MAX_PAGES) cache.clear();
		std::vector<uint8_t>& v = cache[pg];
		v.resize(PAGE);
		if (read(pg, v.data(), PAGE)) { v.clear(); return nullptr; }
		return v.data();
	}
	TypeInfoReadFn read;
	uint64_t dStart, dEnd;
	const std::vector<uint64_t>& data;
	std::unordered_map<uint64_t, std::vector<uint8_t>> cache;
};

inline uint8_t typeEnum(uint64_t bits) { return (bits >> 16) & 0xff; }
inline bool isClassKind(uint8_t t) {
	return t == T_VALUETYPE || t == T_CLASS || t == T_GENERICINST || t == T_ARRAY || t == T_SZARRAY ||
	       primName(t) != nullptr; // System.Object, System.String, System.Int32...
}

struct Dumper {
	Mem& m;
	uint64_t base, moduleEnd;
	uint64_t types = 0, typesCount = 0;
	uint64_t usages = 0, usagesCount = 0; // il2cpp < v27: metadataUsages[] slot pointer array
	// il2cpp < v27: Il2CppType.data / Il2CppGenericClass hold type definition index instead of pointer
	bool indexMode = false;
	uint64_t tdBase = 0, tdStride = 0;
	uint64_t strBase = 0;
	bool haveDecl = false;
	uint32_t declOff = 0, declWidth = 0;
	std::unordered_map<uint64_t, uint64_t> classByHandle;
	std::map<std::pair<uint64_t, int>, std::string> memo;

	Dumper(Mem& m, uint64_t base, uint64_t moduleEnd) : m(m), base(base), moduleEnd(moduleEnd) {}

	bool inModule(uint64_t p) { return p >= base && p < moduleEnd; }

	ClassLayout L = LAYOUT_V24;

	bool isClassWith(const ClassLayout& l, uint64_t k) {
		if (!k || (k & 7) || inModule(k)) return false;
		uint64_t q[16];
		if (!m.rd(k, q, sizeof(q))) return false;
		if (!q[K_NAME / 8] || (q[K_NAME / 8] >= base && q[K_NAME / 8] < moduleEnd)) return false;
		if (!l.byvalPtr) {
			if (q[l.self / 8] != k) return false;
			uint8_t t = typeEnum(q[0x20 / 8 + 1]);
			return isClassKind(t) && typeEnum(q[l.thisArg / 8 + 1]) == t;
		}
		uint64_t bv[2], ta[2];
		if (!q[4] || !q[l.thisArg / 8] || !m.rd(q[4], bv, sizeof(bv)) || !m.rd(q[l.thisArg / 8], ta, sizeof(ta))) return false;
		uint8_t t = typeEnum(bv[1]);
		if (!isClassKind(t) || typeEnum(ta[1]) != t) return false;
		// element_class == castClass == self, except arrays
		return q[l.self / 8] == k || t == T_ARRAY || t == T_SZARRAY;
	}
	bool isClass(uint64_t k) { return isClassWith(L, k); }
	// Il2CppType* of class (byval_arg)
	uint64_t classType(uint64_t k) { return L.byvalPtr ? m.q(k + 0x20) : k + 0x20; }

	uint64_t typeAt(uint64_t index) { return index < typesCount ? m.q(types + index * 8) : 0; }

	// Type definition handle from Il2CppType.data of CLASS/VALUETYPE
	uint64_t defHandle(uint64_t data) { return indexMode ? tdBase + (uint32_t)data * tdStride : data; }
	// Type definition handle of GENERICINST's Il2CppGenericClass
	uint64_t genericDefHandle(uint64_t gc) {
		return indexMode ? tdBase + (uint64_t)m.u32(gc) * tdStride : m.q(m.q(gc));
	}
	uint64_t typeHandle(uint64_t t) {
		uint64_t q[2];
		if (!t || !m.rd(t, q, sizeof(q))) return 0;
		uint8_t ty = typeEnum(q[1]);
		if (ty == T_CLASS || ty == T_VALUETYPE) return defHandle(q[0]);
		if (ty == T_GENERICINST) return genericDefHandle(q[0]);
		return 0;
	}

	std::string defName(uint64_t handle, bool addNs, bool nested, bool* generic) {
		std::string name = m.cstr(strBase + m.u32(handle));
		std::string pre;
		bool done = false;
		if (haveDecl) {
			uint32_t v = 0;
			m.rd(handle + declOff, &v, declWidth);
			uint32_t none = declWidth == 2 ? 0xFFFF : 0xFFFFFFFF;
			if (v != none && v < typesCount) {
				uint64_t dt = typeAt(v);
				if (dt) { pre = typeName(dt, addNs, true) + "."; done = true; }
			}
		}
		else {
			auto it = classByHandle.find(handle);
			if (it != classByHandle.end()) {
				uint64_t d = m.q(it->second + L.declaring);
				if (d) { pre = defName(m.q(d + L.handle), addNs, true, nullptr) + "."; done = true; }
			}
		}
		if (!done && addNs) {
			std::string ns = m.cstr(strBase + m.u32(handle + 4));
			if (!ns.empty()) pre = ns + ".";
		}
		size_t tick = name.find('`');
		if (generic) *generic = tick != std::string::npos;
		// Generic type definition params come from metadata; keep `N name in that case
		if (tick != std::string::npos && (nested || !generic)) name.resize(tick);
		return pre + name;
	}

	std::string typeName(uint64_t t, bool addNs, bool nested) {
		auto key = std::make_pair(t, (addNs ? 1 : 0) | (nested ? 2 : 0));
		auto it = memo.find(key);
		if (it != memo.end()) return it->second;
		std::string r = typeNameImpl(t, addNs, nested);
		memo[key] = r;
		return r;
	}

	std::string typeNameImpl(uint64_t t, bool addNs, bool nested) {
		uint64_t q[2];
		if (!t || !m.rd(t, q, sizeof(q))) return "?";
		uint64_t data = q[0];
		uint8_t ty = typeEnum(q[1]);
		if (const char* p = primName(ty)) return p;
		switch (ty) {
			case T_SZARRAY: return typeName(data, addNs, false) + "[]";
			case T_PTR:     return typeName(data, addNs, false) + "*";
			case T_ARRAY: {
				uint8_t rank = 1;
				m.rd(data + 8, &rank, 1);
				return typeName(m.q(data), addNs, false) + "[" + std::string(rank ? rank - 1 : 0, ',') + "]";
			}
			case T_VAR:
			case T_MVAR:
				return "T";
			case T_VALUETYPE:
			case T_CLASS: {
				bool generic = false;
				std::string n = defName(defHandle(data), addNs, nested, &generic);
				return n;
			}
			case T_GENERICINST: {
				uint64_t h = genericDefHandle(data);
				std::string n = defName(h, addNs, true, nullptr);
				if (nested) return n;
				uint64_t inst = m.q(data + 8);
				uint32_t argc = m.u32(inst);
				uint64_t argv = m.q(inst + 8);
				if (argc > 64) return n + "<?>";
				n += "<";
				for (uint32_t i = 0; i < argc; i++) {
					if (i) n += ", ";
					n += typeName(m.q(argv + i * 8), false, false);
				}
				return n + ">";
			}
		}
		char buf[16];
		snprintf(buf, sizeof(buf), "?T%02X", ty);
		return buf;
	}

	void addClass(uint64_t k, int depth = 0) {
		uint64_t h = m.q(k + L.handle);
		uint8_t t = typeEnum(m.q(classType(k) + 8));
		if (h && (t == T_CLASS || t == T_VALUETYPE)) classByHandle.emplace(h, k);
		uint64_t d = m.q(k + L.declaring);
		if (d && depth < 8 && isClass(d)) addClass(d, depth + 1);
	}

	bool calibrateStrBase() {
		std::map<uint64_t, int> votes;
		int n = 0;
		for (auto& e : classByHandle) {
			uint64_t nm = m.q(e.second + K_NAME);
			votes[nm - m.u32(e.first)]++;
			if (++n == 64) break;
		}
		int best = 0;
		for (auto& v : votes) if (v.second > best) { best = v.second; strBase = v.first; }
		return best >= 3 && best * 2 > n;
	}

	// Il2CppClass.byval_arg.data == typeMetadataHandle in v27+, otherwise it's index into
	// type definition table: calibrate table base and entry size from loaded classes
	bool calibrateMode() {
		std::vector<std::pair<uint64_t, uint64_t>> s; // (byval data, handle)
		for (auto& e : classByHandle) {
			s.emplace_back(m.q(classType(e.second)), e.first);
			if (s.size() == 64) break;
		}
		if (s.size() < 3) return false;
		size_t same = 0;
		for (auto& x : s) if (x.first == x.second) same++;
		if (same == s.size()) { indexMode = false; return true; }
		std::sort(s.begin(), s.end());
		uint64_t stride = 0;
		for (size_t i = 1; i < s.size() && !stride; i++) {
			if (s[i].first == s[i - 1].first || s[i].first > 0xFFFFFF) continue;
			uint64_t di = s[i].first - s[i - 1].first, dp = s[i].second - s[i - 1].second;
			if (dp % di == 0) stride = dp / di;
		}
		if (!stride || stride > 0x400) return false;
		uint64_t b = s[0].second - s[0].first * stride;
		for (auto& x : s) if (b + x.first * stride != x.second) return false;
		indexMode = true;
		tdBase = b;
		tdStride = stride;
		return true;
	}

	// Find which field of type definition is declaringTypeIndex (types[] index of declaring type)
	void calibrateDeclaring() {
		std::vector<std::pair<uint64_t, uint64_t>> nestedS;
		std::vector<uint64_t> topS;
		for (auto& e : classByHandle) {
			uint64_t d = m.q(e.second + L.declaring);
			if (d) {
				uint64_t dh = m.q(d + L.handle);
				if (dh && nestedS.size() < 48) nestedS.emplace_back(e.first, dh);
			}
			else if (topS.size() < 48) topS.push_back(e.first);
		}
		if (nestedS.size() < 3 || topS.size() < 3) return;
		for (uint32_t off = 8; off < 0x40; off += 2) {
			for (uint32_t w = 4; w >= 2; w -= 2) { // prefer 4-byte field (v24-v31), v39 uses 2-byte
				if (off % w) continue;
				uint32_t none = w == 2 ? 0xFFFF : 0xFFFFFFFF;
				bool ok = true;
				for (auto& s : nestedS) {
					uint32_t v = 0;
					m.rd(s.first + off, &v, w);
					if (v >= typesCount || typeHandle(typeAt(v)) != s.second) { ok = false; break; }
				}
				for (size_t i = 0; ok && i < topS.size(); i++) {
					uint32_t v = 0;
					m.rd(topS[i] + off, &v, w);
					if (v != none) ok = false;
				}
				if (ok) { haveDecl = true; declOff = off; declWidth = w; return; }
			}
		}
	}
};

bool findMetadataRegistration(Dumper& d, const std::vector<uint64_t>& Q) {
	for (size_t i = 0; i + 16 < Q.size(); i++) {
		uint64_t c = Q[i + 10];
		if (c < 100 || c > 0x1000000 || Q[i + 12] != c) continue;
		if (Q[i + 6] < c || Q[i + 6] > 0x1000000) continue;
		if (Q[i] > 0x1000000 || Q[i + 2] > 0x1000000 || Q[i + 4] > 0x1000000) continue;
		bool ptrs = true;
		for (int k : {1, 3, 5, 7, 11, 13}) if (!d.inModule(Q[i + k])) ptrs = false;
		if (!ptrs || (Q[i + 7] & 7)) continue;
		d.types = Q[i + 7];
		d.typesCount = Q[i + 6];
		d.usagesCount = Q[i + 14];
		d.usages = Q[i + 15];
		bool ok = true;
		for (uint64_t k = 0; k < 32 && ok; k++) {
			uint64_t t = d.typeAt(k * (d.typesCount / 32));
			uint64_t bits = d.m.q(t + 8);
			if (!d.inModule(t) || typeEnum(bits) == 0 || typeEnum(bits) > 0x55) ok = false;
		}
		if (ok) return true;
	}
	d.types = d.typesCount = 0;
	return false;
}

} // namespace

bool dumpTypeInfos(uint64_t base, uint64_t dataStart, uint64_t bssStart, uint64_t moduleEnd,
                   TypeInfoReadFn read, std::vector<TypeInfoEntry>& out, int (*log)(const char*, ...)) {
	out.clear();
	if (bssStart <= dataStart) return false;
	std::vector<uint64_t> Q((bssStart - dataStart) / 8);
	const size_t chunk = 0x400000;
	for (size_t o = 0; o < Q.size() * 8; o += chunk) {
		size_t sz = std::min(chunk, Q.size() * 8 - o);
		if (read(dataStart + o, (char*)Q.data() + o, sz)) {
			LOG("TypeInfo: failed to read .data\n");
			return false;
		}
	}
	Mem m(read, dataStart, Q);
	Dumper d(m, base, moduleEnd);

	if (!findMetadataRegistration(d, Q)) {
		LOG("TypeInfo: Il2CppMetadataRegistration not found\n");
		return false;
	}
	LOG("TypeInfo: types[] at main+0x%lX, count %ld\n", (unsigned long)(d.types - base), (long)d.typesCount);

	// Metadata usage slot block: cluster of encoded TypeInfo / Il2CppType usages
	auto encoded = [&](uint64_t v, int kind) {
		return (v >> 32) == 0 && (v & 1) && (int)(v >> 29) == kind && ((v >> 1) & 0x0FFFFFFF) < d.typesCount;
	};
	size_t lo = 0, hi = 0, start = SIZE_MAX, prev = 0;
	for (size_t i = 0; i <= Q.size(); i++) {
		bool hit = i < Q.size() && (encoded(Q[i], 1) || encoded(Q[i], 2));
		if (!hit && i < Q.size()) continue;
		if (start != SIZE_MAX && (i == Q.size() || i - prev > 0x200)) {
			if (prev - start > hi - lo) { lo = start; hi = prev; }
			start = SIZE_MAX;
		}
		if (i == Q.size()) break;
		if (start == SIZE_MAX) start = i;
		prev = i;
	}
	struct Slot { uint64_t addr; uint64_t type; bool init; };
	std::vector<Slot> slots;
	if (hi != lo) {
		lo = lo > 64 ? lo - 64 : 0;
		hi = std::min(hi + 64, Q.size() - 1);
		LOG("TypeInfo: usage block main+0x%lX - main+0x%lX\n",
		    (unsigned long)(dataStart + lo * 8 - base), (unsigned long)(dataStart + hi * 8 - base));

		for (size_t i = lo; i <= hi; i++) {
			uint64_t v = Q[i], a = dataStart + i * 8;
			if (encoded(v, 1)) slots.push_back({a, d.typeAt((v >> 1) & 0x0FFFFFFF), false});
			else if (!(v & 1) && d.isClass(v)) {
				slots.push_back({a, d.classType(v), true});
				d.addClass(v);
			}
		}
	}
	else if (d.usagesCount && d.usagesCount < 0x1000000 && d.inModule(d.usages)) {
		// il2cpp < v27: slots are listed in metadataUsages[], uninitialized slots are 0
		// (their type is known only from global-metadata, so only initialized ones are dumped)
		LOG("TypeInfo: il2cpp < v27, %ld metadata usages, dumping initialized TypeInfo only\n", (long)d.usagesCount);
		std::vector<uint64_t> ptrs(d.usagesCount);
		for (size_t o = 0; o < ptrs.size(); o += 0x8000) {
			size_t n = std::min((size_t)0x8000, ptrs.size() - o);
			if (!m.rd(d.usages + o * 8, &ptrs[o], n * 8)) {
				LOG("TypeInfo: failed to read metadataUsages\n");
				return false;
			}
		}
		std::sort(ptrs.begin(), ptrs.end());
		ptrs.erase(std::unique(ptrs.begin(), ptrs.end()), ptrs.end());
		// Il2CppClass layout: vote over slot values
		size_t v24 = 0, v23 = 0, sampled = 0;
		for (uint64_t a : ptrs) {
			if (!d.inModule(a) || (a & 7)) continue;
			uint64_t v = m.q(a);
			if (!v || (v & 7)) continue;
			if (d.isClassWith(LAYOUT_V24, v)) v24++;
			else if (d.isClassWith(LAYOUT_V23, v)) v23++;
			if (++sampled == 4000) break;
		}
		d.L = v23 > v24 ? LAYOUT_V23 : LAYOUT_V24;
		LOG("TypeInfo: Il2CppClass layout %s (%ld/%ld)\n", v23 > v24 ? "v23" : "v24+", (long)v23, (long)v24);
		for (uint64_t a : ptrs) {
			if (!d.inModule(a) || (a & 7)) continue;
			uint64_t v = m.q(a);
			if (v && d.isClass(v)) {
				slots.push_back({a, d.classType(v), true});
				d.addClass(v);
			}
		}
	}
	else {
		LOG("TypeInfo: metadata usage slots not found\n");
		return false;
	}
	if (!d.calibrateStrBase()) {
		LOG("TypeInfo: not enough initialized classes, run it later in game\n");
		return false;
	}
	if (!d.calibrateMode()) {
		LOG("TypeInfo: couldn't calibrate type definition handles\n");
		return false;
	}
	if (d.indexMode) LOG("TypeInfo: type definitions at 0x%lX, size 0x%lX\n", (unsigned long)d.tdBase, (unsigned long)d.tdStride);
	d.calibrateDeclaring();
	if (d.haveDecl) LOG("TypeInfo: declaringTypeIndex at +0x%X (%d bytes)\n", d.declOff, d.declWidth);
	else LOG("TypeInfo: declaringTypeIndex not calibrated, nested names may be incomplete\n");

	out.reserve(slots.size());
	for (auto& s : slots) {
		out.push_back({s.addr - base, d.typeName(s.type, true, false) + "_TypeInfo", s.init});
	}
	return true;
}
