#include "cff_serializer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using std::string;

// Big-endian unsigned, `n` bytes.
void putBE(string& out, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) out.push_back((char)((v >> (8 * i)) & 0xff));
}

// CFF INDEX (TN#5176 s.5): count(2) [ offSize(1) offset[0..count]((count+1)*offSize)
// data ]. Offsets are 1-based (object i is [startPos+off[i], startPos+off[i+1])).
// An empty INDEX is just count=0 (two zero bytes) -- matches FoFiType1C::getIndex.
string encodeIndex(const std::vector<string>& objs) {
    string out;
    uint32_t count = (uint32_t)objs.size();
    putBE(out, count, 2);
    if (count == 0) return out;

    uint32_t dataLen = 0;
    for (const auto& o : objs) dataLen += (uint32_t)o.size();
    uint32_t last = 1 + dataLen;                     // largest offset value
    int offSize = last < 0x100 ? 1 : last < 0x10000 ? 2 : last < 0x1000000 ? 3 : 4;
    out.push_back((char)offSize);

    uint32_t cum = 1;
    putBE(out, cum, offSize);                         // offset[0] = 1
    for (const auto& o : objs) { cum += (uint32_t)o.size(); putBE(out, cum, offSize); }
    for (const auto& o : objs) out += o;              // concatenated object data
    return out;
}

// DICT integer operand, always the 5-byte form (b0=29, int32 BE). Fixed width so
// the encoded DICT size does NOT depend on the offset value -- this breaks the
// offset<->size circular dependency when back-patching CharStrings/Private/Subrs
// offsets. (TN#5176 Table 3.)
void dictInt5(string& out, int32_t v) {
    out.push_back((char)29);
    putBE(out, (uint32_t)v, 4);
}

// Type2 charstring integer (TN#5177 s.3.2). Compact forms; the 16.16 fixed (255)
// branch is only a fallback for out-of-16-bit magnitudes (not expected in normal
// subr indices / operands).
void csInt(string& out, int32_t v) {
    if (v >= -107 && v <= 107) {
        out.push_back((char)(v + 139));
    } else if (v >= 108 && v <= 1131) {
        int32_t t = v - 108;
        out.push_back((char)((t >> 8) + 247));
        out.push_back((char)(t & 0xff));
    } else if (v >= -1131 && v <= -108) {
        int32_t t = -v - 108;
        out.push_back((char)((t >> 8) + 251));
        out.push_back((char)(t & 0xff));
    } else if (v >= -32768 && v <= 32767) {
        out.push_back((char)28);
        putBE(out, (uint16_t)v, 2);
    } else {
        out.push_back((char)255);
        putBE(out, (uint32_t)(v << 16), 4);
    }
}

// Type2 subr bias (TN#5177 s.4.7). Depends only on the subr COUNT, not on offsets,
// so charstring bytes are fully offset-independent.
int32_t subrBias(size_t n) {
    return n < 1240 ? 107 : n < 33900 ? 1131 : 32768;
}

// Encode one charstring (list of Op) to Type2 bytes.
string encodeCharstring(const pdf_cff::Charstring& cs, size_t nLocal, size_t nGlobal) {
    string out;
    for (const auto& op : cs.ops()) {
        switch (op.kind_case()) {
        case pdf_cff::Op::kOperand:
            csInt(out, op.operand());
            break;
        case pdf_cff::Op::kOp:
            out.push_back((char)op.op());            // enum value == Type2 opcode byte
            break;
        case pdf_cff::Op::kCallLocal:                // real index -> biased operand + callsubr
            csInt(out, op.call_local() - subrBias(nLocal));
            out.push_back((char)10);
            break;
        case pdf_cff::Op::kCallGlobal:               // real index -> biased operand + callgsubr
            csInt(out, op.call_global() - subrBias(nGlobal));
            out.push_back((char)29);
            break;
        case pdf_cff::Op::kRaw:
            out += op.raw();
            break;
        case pdf_cff::Op::KIND_NOT_SET:
            break;
        }
    }
    return out;
}

} // namespace

std::string SerializeCff(const pdf_cff::CffFont& font) {
    const size_t nLocal = font.local_subrs_size();
    const size_t nGlobal = font.global_subrs_size();

    // --- offset-independent pieces (charstring bytes depend only on subr counts) ---
    std::vector<string> charstrings, localSubrs, globalSubrs;
    if (font.charstrings_size() == 0) {
        charstrings.push_back(string(1, (char)14));  // default .notdef = endchar
    } else {
        for (const auto& cs : font.charstrings())
            charstrings.push_back(encodeCharstring(cs, nLocal, nGlobal));
    }
    for (const auto& cs : font.local_subrs())
        localSubrs.push_back(encodeCharstring(cs, nLocal, nGlobal));
    for (const auto& cs : font.global_subrs())
        globalSubrs.push_back(encodeCharstring(cs, nLocal, nGlobal));

    const string nameIndex        = encodeIndex({ font.font_name() });
    const string stringIndex      = encodeIndex({});             // no custom strings
    const string gsubrIndex       = encodeIndex(globalSubrs);
    const string charStringsIndex = encodeIndex(charstrings);
    const string localSubrIndex   = nLocal ? encodeIndex(localSubrs) : string();

    // Private DICT: only op 19 (Subrs), whose operand is the Subrs INDEX offset
    // RELATIVE to the Private start. The Local Subr INDEX sits immediately after
    // the Private DICT, so that offset == the Private DICT length. With a fixed
    // 5-byte operand + 1-byte operator the length is a constant 6, so it resolves
    // without iteration.
    string privateDict;
    if (nLocal) {
        dictInt5(privateDict, 6);                    // Subrs offset (relative) = len(privateDict)
        privateDict.push_back((char)19);             // operator 19: Subrs
    }
    const uint32_t privateLen = (uint32_t)privateDict.size();  // 6 or 0

    // Top DICT: CharStrings offset (op 17) + Private size,offset (op 18). All
    // operands use the fixed 5-byte form, so the Top DICT is a constant 17 bytes
    // regardless of the actual offset values -> the Top DICT INDEX size is known
    // before the offsets are.
    auto buildTopDict = [&](uint32_t csOff, uint32_t privOff) {
        string td;
        dictInt5(td, (int32_t)csOff);                td.push_back((char)17);  // CharStrings
        dictInt5(td, (int32_t)privateLen);
        dictInt5(td, (int32_t)privOff);              td.push_back((char)18);  // Private (size, offset)
        return td;
    };
    const string topDictIndexSized = encodeIndex({ buildTopDict(0, 0) });

    // Header is 4 bytes; Name INDEX must start at hdrSize (FoFiType1C reads it from
    // byte 2). Everything up to CharStrings is now fixed-length, so compute offsets.
    const uint32_t offName   = 4;
    const uint32_t offTopDi  = offName   + (uint32_t)nameIndex.size();
    const uint32_t offString = offTopDi  + (uint32_t)topDictIndexSized.size();
    const uint32_t offGsubr  = offString + (uint32_t)stringIndex.size();
    const uint32_t offChar   = offGsubr  + (uint32_t)gsubrIndex.size();       // CharStrings offset
    const uint32_t offPriv   = offChar   + (uint32_t)charStringsIndex.size(); // Private offset

    const string topDictIndex = encodeIndex({ buildTopDict(offChar, offPriv) });
    // topDictIndex.size() == topDictIndexSized.size() by construction (fixed 5-byte operands).

    string out;
    out.push_back(1);            // major
    out.push_back(0);            // minor
    out.push_back(4);            // hdrSize  (== offset of Name INDEX)
    out.push_back(1);            // offSize
    out += nameIndex;
    out += topDictIndex;
    out += stringIndex;
    out += gsubrIndex;
    out += charStringsIndex;
    out += privateDict;
    out += localSubrIndex;
    return out;
}
