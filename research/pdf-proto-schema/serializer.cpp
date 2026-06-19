#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <zlib.h>
#include "pdf.pb.h"

struct XrefEntry {
  uint32_t offset;
  uint32_t generation;
  bool in_use;
};

// Map proto Font.Subtype -> PDF /Subtype name. xpdf's getFontType keys on this.
static const char* subtype_name(pdf_proto::Font::Subtype s) {
  switch (s) {
    case pdf_proto::Font::TRUETYPE: return "TrueType";
    case pdf_proto::Font::TYPE3:    return "Type3";
    case pdf_proto::Font::TYPE0:    return "Type0";
    case pdf_proto::Font::TYPE1:
    default:                        return "Type1";
  }
}

// Map proto EmbeddedFontFile.Key -> the /FontFile* dict key. getFontType keys on
// which of these is present (precedence FontFile3 > FontFile2 > FontFile) and
// dispatches to FoFiType1C / FoFiTrueType / FoFiType1 respectively.
static const char* fontfile_key_name(pdf_proto::EmbeddedFontFile::Key k) {
  switch (k) {
    case pdf_proto::EmbeddedFontFile::FONTFILE2: return "FontFile2";
    case pdf_proto::EmbeddedFontFile::FONTFILE3: return "FontFile3";
    case pdf_proto::EmbeddedFontFile::FONTFILE:
    default:                                     return "FontFile";
  }
}

// Escape arbitrary bytes into a single valid PDF name token (spec 7.3.5):
// delimiters, whitespace, '#' and non-printables become #XX. xpdf un-escapes
// transparently in getName(), so the raw fuzzed bytes still reach the name string.
static std::string pdf_name_escape(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    bool special = (c < 0x21 || c > 0x7e ||
                    c=='(' || c==')' || c=='<' || c=='>' ||
                    c=='[' || c==']' || c=='{' || c=='}' ||
                    c=='/' || c=='%' || c=='#');
    if (special) {
      char buf[4];
      snprintf(buf, sizeof(buf), "#%02X", c);
      out += buf;
    } else {
      out += (char)c;
    }
  }
  return out;
}

// Compress src with zlib (deflate). Returns empty string on failure.
static std::string zlib_compress(const std::string& src) {
  uLongf bound = compressBound((uLong)src.size());
  std::string dst(bound, '\0');
  int rc = compress2(
      reinterpret_cast<Bytef*>(&dst[0]), &bound,
      reinterpret_cast<const Bytef*>(src.data()), (uLong)src.size(),
      Z_DEFAULT_COMPRESSION);
  if (rc != Z_OK) return {};
  dst.resize(bound);
  return dst;
}

std::string SerializePdf(const pdf_proto::PdfDocument& doc) {
  std::ostringstream out;
  std::vector<XrefEntry> xref;

  // object 0 is always the free-list head
  xref.push_back({0, 65535, false});

  auto record_offset = [&]() -> uint32_t {
    uint32_t pos = (uint32_t)out.tellp();
    xref.push_back({pos, 0, true});
    return (uint32_t)xref.size() - 1;
  };

  out << "%PDF-1.4\n";

  // Pre-assign content-stream object numbers.
  // Layout: obj1=Catalog, obj2=Pages, obj3..2+N=Page objects, then all content streams.
  // cs_objs[i] is the list of object numbers for page i's streams (may be empty).
  // After content streams: per-page font objects, then a per-page "driver"
  // content stream (only for pages with >=1 font). The driver runs `/Fk 12 Tf`
  // for each font so xpdf's lazy GfxFont::makeFont fires on every declared font.
  // Object numbers are pre-assigned in the SAME order they are later written, so
  // each xref entry (pushed by record_offset() in write order) lines up with its
  // object number. Each object class gets its own loop here AND its own write loop
  // below, in matching order: content streams, font objects, font-program streams,
  // driver streams. (Interleaving assignment per-page while writing per-class would
  // scramble the xref for multi-page docs.)
  int page_count = doc.pages_size();
  std::vector<std::vector<int>> cs_objs(page_count);
  std::vector<std::vector<int>> font_objs(page_count);
  // fontfile_objs[i][k] = object number of font k's embedded program stream, or -1.
  std::vector<std::vector<int>> fontfile_objs(page_count);
  std::vector<int> driver_objs(page_count, -1);  // -1 = no driver (no fonts)
  {
    int next = 3 + page_count;
    for (int i = 0; i < page_count; i++)
      for (int j = 0; j < doc.pages(i).content_streams_size(); j++)
        cs_objs[i].push_back(next++);
    for (int i = 0; i < page_count; i++)
      for (int k = 0; k < doc.pages(i).fonts_size(); k++)
        font_objs[i].push_back(next++);
    for (int i = 0; i < page_count; i++) {
      fontfile_objs[i].assign(doc.pages(i).fonts_size(), -1);
      for (int k = 0; k < doc.pages(i).fonts_size(); k++)
        if (doc.pages(i).fonts(k).has_font_descriptor() &&
            doc.pages(i).fonts(k).font_descriptor().has_font_file())
          fontfile_objs[i][k] = next++;
    }
    for (int i = 0; i < page_count; i++)
      if (doc.pages(i).fonts_size() > 0)
        driver_objs[i] = next++;
  }

  // Object 1: Catalog
  record_offset();
  out << "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

  // Object 2: Pages (page tree root)
  record_offset();
  out << "2 0 obj\n<< /Type /Pages /Count " << page_count << " /Kids [";
  for (int i = 0; i < page_count; i++)
    out << (3 + i) << " 0 R ";
  out << "] >>\nendobj\n";

  // Objects 3..2+N: Page objects
  for (int i = 0; i < page_count; i++) {
    record_offset();
    const pdf_proto::Page& p = doc.pages(i);
    float w = p.has_width()  ? p.width()  : 612.0f;
    float h = p.has_height() ? p.height() : 792.0f;
    out << (3 + i) << " 0 obj\n"
        << "<< /Type /Page /Parent 2 0 R"
        << " /MediaBox [0 0 " << w << " " << h << "]";
    // Font resources: /F0 /F1 ... map to the pre-assigned font objects.
    if (!font_objs[i].empty()) {
      out << " /Resources << /Font <<";
      for (size_t k = 0; k < font_objs[i].size(); k++)
        out << " /F" << k << " " << font_objs[i][k] << " 0 R";
      out << " >> >>";
    }
    // /Contents = driver stream (if any, first so Tf precedes fuzzer ops) + fuzzer streams.
    std::vector<int> contents;
    if (driver_objs[i] >= 0) contents.push_back(driver_objs[i]);
    for (int n : cs_objs[i]) contents.push_back(n);
    if (contents.size() == 1) {
      out << " /Contents " << contents[0] << " 0 R";
    } else if (contents.size() > 1) {
      out << " /Contents [";
      for (int n : contents) out << n << " 0 R ";
      out << "]";
    }
    out << " >>\nendobj\n";
  }

  // Content-stream objects
  for (int i = 0; i < page_count; i++) {
    for (int j = 0; j < (int)cs_objs[i].size(); j++) {
    record_offset();
    const pdf_proto::ContentStream& cs = doc.pages(i).content_streams(j);
    const std::string& raw = cs.raw_content();

    bool want_flate = (cs.filter() == pdf_proto::ContentStream::FLATE);
    bool do_compress = want_flate && !cs.skip_compression();

    std::string compressed;
    if (do_compress) {
      compressed = zlib_compress(raw);
      if (compressed.empty())
        do_compress = false;  // fall back to raw on compress failure
    }

    const std::string& payload = do_compress ? compressed : raw;

    // PDF spec 7.3.8.1: /Length is the byte count between the line ending
    // after "stream" and the keyword "endstream" (not counting the final \n).
    // length_delta shifts the written value to produce over-read (positive) or
    // under-read (negative) -- exercises stream boundary error handling in parsers.
    long written_len = (long)payload.size() + cs.length_delta();
    out << cs_objs[i][j] << " 0 obj\n<< /Length " << written_len;
    // Declare /Filter /FlateDecode even when skip_compression=true so xpdf feeds
    // the raw (non-zlib) bytes into its FlateStream decoder -- hitting corrupt-data paths.
    if (want_flate)
      out << " /Filter /FlateDecode";
    out << " >>\nstream\n"
        << payload
        << "\nendstream\nendobj\n";
    } // end inner stream loop
  }

  // Font objects. Dummy dict -- enough to reach makeFont/getFontType/Gfx8BitFont.
  // omit_type / omit_subtype drop required keys to hit malformed-dict handling.
  for (int i = 0; i < page_count; i++) {
    for (int k = 0; k < (int)font_objs[i].size(); k++) {
      record_offset();
      const pdf_proto::Font& f = doc.pages(i).fonts(k);
      out << font_objs[i][k] << " 0 obj\n<<";
      if (!f.omit_type())    out << " /Type /Font";
      if (!f.omit_subtype()) out << " /Subtype /" << subtype_name(f.subtype());
      out << " /BaseFont /" << pdf_name_escape(f.base_font());
      // P1.5: inline /FontDescriptor. The embedded program lives in a separate
      // indirect stream object (pre-assigned in fontfile_objs) and is referenced
      // here via /FontFile{,2,3} -- xpdf requires that key to be an indirect ref.
      if (f.has_font_descriptor()) {
        const pdf_proto::FontDescriptor& fd = f.font_descriptor();
        out << " /FontDescriptor << /Type /FontDescriptor"
            << " /FontName /" << pdf_name_escape(f.base_font())
            << " /Flags " << fd.flags()
            << " /ItalicAngle " << fd.italic_angle()
            << " /Ascent " << fd.ascent()
            << " /Descent " << fd.descent()
            << " /CapHeight " << fd.cap_height()
            << " /StemV " << fd.stem_v()
            << " /MissingWidth " << fd.missing_width();
        if (fd.font_bbox_size() == 4) {
          out << " /FontBBox [";
          for (int b = 0; b < 4; b++) out << fd.font_bbox(b) << (b < 3 ? " " : "");
          out << "]";
        }
        if (fd.has_font_file() && fontfile_objs[i][k] >= 0)
          out << " /" << fontfile_key_name(fd.font_file().key())
              << " " << fontfile_objs[i][k] << " 0 R";
        out << " >>";
      }
      out << " >>\nendobj\n";
    }
  }

  // P1.5: embedded font-program streams. Raw `program` bytes go in verbatim;
  // FoFiIdentifier sniffs them and dispatches to FoFiType1 / FoFiTrueType /
  // FoFiType1C. /Subtype is written only for FontFile3 (Type1C/CIDFontType0C/
  // OpenType). length_delta shifts /Length for stream-boundary fuzzing.
  for (int i = 0; i < page_count; i++) {
    for (int k = 0; k < (int)fontfile_objs[i].size(); k++) {
      if (fontfile_objs[i][k] < 0) continue;
      record_offset();
      const pdf_proto::EmbeddedFontFile& ff = doc.pages(i).fonts(k).font_descriptor().font_file();
      const std::string& prog = ff.program();
      long written_len = (long)prog.size() + ff.length_delta();
      out << fontfile_objs[i][k] << " 0 obj\n<< /Length " << written_len;
      if (ff.key() == pdf_proto::EmbeddedFontFile::FONTFILE3 && !ff.subtype().empty())
        out << " /Subtype /" << pdf_name_escape(ff.subtype());
      out << " >>\nstream\n" << prog << "\nendstream\nendobj\n";
    }
  }

  // Driver content streams: force lazy font loading via Tf for every page font.
  for (int i = 0; i < page_count; i++) {
    if (driver_objs[i] < 0) continue;
    std::ostringstream payload;
    payload << "BT\n";
    for (int k = 0; k < (int)font_objs[i].size(); k++)
      payload << "/F" << k << " 12 Tf (Ag) Tj\n";
    payload << "ET\n";
    std::string body = payload.str();
    record_offset();
    out << driver_objs[i] << " 0 obj\n<< /Length " << body.size()
        << " >>\nstream\n" << body << "\nendstream\nendobj\n";
  }

  // Xref table
  uint32_t xref_offset = (uint32_t)out.tellp();
  out << "xref\n0 " << xref.size() << "\n";
  for (auto& e : xref) {
    char buf[21];
    if (e.in_use)
      snprintf(buf, sizeof(buf), "%010u %05u n \n", e.offset, e.generation);
    else
      snprintf(buf, sizeof(buf), "%010u %05u f \n", e.offset, e.generation);
    out << buf;
  }

  // Trailer
  out << "trailer\n"
      << "<< /Size " << xref.size() << " /Root 1 0 R >>\n"
      << "startxref\n"
      << xref_offset << "\n"
      << "%%EOF\n";

  return out.str();
}
