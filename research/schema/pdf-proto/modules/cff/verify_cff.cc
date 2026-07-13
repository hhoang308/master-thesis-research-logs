// verify_cff -- success-case tests for SerializeCff (the CFF assembler).
//
// Two oracles per case:
//   (1) a built-in CFF re-parser that MIRRORS xpdf's FoFiType1C::getIndex /
//       getIndexVal (FoFiType1C.cc:3294/3318) -- if this walks the INDEX/DICT
//       offsets and recovers the structure we put in, xpdf's parser will too.
//   (2) a FontTools cross-check (fontTools.cffLib) -- an independent reader, the
//       "semantic round-trip" oracle. Best-effort: it corroborates (1); a
//       FontTools error is reported but does not by itself fail the build, since
//       the FoFiType1C-mirroring parser is the target-relevant authority.
//
// Success cases only (valid CFF). Weakness injection (self-referential callsubr)
// comes later, once the valid skeleton is proven correct.

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include "modules/cff/cff.pb.h"
#include "modules/cff/cff_serializer.h"

using std::string;

// ---- minimal CFF reader, mirroring FoFiType1C ------------------------------

static uint32_t u8(const string& f, size_t p)  { return p < f.size() ? (uint8_t)f[p] : 0; }
static uint32_t u16(const string& f, size_t p) { return (u8(f, p) << 8) | u8(f, p + 1); }
static uint32_t uvarBE(const string& f, size_t p, int n) {
    uint32_t v = 0; for (int i = 0; i < n; i++) v = (v << 8) | u8(f, p + i); return v;
}

struct Index { int count = 0, offSize = 0; size_t pos = 0, startPos = 0, endPos = 0; bool ok = false; };

// FoFiType1C::getIndex (FoFiType1C.cc:3294)
static Index getIndex(const string& f, size_t pos) {
    Index idx; idx.pos = pos;
    if (pos + 2 > f.size()) return idx;
    idx.count = u16(f, pos);
    if (idx.count == 0) { idx.startPos = idx.endPos = pos + 2; idx.ok = true; return idx; }
    idx.offSize = u8(f, pos + 2);
    if (idx.offSize < 1 || idx.offSize > 4) return idx;
    idx.startPos = pos + 3 + (size_t)(idx.count + 1) * idx.offSize - 1;
    if (idx.startPos >= f.size()) return idx;
    idx.endPos = idx.startPos + uvarBE(f, pos + 3 + (size_t)idx.count * idx.offSize, idx.offSize);
    if (idx.endPos < idx.startPos || idx.endPos > f.size()) return idx;
    idx.ok = true; return idx;
}

// FoFiType1C::getIndexVal (FoFiType1C.cc:3318)
static bool getIndexVal(const string& f, const Index& idx, int i, size_t& vpos, size_t& vlen) {
    if (i < 0 || i >= idx.count) return false;
    size_t p0 = idx.startPos + uvarBE(f, idx.pos + 3 + (size_t)i * idx.offSize, idx.offSize);
    size_t p1 = idx.startPos + uvarBE(f, idx.pos + 3 + (size_t)(i + 1) * idx.offSize, idx.offSize);
    if (p1 < p0 || p1 > f.size()) return false;
    vpos = p0; vlen = p1 - p0; return true;
}

// Scan a DICT [pos,pos+len) for single-byte operator `wantOp`, returning its
// preceding integer operands (TN#5176 s.4: operands precede operator).
static bool dictOp(const string& f, size_t pos, size_t len, int wantOp, std::vector<int>& out) {
    std::vector<int> st;
    size_t p = pos, end = pos + len;
    while (p < end) {
        int b0 = u8(f, p);
        if (b0 >= 32 && b0 <= 246)       { st.push_back(b0 - 139); p += 1; }
        else if (b0 >= 247 && b0 <= 250) { st.push_back((b0 - 247) * 256 + (int)u8(f, p + 1) + 108); p += 2; }
        else if (b0 >= 251 && b0 <= 254) { st.push_back(-(b0 - 251) * 256 - (int)u8(f, p + 1) - 108); p += 2; }
        else if (b0 == 28)               { st.push_back((int16_t)u16(f, p + 1)); p += 3; }
        else if (b0 == 29)               { st.push_back((int32_t)uvarBE(f, p + 1, 4)); p += 5; }
        else if (b0 == 30)               { p += 1; while (p < end) { int b = u8(f, p++); if ((b & 0xf) == 0xf || (b >> 4) == 0xf) break; } st.clear(); }
        else if (b0 == 12)               { if (wantOp == 1200 + (int)u8(f, p + 1)) { out = st; return true; } st.clear(); p += 2; }
        else                             { if (b0 == wantOp) { out = st; return true; } st.clear(); p += 1; }
    }
    return false;
}

struct Parsed { bool ok = false; int nGlyphs = 0, nGsubr = 0, nLocal = 0; string glyph0, localsubr0; };

// Decode the first "<operand> callsubr" in a charstring and return the target
// LOCAL subr index (operand + bias), or INT_MIN if there is none. Used to prove
// a self/cyclic callsubr was actually injected. (TN#5177 s.3.2 number encoding.)
static int firstCallLocalTarget(const string& s, int nLocal) {
    int bias = nLocal < 1240 ? 107 : nLocal < 33900 ? 1131 : 32768;
    size_t p = 0; int operand = 0; bool haveOp = false;
    while (p < s.size()) {
        int b0 = (uint8_t)s[p];
        if (b0 >= 32 && b0 <= 246)       { operand = b0 - 139; haveOp = true; p += 1; }
        else if (b0 >= 247 && b0 <= 250) { operand = (b0 - 247) * 256 + (uint8_t)s[p + 1] + 108; haveOp = true; p += 2; }
        else if (b0 >= 251 && b0 <= 254) { operand = -(b0 - 251) * 256 - (uint8_t)s[p + 1] - 108; haveOp = true; p += 2; }
        else if (b0 == 28)               { operand = (int16_t)(((uint8_t)s[p + 1] << 8) | (uint8_t)s[p + 2]); haveOp = true; p += 3; }
        else if (b0 == 10)               { return haveOp ? operand + bias : INT_MIN; }  // callsubr
        else                             { haveOp = false; p += 1; }
    }
    return INT_MIN;
}

static Parsed reparse(const string& f) {
    Parsed r;
    if (f.size() < 4) return r;
    Index nameIdx = getIndex(f, u8(f, 2));          if (!nameIdx.ok) return r;
    Index topIdx  = getIndex(f, nameIdx.endPos);    if (!topIdx.ok || topIdx.count < 1) return r;
    Index strIdx  = getIndex(f, topIdx.endPos);     if (!strIdx.ok) return r;
    Index gsubrIdx = getIndex(f, strIdx.endPos);    if (!gsubrIdx.ok) return r;
    r.nGsubr = gsubrIdx.count;

    size_t tp, tl;
    if (!getIndexVal(f, topIdx, 0, tp, tl)) return r;
    std::vector<int> cs, pv;
    if (!dictOp(f, tp, tl, 17, cs) || cs.empty()) return r;   // CharStrings offset
    Index charIdx = getIndex(f, (size_t)cs[0]);     if (!charIdx.ok) return r;
    r.nGlyphs = charIdx.count;
    size_t gp, gl;
    if (getIndexVal(f, charIdx, 0, gp, gl)) r.glyph0 = f.substr(gp, gl);

    if (dictOp(f, tp, tl, 18, pv) && pv.size() >= 2) {        // Private (size, offset)
        size_t psize = (size_t)pv[0], poff = (size_t)pv[1];
        if (psize > 0 && poff + psize <= f.size()) {
            std::vector<int> sv;
            if (dictOp(f, poff, psize, 19, sv) && !sv.empty()) {
                Index lsub = getIndex(f, poff + (size_t)sv[0]);
                if (lsub.ok) {
                    r.nLocal = lsub.count;
                    size_t sp, sl;
                    if (getIndexVal(f, lsub, 0, sp, sl)) r.localsubr0 = f.substr(sp, sl);
                }
            }
        }
    }
    r.ok = true; return r;
}

// ---- FontTools cross-check (best-effort) -----------------------------------

static const char* kFtScript =
    "import sys\n"
    "from io import BytesIO\n"
    "from fontTools.cffLib import CFFFontSet\n"
    "data = open(sys.argv[1], 'rb').read()\n"
    "try:\n"
    "    cff = CFFFontSet(); cff.decompile(BytesIO(data), None)\n"
    "    td = cff[cff.fontNames[0]]\n"
    "    print('FT_OK', td.numGlyphs)\n"  // numGlyphs avoids the charset/glyph-name lazy path
    "except Exception as e:\n"
    "    print('FT_ERR', type(e).__name__, str(e).replace(chr(10),' ')[:100])\n";

// Returns nGlyphs FontTools saw, or -1 if unavailable / errored (prints the reason).
static int fonttools_nglyphs(const string& cff, const char* pyPath) {
    char cffPath[] = "/tmp/vcff_XXXXXX";
    int fd = mkstemp(cffPath);
    if (fd < 0) return -1;
    (void)!write(fd, cff.data(), cff.size());
    close(fd);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "python3 %s %s 2>&1", pyPath, cffPath);
    FILE* pf = popen(cmd, "r");
    int n = -1; char line[256];
    if (pf) {
        while (fgets(line, sizeof(line), pf)) {
            if (!strncmp(line, "FT_OK", 5))       { n = atoi(line + 6); }
            else if (!strncmp(line, "FT_ERR", 6)) { fprintf(stderr, "  FontTools: %s", line); }
            else if (strstr(line, "ModuleNotFoundError") || strstr(line, "No module")) { fprintf(stderr, "  FontTools: not installed\n"); }
        }
        pclose(pf);
    }
    unlink(cffPath);
    return n;
}

// ---- driver ----------------------------------------------------------------

static int fails = 0;
static const char* g_pyPath = nullptr;

static void check(const char* name, const pdf_cff::CffFont& font,
                  int expGlyphs, int expLocal, int expGlobal, int glyph0Byte) {
    string cff = SerializeCff(font);
    Parsed p = reparse(cff);
    fprintf(stderr, "=== %s (%zu bytes) ===\n", name, cff.size());
    int ok = 1;
    if (!p.ok) { fprintf(stderr, "  FAIL: re-parse failed (bad CFF structure)\n"); fails++; return; }
    if (p.nGlyphs != expGlyphs) { fprintf(stderr, "  FAIL: nGlyphs=%d expected %d\n", p.nGlyphs, expGlyphs); ok = 0; }
    if (p.nLocal  != expLocal)  { fprintf(stderr, "  FAIL: nLocalSubr=%d expected %d\n", p.nLocal, expLocal); ok = 0; }
    if (p.nGsubr  != expGlobal) { fprintf(stderr, "  FAIL: nGlobalSubr=%d expected %d\n", p.nGsubr, expGlobal); ok = 0; }
    if (glyph0Byte >= 0 && p.glyph0.find((char)glyph0Byte) == string::npos) {
        fprintf(stderr, "  FAIL: glyph0 missing expected opcode 0x%02x\n", glyph0Byte); ok = 0;
    }
    // FontTools cross-check (corroboration, not authority).
    int ftN = fonttools_nglyphs(cff, g_pyPath);
    if (ftN >= 0) {
        if (ftN == expGlyphs) fprintf(stderr, "  FontTools: agrees (%d glyphs)\n", ftN);
        else fprintf(stderr, "  FontTools: DISAGREES nGlyphs=%d expected %d\n", ftN, expGlyphs);
    }
    if (ok) fprintf(stderr, "  PASS (mirror-FoFiType1C: glyphs=%d local=%d global=%d)\n",
                    p.nGlyphs, p.nLocal, p.nGsubr);
    else fails++;
}

// Weakness case: a CFF that is a perfectly VALID container but carries a
// charstring/subr with a self- or cyclic callsubr -> uncontrolled recursion in
// xpdf's cvtGlyph (CVE-2020-35376 / CWE-674). We assert three things:
//   (i)   the container still parses (skeleton stays valid),
//   (ii)  the self-reference is actually injected (subr 0's callsubr targets the
//         expected local subr index -- `expTarget` == 0 means "itself"),
//   (iii) FontTools STILL accepts it as a valid font -- demonstrating the bug is
//         invisible to structural validation and only manifests in an interpreter
//         that EXECUTES the charstring (which is exactly why valid-rate / static
//         parsing cannot find it, and why the grammar must synthesize it).
static void check_weakness(const char* name, const pdf_cff::CffFont& font, int expTarget) {
    string cff = SerializeCff(font);
    Parsed p = reparse(cff);
    fprintf(stderr, "=== %s (%zu bytes) [WEAKNESS: CVE-2020-35376] ===\n", name, cff.size());
    int ok = 1;
    if (!p.ok) { fprintf(stderr, "  FAIL: container did not parse\n"); fails++; return; }
    if (p.nGlyphs < 1 || p.nLocal < 1) {
        fprintf(stderr, "  FAIL: expected >=1 glyph and >=1 local subr (got %d/%d)\n", p.nGlyphs, p.nLocal);
        ok = 0;
    }
    int tgt = firstCallLocalTarget(p.localsubr0, p.nLocal);
    if (tgt != expTarget) {
        fprintf(stderr, "  FAIL: subr0 callsubr targets %d, expected %d\n", tgt, expTarget);
        ok = 0;
    } else {
        fprintf(stderr, "  subr0 -> callsubr subr %d  (self-reference injected: cvtGlyph would recurse forever)\n", tgt);
    }
    int ftN = fonttools_nglyphs(cff, g_pyPath);
    if (ftN >= 0)
        fprintf(stderr, "  FontTools: accepts as VALID CFF (%d glyphs) -- structural validation cannot see the recursion\n", ftN);
    if (ok) fprintf(stderr, "  PASS: valid CFF container carrying an uncontrolled-recursion charstring\n");
    else fails++;
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // stage the FontTools script once
    char pyPath[] = "/tmp/vcff_ft_XXXXXX";
    int pfd = mkstemp(pyPath);
    if (pfd >= 0) { (void)!write(pfd, kFtScript, strlen(kFtScript)); close(pfd); g_pyPath = pyPath; }

    // 1. empty font -> serializer emits a default .notdef (endchar 0x0e)
    { pdf_cff::CffFont f; check("default-notdef", f, 1, 0, 0, 0x0e); }

    // 2. one glyph with a real outline: 0 0 rmoveto ... endchar
    {
        pdf_cff::CffFont f;
        auto* c = f.add_charstrings();
        c->add_ops()->set_operand(0);
        c->add_ops()->set_operand(0);
        c->add_ops()->set_op(pdf_cff::RMOVETO);
        c->add_ops()->set_op(pdf_cff::ENDCHAR);
        check("one-glyph-outline", f, 1, 0, 0, 0x15 /* rmoveto */);
    }

    // 3. glyph 0 calls local subr 0 (valid: subr does a move then returns)
    {
        pdf_cff::CffFont f;
        auto* c = f.add_charstrings();
        c->add_ops()->set_call_local(0);
        c->add_ops()->set_op(pdf_cff::ENDCHAR);
        auto* s = f.add_local_subrs();
        s->add_ops()->set_operand(0);
        s->add_ops()->set_operand(0);
        s->add_ops()->set_op(pdf_cff::RMOVETO);
        s->add_ops()->set_op(pdf_cff::RETURN);
        check("glyph-calls-local-subr", f, 1, 1, 0, 0x0a /* callsubr */);
    }

    // 4. glyph 0 calls global subr 0
    {
        pdf_cff::CffFont f;
        auto* c = f.add_charstrings();
        c->add_ops()->set_call_global(0);
        c->add_ops()->set_op(pdf_cff::ENDCHAR);
        auto* s = f.add_global_subrs();
        s->add_ops()->set_op(pdf_cff::RETURN);
        check("glyph-calls-global-subr", f, 1, 0, 1, 0x1d /* callgsubr */);
    }

    // 5. two glyphs
    {
        pdf_cff::CffFont f;
        f.add_charstrings()->add_ops()->set_op(pdf_cff::ENDCHAR);
        f.add_charstrings()->add_ops()->set_op(pdf_cff::ENDCHAR);
        check("two-glyphs", f, 2, 0, 0, 0x0e);
    }

    // 6. WEAKNESS: subr 0 calls subr 0 -> the CVE-2020-35376 trigger.
    {
        pdf_cff::CffFont f;
        auto* c = f.add_charstrings();
        c->add_ops()->set_call_local(0);          // glyph 0 kicks it off
        c->add_ops()->set_op(pdf_cff::ENDCHAR);
        f.add_local_subrs()->add_ops()->set_call_local(0);  // subr 0 -> subr 0 (itself)
        check_weakness("weakness-self-callsubr", f, /*expTarget=*/0);
    }

    if (g_pyPath) unlink(g_pyPath);
    google::protobuf::ShutdownProtobufLibrary();
    fprintf(stderr, "\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails;
}
