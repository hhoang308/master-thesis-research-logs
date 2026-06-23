// Convert an LPM corpus/crash file (text-format PdfDocument proto) into the actual
// PDF bytes the fuzzer fed the target -- i.e. run SerializePdf() on it. Useful for
// triaging a crash-input.txtpb: get the real .pdf and open it in any reader/tool.
//
// Usage: proto2pdf <input.txtpb> [output.pdf]
//   (no output path -> writes PDF bytes to stdout)
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <google/protobuf/text_format.h>
#include "pdf.pb.h"
#include "serializer.h"

int main(int argc, char** argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <input.txtpb> [output.pdf]\n", argv[0]);
        return 1;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    std::ostringstream ss; ss << in.rdbuf();
    std::string raw = ss.str();

    // Parse leniently, exactly like libprotobuf-mutator does for the corpus.
    google::protobuf::TextFormat::Parser parser;
    parser.AllowPartialMessage(true);
    parser.AllowUnknownField(true);
    pdf_proto::PdfDocument doc;
    if (!parser.ParseFromString(raw, &doc)) {
        fprintf(stderr, "warning: text-proto parse had errors; serializing what parsed\n");
    }

    std::string pdf = SerializePdf(doc);

    if (argc >= 3) {
        std::ofstream out(argv[2], std::ios::binary);
        out.write(pdf.data(), pdf.size());
        fprintf(stderr, "wrote %zu bytes to %s (%d page(s))\n",
                pdf.size(), argv[2], doc.pages_size());
    } else {
        fwrite(pdf.data(), 1, pdf.size(), stdout);
    }
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
