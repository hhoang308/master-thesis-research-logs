#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "pdf.pb.h"
#include "serializer.h"

static int run_test(const char* name, pdf_proto::PdfDocument& doc) {
    std::string bytes = SerializePdf(doc);
    char path[] = "/tmp/verify_pdf_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { fprintf(stderr, "[%s] mkstemp failed\n", name); return 1; }
    write(fd, bytes.data(), bytes.size());
    close(fd);

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "pdfinfo %s 2>&1", path);
    fprintf(stderr, "\n=== %s ===\n", name);
    FILE* f = popen(cmd, "r");
    char line[256];
    int ok = 0;
    while (fgets(line, sizeof(line), f)) {
        fputs(line, stderr);
        if (strstr(line, "Pages:")) ok = 1;
    }
    pclose(f);
    unlink(path);
    if (!ok) { fprintf(stderr, "[%s] FAIL\n", name); return 1; }
    fprintf(stderr, "[%s] PASS\n", name);
    return 0;
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    int failures = 0;

    // Test 1: plain page, no content stream (baseline -- must stay green)
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        failures += run_test("plain-page-no-stream", doc);
    }

    // Test 2: page with raw (NONE) content stream
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::ContentStream* cs = p->mutable_content_stream();
        cs->set_filter(pdf_proto::ContentStream::NONE);
        cs->set_raw_content("BT /F1 12 Tf 100 700 Td (hello) Tj ET");
        failures += run_test("raw-content-stream-NONE-filter", doc);
    }

    // Test 3: page with FlateDecode content stream
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::ContentStream* cs = p->mutable_content_stream();
        cs->set_filter(pdf_proto::ContentStream::FLATE);
        cs->set_raw_content("BT /F1 12 Tf 100 700 Td (flate test) Tj ET");
        failures += run_test("content-stream-FlateDecode", doc);
    }

    // Test 4: two pages, second with FlateDecode
    {
        pdf_proto::PdfDocument doc;
        doc.add_pages()->set_width(612);
        pdf_proto::Page* p2 = doc.add_pages();
        p2->set_width(595); p2->set_height(842);
        pdf_proto::ContentStream* cs = p2->mutable_content_stream();
        cs->set_filter(pdf_proto::ContentStream::FLATE);
        cs->set_raw_content("q 1 0 0 1 0 0 cm Q");
        failures += run_test("two-pages-second-FlateDecode", doc);
    }

    google::protobuf::ShutdownProtobufLibrary();
    return failures;
}
