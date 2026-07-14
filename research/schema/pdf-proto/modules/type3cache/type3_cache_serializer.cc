#include "modules/type3cache/type3_cache_serializer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kPageWidth = 612;
constexpr int kPageHeight = 792;

std::string StreamObject(const std::string& data) {
  std::ostringstream out;
  out << "<< /Length " << data.size() << " >>\nstream\n"
      << data << "\nendstream";
  return out.str();
}

uint32_t ClampU32(uint32_t value, uint32_t lo, uint32_t hi) {
  return std::max(lo, std::min(value, hi));
}

std::string GlyphName(int index) {
  if (index >= 0 && index < 26) {
    return std::string(1, static_cast<char>('A' + index));
  }
  return "G" + std::to_string(index);
}

std::string LiteralGlyphString(int index) {
  if (index >= 0 && index < 26) {
    return std::string(1, static_cast<char>('A' + index));
  }
  return "?";
}

std::string NestedFontSize(
    pdf_type3cache::Type3CacheDocument::MatrixMode mode) {
  switch (mode) {
    case pdf_type3cache::Type3CacheDocument::EXACT_REUSE:
      return "999.9999999999998";
    case pdf_type3cache::Type3CacheDocument::NEAR_REUSE:
      return "1000";
    case pdf_type3cache::Type3CacheDocument::DIFFERENT:
    default:
      return "500";
  }
}

std::string MetricOp(bool uses_d1, int glyph_size) {
  std::ostringstream out;
  if (uses_d1) {
    out << glyph_size << " 0 0 0 " << glyph_size << " " << glyph_size
        << " d1\n";
  } else {
    out << glyph_size << " 0 d0\n";
  }
  return out.str();
}

std::string GlyphBody(
    pdf_type3cache::Type3CacheDocument::GlyphProgramStyle style,
    int glyph_size) {
  std::ostringstream out;
  switch (style) {
    case pdf_type3cache::Type3CacheDocument::EMPTY_GLYPH:
      break;
    case pdf_type3cache::Type3CacheDocument::REPEATED_TEXT:
      out << "BT /F0 10 Tf (B) Tj (B) Tj ET\n";
      break;
    case pdf_type3cache::Type3CacheDocument::RECT_FILL:
    default:
      out << "0 0 " << glyph_size << " " << glyph_size << " re f\n";
      break;
  }
  return out.str();
}

std::string Type3FontDict(const std::string& name,
                          const std::vector<std::pair<int, std::string>>& chars,
                          int glyph_size,
                          const std::string& resources) {
  std::ostringstream charprocs;
  std::ostringstream differences;
  for (const auto& ch : chars) {
    charprocs << " /" << ch.second << " " << ch.first << " 0 R";
    differences << " /" << ch.second;
  }

  std::ostringstream widths;
  for (size_t i = 0; i < chars.size(); ++i) {
    widths << glyph_size << " ";
  }

  std::ostringstream out;
  out << "<< /Type /Font /Subtype /Type3 /Name /" << name
      << " /FontBBox [0 0 " << glyph_size << " " << glyph_size << "]"
      << " /FontMatrix [0.001 0 0 0.001 0 0]"
      << " /CharProcs <<" << charprocs.str() << " >>"
      << " /Encoding << /Type /Encoding /Differences [65"
      << differences.str() << "] >>"
      << " /FirstChar 65 /LastChar " << (65 + static_cast<int>(chars.size()) - 1)
      << " /Widths [" << widths.str() << "]"
      << " /Resources " << resources << " >>";
  return out.str();
}

void AppendXrefEntry(std::string* out, uint32_t offset) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%010u 00000 n \n", offset);
  *out += buf;
}

}  // namespace

std::string SerializeType3CachePdf(
    const pdf_type3cache::Type3CacheDocument& doc) {
  const int filler_fonts = static_cast<int>(ClampU32(doc.filler_fonts(), 0, 64));
  const int nested_depth = static_cast<int>(ClampU32(doc.nested_depth(), 0, 8));
  const int glyph_size = static_cast<int>(ClampU32(doc.glyph_size(), 1, 100000));
  const int page_font_size =
      static_cast<int>(ClampU32(doc.page_font_size(), 1, 1000));

  const int catalog_obj = 1;
  const int pages_obj = 2;
  const int page_obj = 3;
  const int main_font_obj = 4;

  const int main_glyph_count = nested_depth + 1;
  std::vector<int> main_char_objs;
  int next_obj = 5;
  for (int i = 0; i < main_glyph_count; ++i) {
    main_char_objs.push_back(next_obj++);
  }

  struct FillerSpec {
    int index;
    int font_obj;
    int char_obj;
    std::string font_name;
  };
  std::vector<FillerSpec> fillers;
  for (int i = 1; i <= filler_fonts; ++i) {
    fillers.push_back({i, next_obj, next_obj + 1, "F" + std::to_string(i)});
    next_obj += 2;
  }
  const int contents_obj = next_obj++;

  std::map<int, std::string> objects;

  objects[catalog_obj] = "<< /Type /Catalog /Pages 2 0 R >>";
  objects[pages_obj] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
  {
    std::ostringstream page;
    page << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
         << kPageWidth << " " << kPageHeight << "]"
         << " /Resources << /Font << /F0 4 0 R >> >>"
         << " /Contents " << contents_obj << " 0 R >>";
    objects[page_obj] = page.str();
  }

  std::ostringstream font_resources;
  font_resources << "<< /Font << /F0 4 0 R";
  for (const auto& filler : fillers) {
    font_resources << " /" << filler.font_name << " " << filler.font_obj
                   << " 0 R";
  }
  font_resources << " >> >>";

  std::vector<std::pair<int, std::string>> main_chars;
  for (int i = 0; i < main_glyph_count; ++i) {
    main_chars.push_back({main_char_objs[i], GlyphName(i)});
  }
  objects[main_font_obj] =
      Type3FontDict("F0", main_chars, glyph_size, font_resources.str());

  std::ostringstream filler_draws;
  for (const auto& filler : fillers) {
    filler_draws << "/" << filler.font_name << " 1000 Tf (A) Tj\n";
  }

  for (int i = 0; i < main_glyph_count; ++i) {
    std::ostringstream body;
    const bool uses_d1 = (i == 0) ? doc.outer_uses_d1() : doc.inner_uses_d1();
    body << MetricOp(uses_d1, glyph_size);

    if (i == 0 && !doc.render_fillers_after_nested()) {
      body << filler_draws.str();
    }

    if (i + 1 < main_glyph_count) {
      body << "/F0 " << NestedFontSize(doc.matrix_mode()) << " Tf ("
           << LiteralGlyphString(i + 1) << ") Tj\n";
    } else {
      body << GlyphBody(doc.glyph_program_style(), glyph_size);
    }

    if (i == 0 && doc.render_fillers_after_nested()) {
      body << filler_draws.str();
    }

    objects[main_char_objs[i]] = StreamObject(body.str());
  }

  for (const auto& filler : fillers) {
    objects[filler.font_obj] =
        Type3FontDict(filler.font_name, {{filler.char_obj, "A"}}, glyph_size,
                      "<< >>");

    std::ostringstream body;
    body << MetricOp(doc.fillers_use_d1(), glyph_size)
         << GlyphBody(doc.glyph_program_style(), glyph_size);
    objects[filler.char_obj] = StreamObject(body.str());
  }

  {
    std::ostringstream body;
    body << "BT /F0 " << page_font_size << " Tf 72 600 Td (A) Tj ET\n";
    objects[contents_obj] = StreamObject(body.str());
  }

  std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
  std::vector<uint32_t> offsets(next_obj, 0);
  for (int obj_num = 1; obj_num < next_obj; ++obj_num) {
    offsets[obj_num] = static_cast<uint32_t>(out.size());
    out += std::to_string(obj_num) + " 0 obj\n";
    out += objects[obj_num];
    out += "\nendobj\n";
  }

  const uint32_t xref_offset = static_cast<uint32_t>(out.size());
  out += "xref\n0 " + std::to_string(offsets.size()) + "\n";
  out += "0000000000 65535 f \n";
  for (size_t obj_num = 1; obj_num < offsets.size(); ++obj_num) {
    AppendXrefEntry(&out, offsets[obj_num]);
  }
  out += "trailer\n<< /Size " + std::to_string(offsets.size()) +
         " /Root " + std::to_string(catalog_obj) + " 0 R >>\n";
  out += "startxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";

  return out;
}
