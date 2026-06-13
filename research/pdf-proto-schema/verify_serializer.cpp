#include <cstdio>
#include <fstream>
#include "pdf.pb.h"
#include "serializer.h"

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    pdf_proto::PdfDocument doc;
    pdf_proto::Page* p = doc.add_pages();
    p->set_width(612);
    p->set_height(792);

    std::string bytes = SerializePdf(doc);

    // print to stdout so you can pipe to pdftotext
    fwrite(bytes.data(), 1, bytes.size(), stdout);

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
