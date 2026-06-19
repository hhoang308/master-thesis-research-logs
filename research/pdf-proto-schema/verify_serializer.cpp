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
        pdf_proto::ContentStream* cs = p->add_content_streams();
        cs->set_filter(pdf_proto::ContentStream::NONE);
        cs->set_raw_content("BT /F1 12 Tf 100 700 Td (hello) Tj ET");
        failures += run_test("raw-content-stream-NONE-filter", doc);
    }

    // Test 3: page with FlateDecode content stream
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::ContentStream* cs = p->add_content_streams();
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
        pdf_proto::ContentStream* cs = p2->add_content_streams();
        cs->set_filter(pdf_proto::ContentStream::FLATE);
        cs->set_raw_content("q 1 0 0 1 0 0 cm Q");
        failures += run_test("two-pages-second-FlateDecode", doc);
    }

    // Test 5 (P1): page with two fonts -- driver stream must force makeFont,
    // PDF must still parse (Resources/Font dict + driver /Contents are well-formed).
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::Font* f0 = p->add_fonts();
        f0->set_subtype(pdf_proto::Font::TYPE1);
        f0->set_base_font("Helvetica");
        pdf_proto::Font* f1 = p->add_fonts();
        f1->set_subtype(pdf_proto::Font::TRUETYPE);
        f1->set_base_font("Arial");
        failures += run_test("page-with-two-fonts", doc);
    }

    // Test 6 (P1): font dict malformations + page also has a fuzzer stream.
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::ContentStream* cs = p->add_content_streams();
        cs->set_raw_content("q Q");
        pdf_proto::Font* f0 = p->add_fonts();
        f0->set_omit_type(true);
        f0->set_omit_subtype(true);
        f0->set_base_font("weird name/with()delims");  // exercises name escaping
        failures += run_test("font-malformed-dict-with-stream", doc);
    }

    // Test 7 (P1.5): font with /FontDescriptor + embedded Type1 (/FontFile) program.
    // PDF must be structurally valid (xref must account for the extra stream object).
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::Font* f = p->add_fonts();
        f->set_subtype(pdf_proto::Font::TYPE1);
        f->set_base_font("MyEmbeddedFont");
        pdf_proto::FontDescriptor* fd = f->mutable_font_descriptor();
        fd->set_flags(4);
        fd->add_font_bbox(0); fd->add_font_bbox(-200);
        fd->add_font_bbox(1000); fd->add_font_bbox(900);
        pdf_proto::EmbeddedFontFile* ff = fd->mutable_font_file();
        ff->set_key(pdf_proto::EmbeddedFontFile::FONTFILE);
        // Minimal Type1 PFA header -- FoFiIdentifier matches "%!" -> FoFiType1.
        ff->set_program("%!PS-AdobeFont-1.0: MyEmbeddedFont 001.001\n"
                        "/FontType 1 def\n/FontMatrix [0.001 0 0 0.001 0 0] def\n");
        failures += run_test("font-FontFile-Type1-embedded", doc);
    }

    // Test 8 (P1.5): FontFile3 (CFF) with /Subtype, plus FontFile2 (TrueType) on a
    // second font -- exercises both keys and the /Subtype-only-for-FontFile3 path.
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::Font* f0 = p->add_fonts();
        f0->set_base_font("CFFFont");
        pdf_proto::EmbeddedFontFile* ff0 =
            f0->mutable_font_descriptor()->mutable_font_file();
        ff0->set_key(pdf_proto::EmbeddedFontFile::FONTFILE3);
        ff0->set_subtype("Type1C");
        ff0->set_program("\x01\x00\x04\x01", 4);  // CFF header magic-ish
        pdf_proto::Font* f1 = p->add_fonts();
        f1->set_subtype(pdf_proto::Font::TRUETYPE);
        f1->set_base_font("TTFont");
        pdf_proto::EmbeddedFontFile* ff1 =
            f1->mutable_font_descriptor()->mutable_font_file();
        ff1->set_key(pdf_proto::EmbeddedFontFile::FONTFILE2);
        ff1->set_program(std::string("\x00\x01\x00\x00", 4) + "tabledata");  // TrueType sfnt magic
        failures += run_test("font-FontFile3-CFF-and-FontFile2-TrueType", doc);
    }

    // Test 9 (P1.5): MULTI-PAGE with embedded fonts -- regression for the xref
    // object-numbering fix (interleaved assignment used to scramble offsets).
    {
        pdf_proto::PdfDocument doc;
        for (int pg = 0; pg < 3; pg++) {
            pdf_proto::Page* p = doc.add_pages();
            p->set_width(612); p->set_height(792);
            pdf_proto::ContentStream* cs = p->add_content_streams();
            cs->set_raw_content("q Q");
            pdf_proto::Font* f = p->add_fonts();
            f->set_base_font("PageFont");
            pdf_proto::EmbeddedFontFile* ff =
                f->mutable_font_descriptor()->mutable_font_file();
            ff->set_key(pdf_proto::EmbeddedFontFile::FONTFILE);
            ff->set_program("%!PS-AdobeFont-1.0: PageFont\n");
        }
        failures += run_test("multipage-embedded-fonts-xref", doc);
    }

    // Test 10 (Step A): 8-bit font with /Encoding+/Differences, /Widths, /ToUnicode.
    // Must stay a structurally valid PDF (extra ToUnicode stream object in xref).
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::Font* f = p->add_fonts();
        f->set_subtype(pdf_proto::Font::TYPE1);
        f->set_base_font("EncFont");
        f->set_base_encoding(pdf_proto::Font::WINANSI);
        pdf_proto::Font::EncodingDiff* d0 = f->add_differences();
        d0->set_code(65); d0->set_name("A");
        pdf_proto::Font::EncodingDiff* d1 = f->add_differences();
        d1->set_code(97); d1->set_name("a");
        f->set_first_char(65);
        f->add_widths(500); f->add_widths(600); f->add_widths(700);
        f->set_to_unicode("/CIDInit /ProcSet findresource begin\n"
                          "1 begincodespacerange <00> <ff> endcodespacerange\n"
                          "1 beginbfchar <41> <0041> endbfchar\nend\n");
        failures += run_test("font-encoding-differences-widths-tounicode", doc);
    }

    // Test 11 (Step B): Type0/CID font, /CIDToGIDMap = /Identity name, /W array.
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::Font* f = p->add_fonts();
        f->set_subtype(pdf_proto::Font::TYPE0);
        f->set_base_font("CIDFontIdentity");
        pdf_proto::Font::CidFont* cid = f->mutable_cid();
        cid->set_cid_subtype(pdf_proto::Font::CidFont::CIDFONTTYPE2);
        cid->set_encoding("Identity-H");
        cid->add_w(0); cid->add_w(2); cid->add_w(1000);  // CIDs 0..2 width 1000
        cid->set_dw(1000);
        pdf_proto::FontDescriptor* fd = cid->mutable_descendant_descriptor();
        fd->set_flags(4);
        failures += run_test("cidfont-identity-cidtogid-with-W", doc);
    }

    // Test 12 (Step B): Type0/CID font with /CIDToGIDMap as a stream + ToUnicode;
    // two pages to stress xref ordering across the new descendant/cidgid classes.
    {
        pdf_proto::PdfDocument doc;
        for (int pg = 0; pg < 2; pg++) {
            pdf_proto::Page* p = doc.add_pages();
            p->set_width(612); p->set_height(792);
            pdf_proto::ContentStream* cs = p->add_content_streams();
            cs->set_raw_content("q Q");
            pdf_proto::Font* f = p->add_fonts();
            f->set_subtype(pdf_proto::Font::TYPE0);
            f->set_base_font("CIDFontStream");
            f->set_to_unicode("/CIDInit /ProcSet findresource begin end\n");
            pdf_proto::Font::CidFont* cid = f->mutable_cid();
            cid->set_encoding("Identity-H");
            cid->set_cid_to_gid_map_stream(std::string("\x00\x00\x00\x01\x00\x02", 6));
            cid->add_w(0); cid->add_w(1); cid->add_w(500);
        }
        failures += run_test("cidfont-stream-cidtogid-multipage", doc);
    }

    google::protobuf::ShutdownProtobufLibrary();
    return failures;
}
