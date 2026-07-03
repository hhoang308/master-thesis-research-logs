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

// Map proto ImageXObject.Filter -> PDF /Filter name, or nullptr for RAW (no filter).
static const char* image_filter_name(pdf_proto::ImageXObject::Filter f) {
  switch (f) {
    case pdf_proto::ImageXObject::FLATE:     return "FlateDecode";
    case pdf_proto::ImageXObject::DCT:       return "DCTDecode";
    case pdf_proto::ImageXObject::JPX:       return "JPXDecode";
    case pdf_proto::ImageXObject::CCITT:     return "CCITTFaxDecode";
    case pdf_proto::ImageXObject::LZW:       return "LZWDecode";
    case pdf_proto::ImageXObject::RUNLENGTH: return "RunLengthDecode";
    case pdf_proto::ImageXObject::RAW:
    default:                                 return nullptr;
  }
}

// Map proto ImageXObject.ColorSpace -> PDF device colorspace name.
static const char* image_colorspace_name(pdf_proto::ImageXObject::ColorSpace c) {
  switch (c) {
    case pdf_proto::ImageXObject::DEVICE_GRAY: return "DeviceGray";
    case pdf_proto::ImageXObject::DEVICE_CMYK: return "DeviceCMYK";
    case pdf_proto::ImageXObject::DEVICE_RGB:
    default:                                   return "DeviceRGB";
  }
}

// Map proto Font.BaseEncoding -> PDF encoding name, or nullptr for BASE_NONE.
static const char* base_encoding_name(pdf_proto::Font::BaseEncoding e) {
  switch (e) {
    case pdf_proto::Font::STANDARD:  return "StandardEncoding";
    case pdf_proto::Font::WINANSI:   return "WinAnsiEncoding";
    case pdf_proto::Font::MACROMAN:  return "MacRomanEncoding";
    case pdf_proto::Font::MACEXPERT: return "MacExpertEncoding";
    case pdf_proto::Font::BASE_NONE:
    default:                         return nullptr;
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

// Escape bytes into a PDF literal string body (spec 7.3.4.2): '(' ')' '\' get a
// backslash. Caller wraps the result in parentheses.
static std::string pdf_string_escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '(' || c == ')' || c == '\\') out += '\\';
    out += c;
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
  // P3: image_objs[i][k] = object number of image k's XObject stream.
  std::vector<std::vector<int>> image_objs(page_count);
  std::vector<std::vector<int>> font_objs(page_count);
  // fontfile_objs[i][k] = object number of font k's embedded program stream, or -1.
  std::vector<std::vector<int>> fontfile_objs(page_count);
  // tounicode_objs[i][k] = object number of font k's /ToUnicode CMap stream, or -1.
  std::vector<std::vector<int>> tounicode_objs(page_count);
  // Step B: descendant_objs[i][k] = descendant CIDFont object (TYPE0 fonts), or -1.
  std::vector<std::vector<int>> descendant_objs(page_count);
  // Step B: cidgid_objs[i][k] = /CIDToGIDMap stream object (stream form), or -1.
  std::vector<std::vector<int>> cidgid_objs(page_count);
  std::vector<int> driver_objs(page_count, -1);  // -1 = no driver (no fonts)
  // A font is a composite (Type0) font iff subtype==TYPE0 AND it carries a CidFont.
  auto is_cid = [&](int i, int k) {
    const pdf_proto::Font& f = doc.pages(i).fonts(k);
    return f.subtype() == pdf_proto::Font::TYPE0 && f.has_cid();
  };
  {
    int next = 3 + page_count;
    // Canonical object-class order (assignment == write order, to keep xref consistent):
    // content, images, fonts, descendants, cidgid, fontfile, tounicode, drivers.
    for (int i = 0; i < page_count; i++)
      for (int j = 0; j < doc.pages(i).content_streams_size(); j++)
        cs_objs[i].push_back(next++);
    for (int i = 0; i < page_count; i++)
      for (int k = 0; k < doc.pages(i).images_size(); k++)
        image_objs[i].push_back(next++);
    for (int i = 0; i < page_count; i++)
      for (int k = 0; k < doc.pages(i).fonts_size(); k++)
        font_objs[i].push_back(next++);
    for (int i = 0; i < page_count; i++) {
      descendant_objs[i].assign(doc.pages(i).fonts_size(), -1);
      for (int k = 0; k < doc.pages(i).fonts_size(); k++)
        if (is_cid(i, k))
          descendant_objs[i][k] = next++;
    }
    for (int i = 0; i < page_count; i++) {
      cidgid_objs[i].assign(doc.pages(i).fonts_size(), -1);
      for (int k = 0; k < doc.pages(i).fonts_size(); k++)
        if (is_cid(i, k) && doc.pages(i).fonts(k).cid().has_cid_to_gid_map_stream())
          cidgid_objs[i][k] = next++;
    }
    for (int i = 0; i < page_count; i++) {
      fontfile_objs[i].assign(doc.pages(i).fonts_size(), -1);
      for (int k = 0; k < doc.pages(i).fonts_size(); k++)
        if (doc.pages(i).fonts(k).has_font_descriptor() &&
            doc.pages(i).fonts(k).font_descriptor().has_font_file())
          fontfile_objs[i][k] = next++;
    }
    for (int i = 0; i < page_count; i++) {
      tounicode_objs[i].assign(doc.pages(i).fonts_size(), -1);
      for (int k = 0; k < doc.pages(i).fonts_size(); k++)
        if (doc.pages(i).fonts(k).has_to_unicode())
          tounicode_objs[i][k] = next++;
    }
    // Driver content stream needed whenever a page has fonts (Tf) OR images (Do).
    for (int i = 0; i < page_count; i++)
      if (doc.pages(i).fonts_size() > 0 || doc.pages(i).images_size() > 0)
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
    // Resources: /Font (/F0 /F1 ...) and/or /XObject (/Im0 /Im1 ...) sub-dicts.
    if (!font_objs[i].empty() || !image_objs[i].empty()) {
      out << " /Resources <<";
      if (!font_objs[i].empty()) {
        out << " /Font <<";
        for (size_t k = 0; k < font_objs[i].size(); k++)
          out << " /F" << k << " " << font_objs[i][k] << " 0 R";
        out << " >>";
      }
      if (!image_objs[i].empty()) {
        out << " /XObject <<";
        for (size_t k = 0; k < image_objs[i].size(); k++)
          out << " /Im" << k << " " << image_objs[i][k] << " 0 R";
        out << " >>";
      }
      out << " >>";
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

  // P3: image XObject stream objects. `data` is written verbatim (NOT compressed) so the
  // chosen /Filter decoder (DCT/JPX/CCITT/LZW/Flate) receives the raw fuzzer bytes. The
  // harness's FuzzOutputDev drains the stream via `Do`, forcing the decoder to run.
  for (int i = 0; i < page_count; i++) {
    for (int k = 0; k < (int)image_objs[i].size(); k++) {
      record_offset();
      const pdf_proto::ImageXObject& img = doc.pages(i).images(k);
      const std::string& data = img.data();
      long written_len = (long)data.size() + img.length_delta();
      out << image_objs[i][k] << " 0 obj\n"
          << "<< /Type /XObject /Subtype /Image"
          << " /Width " << img.width()
          << " /Height " << img.height()
          << " /BitsPerComponent " << img.bits_per_component();
      if (img.image_mask())
        out << " /ImageMask true";  // stencil mask: no /ColorSpace
      else
        out << " /ColorSpace /" << image_colorspace_name(img.color_space());
      const char* ifn = image_filter_name(img.filter());
      if (ifn)
        out << " /Filter /" << ifn;
      out << " /Length " << written_len << " >>\nstream\n"
          << data << "\nendstream\nendobj\n";
    }
  }

  // Helper: emit an inline "/FontDescriptor << ... >>". fontfile_obj >= 0 adds the
  // /FontFile{,2,3} indirect reference (P1.5); -1 emits metrics only. Used by both the
  // 8-bit font (root dict) and the CID descendant font.
  auto emit_descriptor = [&out](const std::string& fontname,
                                const pdf_proto::FontDescriptor& fd, int fontfile_obj) {
    out << " /FontDescriptor << /Type /FontDescriptor"
        << " /FontName /" << pdf_name_escape(fontname)
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
    if (fontfile_obj >= 0)
      out << " /" << fontfile_key_name(fd.font_file().key()) << " " << fontfile_obj << " 0 R";
    out << " >>";
  };

  // Font objects (the root dict; for Type0 this is the composite-font wrapper).
  // omit_type / omit_subtype drop required keys to hit malformed-dict handling.
  for (int i = 0; i < page_count; i++) {
    for (int k = 0; k < (int)font_objs[i].size(); k++) {
      record_offset();
      const pdf_proto::Font& f = doc.pages(i).fonts(k);
      out << font_objs[i][k] << " 0 obj\n<<";
      if (!f.omit_type()) out << " /Type /Font";

      if (is_cid(i, k)) {
        // Step B: Type0 root -- /Encoding (CMap name) + /DescendantFonts [D 0 R].
        // The CIDSystemInfo, CIDToGIDMap, W, DW, and FontDescriptor live on the
        // descendant CIDFont object written below.
        const pdf_proto::Font::CidFont& cid = f.cid();
        if (!f.omit_subtype()) out << " /Subtype /Type0";
        out << " /BaseFont /" << pdf_name_escape(f.base_font());
        out << " /Encoding /" << pdf_name_escape(cid.encoding());
        out << " /DescendantFonts [" << descendant_objs[i][k] << " 0 R]";
        if (f.has_to_unicode() && tounicode_objs[i][k] >= 0)
          out << " /ToUnicode " << tounicode_objs[i][k] << " 0 R";
      } else {
        // 8-bit font (Type1/TrueType/Type3).
        if (!f.omit_subtype()) out << " /Subtype /" << subtype_name(f.subtype());
        out << " /BaseFont /" << pdf_name_escape(f.base_font());
        // P1.5: inline /FontDescriptor (+ embedded program ref).
        if (f.has_font_descriptor())
          emit_descriptor(f.base_font(), f.font_descriptor(),
                          (f.font_descriptor().has_font_file() && fontfile_objs[i][k] >= 0)
                              ? fontfile_objs[i][k] : -1);
        // Step A: /Encoding (name, or dict with /BaseEncoding + /Differences).
        const char* be = base_encoding_name(f.base_encoding());
        if (f.differences_size() > 0) {
          out << " /Encoding <<";
          if (be) out << " /BaseEncoding /" << be;
          out << " /Differences [";
          for (const auto& d : f.differences())
            out << d.code() << " /" << pdf_name_escape(d.name()) << " ";
          out << "] >>";
        } else if (be) {
          out << " /Encoding /" << be;
        }
        // Step A: /FirstChar /LastChar /Widths.
        if (f.widths_size() > 0) {
          int fc = (int)f.first_char();
          out << " /FirstChar " << fc
              << " /LastChar " << (fc + f.widths_size() - 1)
              << " /Widths [";
          for (int w : f.widths()) out << w << " ";
          out << "]";
        }
        // Step A: /ToUnicode CMap stream.
        if (f.has_to_unicode() && tounicode_objs[i][k] >= 0)
          out << " /ToUnicode " << tounicode_objs[i][k] << " 0 R";
      }
      out << " >>\nendobj\n";
    }
  }

  // Step B: descendant CIDFont objects (for Type0 fonts). Carries CIDSystemInfo,
  // FontDescriptor (metrics), CIDToGIDMap (/Identity or a 2-byte-GID stream ref),
  // /W (width array -> GfxCIDFont's width parser) and /DW.
  for (int i = 0; i < page_count; i++) {
    for (int k = 0; k < (int)descendant_objs[i].size(); k++) {
      if (descendant_objs[i][k] < 0) continue;
      record_offset();
      const pdf_proto::Font& f = doc.pages(i).fonts(k);
      const pdf_proto::Font::CidFont& cid = f.cid();
      const char* cidsub = (cid.cid_subtype() == pdf_proto::Font::CidFont::CIDFONTTYPE0)
                               ? "CIDFontType0" : "CIDFontType2";
      out << descendant_objs[i][k] << " 0 obj\n"
          << "<< /Type /Font /Subtype /" << cidsub
          << " /BaseFont /" << pdf_name_escape(f.base_font())
          << " /CIDSystemInfo << /Registry (" << pdf_string_escape(cid.registry())
          << ") /Ordering (" << pdf_string_escape(cid.ordering())
          << ") /Supplement " << cid.supplement() << " >>";
      if (cid.has_descendant_descriptor())
        emit_descriptor(f.base_font(), cid.descendant_descriptor(), -1);
      if (cidgid_objs[i][k] >= 0)
        out << " /CIDToGIDMap " << cidgid_objs[i][k] << " 0 R";
      else
        out << " /CIDToGIDMap /Identity";
      if (cid.w_size() > 0) {
        out << " /W [";
        for (int w : cid.w()) out << w << " ";
        out << "]";
      }
      out << " /DW " << cid.dw();
      out << " >>\nendobj\n";
    }
  }

  // Step B: /CIDToGIDMap streams (stream form). Raw bytes are read as big-endian
  // 2-byte GIDs by GfxCIDFont (GfxFont.cc:97) -- arbitrary bytes exercise that reader.
  for (int i = 0; i < page_count; i++) {
    for (int k = 0; k < (int)cidgid_objs[i].size(); k++) {
      if (cidgid_objs[i][k] < 0) continue;
      record_offset();
      const std::string& m = doc.pages(i).fonts(k).cid().cid_to_gid_map_stream();
      out << cidgid_objs[i][k] << " 0 obj\n<< /Length " << m.size()
          << " >>\nstream\n" << m << "\nendstream\nendobj\n";
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

  // Step A: /ToUnicode CMap streams. Raw `to_unicode` bytes are the CMap program;
  // xpdf's readToUnicodeCMap parses them (CMap tokenizer -> another fuzz surface).
  for (int i = 0; i < page_count; i++) {
    for (int k = 0; k < (int)tounicode_objs[i].size(); k++) {
      if (tounicode_objs[i][k] < 0) continue;
      record_offset();
      const std::string& cmap = doc.pages(i).fonts(k).to_unicode();
      out << tounicode_objs[i][k] << " 0 obj\n<< /Length " << cmap.size()
          << " >>\nstream\n" << cmap << "\nendstream\nendobj\n";
    }
  }

  // Driver content streams: force lazy resource loading -- Tf for every font
  // (-> GfxFont::makeFont) and `Do` for every image (-> Gfx::doImage -> decoder).
  for (int i = 0; i < page_count; i++) {
    if (driver_objs[i] < 0) continue;
    std::ostringstream payload;
    payload << "BT\n";
    for (int k = 0; k < (int)font_objs[i].size(); k++)
      payload << "/F" << k << " 12 Tf (Ag) Tj\n";
    payload << "ET\n";
    for (int k = 0; k < (int)image_objs[i].size(); k++)
      payload << "q /Im" << k << " Do Q\n";
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
