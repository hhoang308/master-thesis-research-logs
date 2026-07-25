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

// LiteralGlyphString emits the text byte used by Tj, not the CharProc name.
// Type3FontDict installs Differences starting at code 65, so glyph index N is
// selected by byte 65 + N and must stay in lockstep with GlyphName(N).
// This helper currently only handles A..Z because nested_depth is clamped low
// enough that generated main glyph indexes stay below 26. If that clamp is ever
// raised past 25, this fallback must be replaced with an encoding-safe byte
// emitter; "?" is code 63 and will not select /G26, /G27, etc. under the
// current FirstChar/Differences layout.
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

std::string MalformedPrelude(
    pdf_type3cache::Type3CacheDocument::MalformedCharProc malformed) {
  switch (malformed) {
    case pdf_type3cache::Type3CacheDocument::BAD_D1_ARITY:
      return "1000 d1\n";
    case pdf_type3cache::Type3CacheDocument::UNKNOWN_OPERATOR:
      return "1 2 Type3BogusOp\n";
    case pdf_type3cache::Type3CacheDocument::UNBALANCED_Q:
      return "q\n";
    case pdf_type3cache::Type3CacheDocument::MISSING_FONT_TEXT:
      return "BT /FMissing 12 Tf (X) Tj ET\n";
    case pdf_type3cache::Type3CacheDocument::WELL_FORMED:
    default:
      return "";
  }
}

bool UsesCharProcAlias(
    pdf_type3cache::Type3CacheDocument::ResourceAliasMode mode) {
  return mode == pdf_type3cache::Type3CacheDocument::CHARPROC_ALIAS ||
         mode == pdf_type3cache::Type3CacheDocument::PAGE_AND_CHARPROC;
}

bool UsesPageAlias(
    pdf_type3cache::Type3CacheDocument::ResourceAliasMode mode) {
  return mode == pdf_type3cache::Type3CacheDocument::PAGE_ALIAS ||
         mode == pdf_type3cache::Type3CacheDocument::PAGE_AND_CHARPROC;
}

std::string GlyphDraw(const std::string& font_resource, int index,
                      pdf_type3cache::Type3CacheDocument::MatrixMode mode) {
  std::ostringstream out;
  out << "/" << font_resource << " " << NestedFontSize(mode) << " Tf ("
      << LiteralGlyphString(index) << ") Tj\n";
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
  const int in_progress_cache_hits =
      static_cast<int>(ClampU32(doc.in_progress_cache_hits(), 0, 8));
  const auto recursion_pattern = doc.recursion_pattern();
  const auto nested_call_timing = doc.nested_call_timing();
  const auto malformed_charproc = doc.malformed_charproc();
  const auto resource_alias_mode = doc.resource_alias_mode();
  const std::string nested_main_font =
      UsesCharProcAlias(resource_alias_mode) ? "FAlias" : "F0";
  const std::string page_main_font =
      UsesPageAlias(resource_alias_mode) ? "FPage" : "F0";

  const int catalog_obj = 1;
  const int pages_obj = 2;
  const int page_obj = 3;
  const int main_font_obj = 4;

  int main_glyph_count = nested_depth + 1;
  switch (recursion_pattern) {
    case pdf_type3cache::Type3CacheDocument::SELF:
      main_glyph_count = 1;
      break;
    case pdf_type3cache::Type3CacheDocument::MUTUAL:
      main_glyph_count = std::max(2, main_glyph_count);
      break;
    case pdf_type3cache::Type3CacheDocument::FANOUT:
      main_glyph_count = std::max(2, main_glyph_count);
      break;
    case pdf_type3cache::Type3CacheDocument::CHAIN:
    default:
      break;
  }
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
         << " /Resources << /Font << /F0 4 0 R";
    if (UsesPageAlias(resource_alias_mode)) {
      page << " /FPage 4 0 R";
    }
    page << " >> >>"
         << " /Contents " << contents_obj << " 0 R >>";
    objects[page_obj] = page.str();
  }

  std::ostringstream font_resources;
  font_resources << "<< /Font << /F0 4 0 R";
  if (UsesCharProcAlias(resource_alias_mode)) {
    font_resources << " /FAlias 4 0 R";
  }
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
    body << MalformedPrelude(malformed_charproc);

    std::ostringstream recursive_draws;
    switch (recursion_pattern) {
      case pdf_type3cache::Type3CacheDocument::SELF:
        if (i == 0 && nested_depth >= 1) {
          recursive_draws << GlyphDraw(nested_main_font, 0, doc.matrix_mode());
        }
        break;
      case pdf_type3cache::Type3CacheDocument::MUTUAL:
        if (i == 0) {
          recursive_draws << GlyphDraw(nested_main_font, 1, doc.matrix_mode());
        } else if (i == 1) {
          // MUTUAL already forces at least two main glyphs, so glyph B should
          // always close the cycle back to glyph A. Tying this edge to
          // nested_depth would make depth 0 degrade into A -> B and stop.
          recursive_draws << GlyphDraw(nested_main_font, 0, doc.matrix_mode());
        } else if (i + 1 < main_glyph_count) {
          recursive_draws << GlyphDraw(nested_main_font, i + 1,
                                       doc.matrix_mode());
        }
        break;
      case pdf_type3cache::Type3CacheDocument::FANOUT:
        if (i == 0) {
          for (int target = 1; target < main_glyph_count; ++target) {
            recursive_draws << GlyphDraw(nested_main_font, target,
                                         doc.matrix_mode());
          }
        }
        break;
      case pdf_type3cache::Type3CacheDocument::CHAIN:
      default:
        if (i + 1 < main_glyph_count) {
          recursive_draws << GlyphDraw(nested_main_font, i + 1,
                                       doc.matrix_mode());
        }
        break;
    }
    const std::string nested_text = recursive_draws.str();
    const bool emit_before_metric =
        nested_call_timing == pdf_type3cache::Type3CacheDocument::BEFORE_METRIC ||
        nested_call_timing == pdf_type3cache::Type3CacheDocument::BOTH_TIMINGS;
    const bool emit_after_metric =
        nested_call_timing == pdf_type3cache::Type3CacheDocument::AFTER_METRIC ||
        nested_call_timing == pdf_type3cache::Type3CacheDocument::BOTH_TIMINGS;

    if (emit_before_metric) {
      body << nested_text;
    }
    body << MetricOp(uses_d1, glyph_size);
    for (int hit = 0; hit < in_progress_cache_hits; ++hit) {
      body << GlyphDraw(nested_main_font, i, doc.matrix_mode());
    }

    if (i == 0 && !doc.render_fillers_after_nested()) {
      body << filler_draws.str();
    }

    if (emit_after_metric) {
      body << nested_text;
    }
    if (nested_text.empty()) {
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
    body << MalformedPrelude(malformed_charproc)
         << MetricOp(doc.fillers_use_d1(), glyph_size)
         << GlyphBody(doc.glyph_program_style(), glyph_size);
    objects[filler.char_obj] = StreamObject(body.str());
  }

  {
    std::ostringstream body;
    body << "BT /" << page_main_font << " " << page_font_size
         << " Tf 72 600 Td (A) Tj ET\n";
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
