// Inspect PdfDocument corpus seeds for structured CFF / callsubr patterns.
//
// Usage: inspect_cff_seed <seed.txtpb|seed.pb> [...]
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <google/protobuf/text_format.h>
#include "pdf.pb.h"

namespace {

struct CffStats {
    int font_files = 0;
    int structured = 0;
    int recursive_local = 0;
};

std::string ReadFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool ParseSeed(const std::string& raw, pdf_proto::PdfDocument* doc) {
    google::protobuf::TextFormat::Parser parser;
    parser.AllowPartialMessage(true);
    parser.AllowUnknownField(true);
    if (parser.ParseFromString(raw, doc)) return true;
    doc->Clear();
    return doc->ParsePartialFromString(raw);
}

bool OpCallsLocal(const pdf_cff::Op& op, int target) {
    return op.kind_case() == pdf_cff::Op::kCallLocal && op.call_local() == target;
}

bool HasRecursiveLocalSubr(const pdf_cff::CffFont& cff) {
    for (int i = 0; i < cff.local_subrs_size(); ++i) {
        const pdf_cff::Charstring& subr = cff.local_subrs(i);
        for (const auto& op : subr.ops()) {
            if (OpCallsLocal(op, i)) return true;
        }
    }
    return false;
}

void InspectFontFile(const pdf_proto::EmbeddedFontFile& ff, CffStats* stats) {
    stats->font_files++;
    if (ff.has_cff()) {
        stats->structured++;
        if (HasRecursiveLocalSubr(ff.cff())) stats->recursive_local++;
    }
}

void InspectDescriptor(const pdf_proto::FontDescriptor& fd, CffStats* stats) {
    if (fd.has_font_file()) InspectFontFile(fd.font_file(), stats);
}

CffStats InspectDocument(const pdf_proto::PdfDocument& doc) {
    CffStats stats;
    for (const auto& page : doc.pages()) {
        for (const auto& font : page.fonts()) {
            if (font.has_font_descriptor()) InspectDescriptor(font.font_descriptor(), &stats);
            if (font.has_cid() && font.cid().has_descendant_descriptor()) {
                InspectDescriptor(font.cid().descendant_descriptor(), &stats);
            }
        }
    }
    return stats;
}

}  // namespace

int main(int argc, char** argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <seed.txtpb|seed.pb> [...]\n", argv[0]);
        return 1;
    }

    int missing_recursive = 0;
    for (int i = 1; i < argc; ++i) {
        pdf_proto::PdfDocument doc;
        const std::string raw = ReadFile(argv[i]);
        if (raw.empty() || !ParseSeed(raw, &doc)) {
            std::printf("%s: parse_failed\n", argv[i]);
            missing_recursive++;
            continue;
        }
        const CffStats stats = InspectDocument(doc);
        std::printf("%s: pages=%d font_files=%d structured_cff=%d recursive_local=%d\n",
                    argv[i], doc.pages_size(), stats.font_files, stats.structured,
                    stats.recursive_local);
        if (stats.recursive_local == 0) missing_recursive++;
    }

    google::protobuf::ShutdownProtobufLibrary();
    return missing_recursive ? 2 : 0;
}
