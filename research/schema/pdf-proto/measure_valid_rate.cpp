// Valid-input-rate measurement (thesis criterion 1).
//
// Reads a directory of libFuzzer/LPM corpus files (text-format PdfDocument protos),
// serializes each with SerializePdf(), and validates the resulting PDF with `pdfinfo`
// (poppler -- independent of our fuzz targets xpdf/PoDoFo/qpdf). Reports:
//   CLEAN     : pdfinfo reports Pages and prints NO syntax error/warning
//   PARSEABLE : pdfinfo reports Pages but with syntax warnings/errors (structure OK,
//               stream/content issues -- e.g. from length_delta / skip_compression)
//   FAILED    : pdfinfo could not report Pages (structurally invalid)
//
// "Valid input rate" = (CLEAN + PARSEABLE) / total, i.e. the fraction of generated
// PDFs whose structure a real reader can parse. CLEAN alone is the strict rate.
//
// Usage: measure_valid_rate <corpus_dir> [max_samples]
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

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

// Run pdfinfo on a PDF file; classify validity by its output.
enum Verdict { CLEAN, PARSEABLE, FAILED };
static Verdict validate(const std::string& pdf_path) {
    std::string cmd = "pdfinfo '" + pdf_path + "' 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return FAILED;
    char line[512];
    bool has_pages = false, has_err = false;
    while (fgets(line, sizeof(line), p)) {
        if (strstr(line, "Pages:")) has_pages = true;
        if (strstr(line, "Syntax Error") || strstr(line, "Syntax Warning") ||
            strstr(line, "Error") || strstr(line, "Internal Error"))
            has_err = true;
    }
    pclose(p);
    if (!has_pages) return FAILED;
    return has_err ? PARSEABLE : CLEAN;
}

int main(int argc, char** argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    if (argc < 2) { fprintf(stderr, "usage: %s <corpus_dir> [max_samples]\n", argv[0]); return 1; }
    const char* dir = argv[1];
    long max_samples = (argc >= 3) ? atol(argv[2]) : 0;  // 0 = all

    DIR* d = opendir(dir);
    if (!d) { fprintf(stderr, "cannot open dir %s\n", dir); return 1; }

    google::protobuf::TextFormat::Parser parser;
    parser.AllowPartialMessage(true);
    parser.AllowUnknownField(true);

    long total = 0, clean = 0, parseable = 0, failed = 0, unparsed_proto = 0;
    char tmpl[] = "/tmp/vr_XXXXXX";
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        if (max_samples && total >= max_samples) break;
        std::string path = std::string(dir) + "/" + e->d_name;
        std::string raw = read_file(path);
        if (raw.empty()) continue;

        pdf_proto::PdfDocument doc;
        if (!parser.ParseFromString(raw, &doc) || doc.pages_size() == 0) {
            unparsed_proto++;   // not a (usable) proto -- skip from rate
            continue;
        }
        std::string bytes = SerializePdf(doc);

        char path2[] = "/tmp/vr_XXXXXX";
        int fd = mkstemp(path2);
        if (fd < 0) continue;
        ssize_t wr = write(fd, bytes.data(), bytes.size());
        close(fd);
        (void)wr;
        Verdict v = validate(path2);
        unlink(path2);

        total++;
        if (v == CLEAN) clean++;
        else if (v == PARSEABLE) parseable++;
        else failed++;
    }
    closedir(d);
    (void)tmpl;

    long valid = clean + parseable;
    printf("=== Valid-input-rate (%s) ===\n", dir);
    printf("samples (proto -> PDF):    %ld\n", total);
    printf("skipped (unparseable proto): %ld\n", unparsed_proto);
    printf("CLEAN     (no warnings):   %ld (%.1f%%)\n", clean,     total ? 100.0*clean/total : 0);
    printf("PARSEABLE (with warnings): %ld (%.1f%%)\n", parseable, total ? 100.0*parseable/total : 0);
    printf("FAILED    (no Pages):      %ld (%.1f%%)\n", failed,    total ? 100.0*failed/total : 0);
    printf("VALID rate (CLEAN+PARSEABLE): %.1f%%\n",    total ? 100.0*valid/total : 0);
    printf("STRICT rate (CLEAN only):     %.1f%%\n",    total ? 100.0*clean/total : 0);
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
