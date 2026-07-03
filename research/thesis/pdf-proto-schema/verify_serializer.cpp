#include <cstdio>
#include <cstring>
#include <string>
#include <initializer_list>
#include <unistd.h>
#include <sys/wait.h>
#include "pdf.pb.h"
#include "serializer.h"

// Run one serializer test case.
//   expect: substrings that MUST appear in the serialized PDF -- the feature this
//   test is named for (e.g. "/FontFile", "/DCTDecode"). This guards against a
//   serializer bug that silently DROPS a feature: the pdfinfo "Pages:" check alone
//   would still pass in that case, because a reader can render a valid page while
//   the embedded font / image / CMap is missing. The content check re-reads the
//   raw output bytes (it does not modify them) and fails if a marker is absent.
static int run_test(const char* name, pdf_proto::PdfDocument& doc,
                    std::initializer_list<const char*> expect = {}) {
    std::string bytes = SerializePdf(doc);
    fprintf(stderr, "\n=== %s ===\n", name);

    // (1) Content check -- every expected marker must be present in the output.
    for (const char* marker : expect) {
        if (bytes.find(marker) == std::string::npos) {
            fprintf(stderr, "[%s] FAIL: expected marker \"%s\" not found in serialized output\n",
                    name, marker);
            return 1;
        }
    }

    // (2) Structural check -- the PDF must open in a reader.
    char path[] = "/tmp/verify_pdf_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { fprintf(stderr, "[%s] mkstemp failed\n", name); return 1; }
    // write() may short-write; loop until the whole buffer is on disk.
    size_t off = 0;
    while (off < bytes.size()) {
        ssize_t w = write(fd, bytes.data() + off, bytes.size() - off);
        if (w <= 0) {
            fprintf(stderr, "[%s] FAIL: short write to temp file\n", name);
            close(fd); unlink(path); return 1;
        }
        off += (size_t)w;
    }
    close(fd);

    // (2) Structural check -- qpdf --check validates xref offsets and object
    // structure. Unlike pdfinfo/poppler (which silently reconstruct a broken xref
    // and would hide exactly the bugs tests 9/12/15 target), qpdf reports xref/
    // stream-boundary damage explicitly.
    //
    // Subtlety: qpdf --check ALSO tries to decode stream data, so an intentionally
    // junk filtered payload (e.g. the fake JPEG in the DCT-image test, or a bogus
    // CFF/TrueType program) makes qpdf warn (exit 3) even though the PDF *skeleton*
    // is perfect. We must not fail on that -- embedding junk stream data is the
    // whole point of the fuzzing serializer. Both broken-xref and junk-stream exit
    // 3, so the exit code alone can't separate them; we key on qpdf's message.
    // Structural-damage phrases ("damaged", "reconstruct", "expected endstream")
    // never appear for a mere stream-decode warning.
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "qpdf --check %s 2>&1", path);
    FILE* f = popen(cmd, "r");
    if (!f) { fprintf(stderr, "[%s] popen(qpdf) failed\n", name); unlink(path); return 1; }
    char line[512];
    int structural_damage = 0;
    while (fgets(line, sizeof(line), f)) {
        fputs(line, stderr);
        if (strstr(line, "damaged") || strstr(line, "reconstruct") ||
            strstr(line, "expected endstream")) {
            structural_damage = 1;
        }
    }
    int status = pclose(f);
    unlink(path);

    if (status == -1 || !WIFEXITED(status)) {
        fprintf(stderr, "[%s] FAIL: qpdf did not run to completion\n", name);
        return 1;
    }
    int code = WEXITSTATUS(status);
    if (code == 127) {  // shell could not find qpdf -- environment gap, not a serializer bug
        fprintf(stderr, "[%s] WARN: qpdf not found -- structural check skipped "
                        "(content check passed)\n", name);
        return 0;
    }
    if (code == 2) {    // qpdf hit an unrecoverable error
        fprintf(stderr, "[%s] FAIL: qpdf --check errored (exit 2)\n", name);
        return 1;
    }
    if (structural_damage) {  // xref / stream-boundary damage in the serializer skeleton
        fprintf(stderr, "[%s] FAIL: qpdf reported xref/structure damage\n", name);
        return 1;
    }
    // exit 0, or exit 3 whose only warnings are stream-content decode -> skeleton OK.
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
        failures += run_test("plain-page-no-stream", doc, {"/Type /Page"});
    }

    // Test 2: page with raw (NONE) content stream
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::ContentStream* cs = p->add_content_streams();
        cs->set_filter(pdf_proto::ContentStream::NONE);
        cs->set_raw_content("BT /F1 12 Tf 100 700 Td (hello) Tj ET");
        // NONE filter -> raw_content is written verbatim, so the text appears.
        failures += run_test("raw-content-stream-NONE-filter", doc, {"hello"});
    }

    // Test 3: page with FlateDecode content stream
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::ContentStream* cs = p->add_content_streams();
        cs->set_filter(pdf_proto::ContentStream::FLATE);
        cs->set_raw_content("BT /F1 12 Tf 100 700 Td (flate test) Tj ET");
        failures += run_test("content-stream-FlateDecode", doc, {"/FlateDecode"});
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
        failures += run_test("two-pages-second-FlateDecode", doc, {"/FlateDecode"});
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
        // both fonts must be emitted -> both base names present.
        failures += run_test("page-with-two-fonts", doc, {"Helvetica", "Arial"});
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
        // The driver/content stream must survive; the dict malformation itself
        // (missing /Type /Font, /Subtype) is an absence -- a "must NOT contain"
        // check is the natural next step (the (b)-side weakness-injection test).
        failures += run_test("font-malformed-dict-with-stream", doc, {"q Q"});
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
        failures += run_test("font-FontFile-Type1-embedded", doc, {"/FontFile"});
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
        failures += run_test("font-FontFile3-CFF-and-FontFile2-TrueType", doc,
                             {"/FontFile3", "/FontFile2", "/Type1C"});
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
        // NB: content check confirms the fonts are emitted; it does NOT validate
        // xref offsets -- that needs a strict checker (qpdf --check), a future step.
        failures += run_test("multipage-embedded-fonts-xref", doc, {"/FontFile"});
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
        failures += run_test("font-encoding-differences-widths-tounicode", doc,
                             {"/Differences", "/ToUnicode", "WinAnsiEncoding"});
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
        failures += run_test("cidfont-identity-cidtogid-with-W", doc,
                             {"/CIDToGIDMap", "/W "});
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
        failures += run_test("cidfont-stream-cidtogid-multipage", doc,
                             {"/CIDToGIDMap", "/ToUnicode"});
    }

    // Test 13 (P3): page with a DCTDecode image XObject. Must stay a valid PDF
    // (image stream object accounted for in xref; /Resources has /XObject).
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::ImageXObject* im = p->add_images();
        im->set_width(2); im->set_height(2);
        im->set_bits_per_component(8);
        im->set_filter(pdf_proto::ImageXObject::DCT);
        im->set_color_space(pdf_proto::ImageXObject::DEVICE_RGB);
        im->set_data(std::string("\xFF\xD8\xFF\xE0", 4) + "junkjpeg");  // JPEG SOI-ish
        failures += run_test("image-dct-xobject", doc, {"/Subtype /Image", "/DCTDecode"});
    }

    // Test 14 (P3): font + image on the same page -> /Resources has both /Font and
    // /XObject; the driver stream runs both Tf and Do.
    {
        pdf_proto::PdfDocument doc;
        pdf_proto::Page* p = doc.add_pages();
        p->set_width(612); p->set_height(792);
        pdf_proto::Font* f = p->add_fonts();
        f->set_base_font("Helvetica");
        pdf_proto::ImageXObject* im = p->add_images();
        im->set_width(1); im->set_height(1);
        im->set_filter(pdf_proto::ImageXObject::RAW);
        im->set_color_space(pdf_proto::ImageXObject::DEVICE_GRAY);
        im->set_bits_per_component(8);
        im->set_data(std::string("\x80", 1));  // 1 gray sample
        // both resource kinds present: /XObject (image) and the font base name.
        failures += run_test("font-and-image-same-page", doc, {"/XObject", "Helvetica"});
    }

    // Test 15 (P3): multi-page with images + an ImageMask -> xref-order regression
    // across content/image/driver classes; also the stencil-mask path (no /ColorSpace).
    {
        pdf_proto::PdfDocument doc;
        for (int pg = 0; pg < 2; pg++) {
            pdf_proto::Page* p = doc.add_pages();
            p->set_width(612); p->set_height(792);
            pdf_proto::ContentStream* cs = p->add_content_streams();
            cs->set_raw_content("q Q");
            pdf_proto::ImageXObject* im = p->add_images();
            im->set_width(8); im->set_height(8);
            im->set_image_mask(true);   // stencil mask: 1bpc, no /ColorSpace
            im->set_bits_per_component(1);
            im->set_filter(pdf_proto::ImageXObject::RAW);
            im->set_data(std::string(8, '\xAA'));  // 8x8 1bpc = 8 bytes
        }
        failures += run_test("multipage-imagemask-xref", doc, {"/ImageMask"});
    }

    google::protobuf::ShutdownProtobufLibrary();
    return failures;
}
