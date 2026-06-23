// Serializer + schema validation (run BEFORE the AFL++ migration).
//
// For each LPM corpus proto (text-format PdfDocument), serialize it and check:
//
//   (1) INTERNAL xref invariant -- parser-independent ground truth, the serializer's
//       core contribution. Re-parse the output bytes: the /startxref offset must point
//       at the "xref" keyword, /Size must equal the entry count, and every in-use xref
//       entry's offset must land exactly on "<n> 0 obj". This holds for EVERY input
//       regardless of field mutations (xref offsets don't depend on length_delta etc.),
//       so it must be 100% -- any failure is a serializer bug.
//
//   (2) EXTERNAL parser panel -- three independent implementations:
//         qpdf --check  (strict, no aggressive recovery; exit 0=clean, 3=warnings, 2=errors)
//         mutool info   (MuPDF;  exit 0 = parsed)
//         pdfinfo       (poppler; "Pages:" reported)
//       Reports per-parser acceptance + 3-way consensus -- a far stronger validity
//       claim than any single lenient parser.
//
// Usage: check_serializer <corpus_dir> [max_samples]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <google/protobuf/text_format.h>
#include "pdf.pb.h"
#include "serializer.h"

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

// Parser-independent xref invariant check on the serialized bytes.
static bool xref_ok(const std::string& s, std::string& why) {
    size_t sx = s.rfind("startxref\n");
    if (sx == std::string::npos) { why = "no startxref"; return false; }
    long off = atol(s.c_str() + sx + 10);                 // claimed xref byte offset
    if (off < 0 || (size_t)off + 5 > s.size() || s.compare(off, 5, "xref\n") != 0) {
        why = "startxref does not point at 'xref'"; return false;
    }
    // subsection header "0 <count>\n"
    size_t p = off + 5;
    if (s.compare(p, 2, "0 ") != 0) { why = "bad xref subsection header"; return false; }
    long count = atol(s.c_str() + p + 2);
    size_t nl = s.find('\n', p);
    if (nl == std::string::npos) { why = "bad xref header eol"; return false; }
    p = nl + 1;
    // count entries, 20 bytes each: "OOOOOOOOOO GGGGG t \n"
    for (long i = 0; i < count; i++, p += 20) {
        if (p + 20 > s.size()) { why = "xref truncated"; return false; }
        char t = s[p + 17];
        if (t == 'n') {
            long o = atol(s.substr(p, 10).c_str());
            std::string expect = std::to_string(i) + " 0 obj";
            if (o < 0 || (size_t)o + expect.size() > s.size() ||
                s.compare(o, expect.size(), expect) != 0) {
                why = "obj " + std::to_string(i) + " xref offset mismatch"; return false;
            }
        }
    }
    // /Size must equal entry count
    std::string need = "/Size " + std::to_string(count);
    if (s.find(need) == std::string::npos) { why = "/Size != xref count"; return false; }
    return true;
}

static int run_exit(const std::string& cmd) {
    int rc = system((cmd + " >/dev/null 2>&1").c_str());
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

int main(int argc, char** argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    if (argc < 2) { fprintf(stderr, "usage: %s <corpus_dir> [max_samples]\n", argv[0]); return 1; }
    const char* dir = argv[1];
    long maxs = (argc >= 3) ? atol(argv[2]) : 0;

    DIR* d = opendir(dir);
    if (!d) { fprintf(stderr, "cannot open %s\n", dir); return 1; }
    google::protobuf::TextFormat::Parser parser;
    parser.AllowPartialMessage(true); parser.AllowUnknownField(true);

    long total = 0, xrefbad = 0, qpdf_clean = 0, qpdf_ok = 0, mu_ok = 0, pop_ok = 0, all3 = 0, skipped = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        if (maxs && total >= maxs) break;
        std::string raw = read_file(std::string(dir) + "/" + e->d_name);
        if (raw.empty()) continue;
        pdf_proto::PdfDocument doc;
        if (!parser.ParseFromString(raw, &doc) || doc.pages_size() == 0) { skipped++; continue; }

        std::string pdf = SerializePdf(doc);
        total++;

        std::string why;
        if (!xref_ok(pdf, why)) {
            xrefbad++;
            if (xrefbad <= 10) fprintf(stderr, "[XREF BUG] %s: %s\n", e->d_name, why.c_str());
        }

        char path[] = "/tmp/cs_XXXXXX";
        int fd = mkstemp(path); if (fd < 0) continue;
        ssize_t w = write(fd, pdf.data(), pdf.size()); (void)w; close(fd);

        int q = run_exit(std::string("qpdf --check ") + path);
        int m = run_exit(std::string("mutool info ") + path);
        std::string pc = std::string("pdfinfo ") + path + " 2>/dev/null | grep -q Pages:";
        int pp = run_exit(pc);
        unlink(path);

        bool qok = (q == 0 || q == 3), mok = (m == 0), popok = (pp == 0);
        if (q == 0) qpdf_clean++;
        if (qok) qpdf_ok++;
        if (mok) mu_ok++;
        if (popok) pop_ok++;
        if (qok && mok && popok) all3++;
    }
    closedir(d);

    printf("\n=== Serializer/schema validation (%s) ===\n", dir);
    printf("samples (proto->PDF):        %ld   (skipped unparseable protos: %ld)\n", total, skipped);
    printf("\n-- INTERNAL xref invariant (must be 100%%) --\n");
    printf("xref byte-offsets correct:   %ld/%ld (%.2f%%)   bugs: %ld\n",
           total - xrefbad, total, total ? 100.0*(total-xrefbad)/total : 0, xrefbad);
    printf("\n-- EXTERNAL parser panel --\n");
    printf("qpdf --check CLEAN (exit 0):  %ld (%.1f%%)\n", qpdf_clean, total?100.0*qpdf_clean/total:0);
    printf("qpdf --check OK (0 or warn):  %ld (%.1f%%)\n", qpdf_ok, total?100.0*qpdf_ok/total:0);
    printf("mutool (MuPDF) parsed:        %ld (%.1f%%)\n", mu_ok, total?100.0*mu_ok/total:0);
    printf("pdfinfo (poppler) parsed:     %ld (%.1f%%)\n", pop_ok, total?100.0*pop_ok/total:0);
    printf("ALL THREE accept (consensus): %ld (%.1f%%)\n", all3, total?100.0*all3/total:0);
    google::protobuf::ShutdownProtobufLibrary();
    return xrefbad ? 2 : 0;   // nonzero exit if any serializer xref bug found
}
