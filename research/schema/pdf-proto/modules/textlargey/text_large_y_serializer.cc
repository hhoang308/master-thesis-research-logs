#include "modules/textlargey/text_large_y_serializer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr char kDefaultHeaderLine[] = "%PDF-1.4";
constexpr char kDefaultFontName[] = "Helvetica";
constexpr uint32_t kDefaultFontSize = 12;
constexpr uint32_t kDefaultPageWidth = 612;
constexpr uint32_t kDefaultPageHeight = 200000500;
constexpr uint32_t kDefaultNormalX = 72;
constexpr uint32_t kDefaultNormalY = 72;
constexpr char kDefaultNormalText[] = "N";
constexpr uint32_t kDefaultLargeX = 72;
constexpr uint32_t kDefaultLargeY = 200000000;
constexpr char kDefaultLargeText[] = "A";

constexpr uint32_t kMinPageHeight = 100000100;
constexpr uint32_t kMaxPageHeight = 1000000000;
constexpr uint32_t kMinLargeY = 100000001;

uint32_t ClampU32(uint32_t value, uint32_t lo, uint32_t hi) {
  return std::max(lo, std::min(value, hi));
}

std::string SanitizeHeaderLine(const std::string& header) {
  if (header.size() == 8 &&
      header[0] == '%' &&
      header[1] == 'P' &&
      header[2] == 'D' &&
      header[3] == 'F' &&
      header[4] == '-' &&
      header[5] == '1' &&
      header[6] == '.' &&
      std::isdigit(static_cast<unsigned char>(header[7]))) {
    return header;
  }
  return kDefaultHeaderLine;
}

std::string SanitizeFontName(const std::string& name) {
  std::string out;
  for (unsigned char ch : name) {
    if (std::isalnum(ch) || ch == '-' || ch == '_') {
      out.push_back(static_cast<char>(ch));
    }
    if (out.size() == 48) {
      break;
    }
  }
  if (out.empty()) {
    out = kDefaultFontName;
  }
  return out;
}

std::string SanitizeLiteralText(const std::string& text,
                                const std::string& fallback) {
  std::string out;
  for (unsigned char ch : text) {
    if (ch >= 0x20 && ch <= 0x7e && ch != '(' && ch != ')' && ch != '\\') {
      out.push_back(static_cast<char>(ch));
    }
    if (out.size() == 32) {
      break;
    }
  }
  if (out.empty()) {
    out = fallback;
  }
  return out;
}

std::string EscapeLiteralString(const std::string& text) {
  std::string out;
  for (char ch : text) {
    if (ch == '(' || ch == ')' || ch == '\\') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

std::string StreamObject(const std::string& data) {
  std::ostringstream out;
  out << "<< /Length " << data.size() << " >>\nstream\n"
      << data << "\nendstream";
  return out.str();
}

void AppendXrefEntry(std::string* out, uint32_t offset) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%010u 00000 n \n", offset);
  *out += buf;
}

}  // namespace

pdf_textlargey::TextLargeYDocument CanonicalizeTextLargeYDocument(
    const pdf_textlargey::TextLargeYDocument& doc) {
  pdf_textlargey::TextLargeYDocument canon;
  canon.set_header_line(SanitizeHeaderLine(doc.header_line()));
  canon.set_font_name(SanitizeFontName(doc.font_name()));
  canon.set_font_size(ClampU32(doc.font_size(), 1, 4096));
  canon.set_page_width(ClampU32(doc.page_width(), 1, 20000));
  canon.set_page_height(
      ClampU32(doc.page_height(), kMinPageHeight, kMaxPageHeight));
  canon.set_normal_x(ClampU32(doc.normal_x(), 0, canon.page_width()));
  canon.set_normal_y(ClampU32(doc.normal_y(), 0, 99999999));
  canon.set_normal_text(
      SanitizeLiteralText(doc.normal_text(), kDefaultNormalText));
  canon.set_large_x(ClampU32(doc.large_x(), 0, canon.page_width()));
  canon.set_large_y(
      ClampU32(doc.large_y(), kMinLargeY, canon.page_height() - 1));
  canon.set_large_text(
      SanitizeLiteralText(doc.large_text(), kDefaultLargeText));
  return canon;
}

std::string SerializeTextLargeYPdf(
    const pdf_textlargey::TextLargeYDocument& doc) {
  const pdf_textlargey::TextLargeYDocument canon =
      CanonicalizeTextLargeYDocument(doc);

  std::ostringstream content;
  content << "BT\n"
          << "/F1 " << canon.font_size() << " Tf\n"
          << "1 0 0 1 " << canon.normal_x() << " " << canon.normal_y()
          << " Tm\n"
          << "(" << EscapeLiteralString(canon.normal_text()) << ") Tj\n"
          << "1 0 0 1 " << canon.large_x() << " " << canon.large_y()
          << " Tm\n"
          << "(" << EscapeLiteralString(canon.large_text()) << ") Tj\n"
          << "ET";

  std::string pdf;
  std::vector<uint32_t> offsets(1, 0);
  auto append_object = [&](uint32_t obj_num, const std::string& body) {
    offsets.push_back(static_cast<uint32_t>(pdf.size()));
    pdf += std::to_string(obj_num);
    pdf += " 0 obj\n";
    pdf += body;
    pdf += "\nendobj\n";
  };

  pdf += canon.header_line();
  pdf += "\n%\342\343\317\323\n";

  append_object(1, "<< /Type /Catalog /Pages 2 0 R >>");
  append_object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
  {
    std::ostringstream page;
    page << "<< /Type /Page /Parent 2 0 R"
         << " /MediaBox [0 0 " << canon.page_width() << " "
         << canon.page_height() << "]"
         << " /Resources << /Font << /F1 4 0 R >> >>"
         << " /Contents 5 0 R >>";
    append_object(3, page.str());
  }
  {
    std::ostringstream font;
    font << "<< /Type /Font /Subtype /Type1 /BaseFont /"
         << canon.font_name() << " >>";
    append_object(4, font.str());
  }
  append_object(5, StreamObject(content.str()));

  const uint32_t xref_offset = static_cast<uint32_t>(pdf.size());
  pdf += "xref\n0 ";
  pdf += std::to_string(offsets.size());
  pdf += "\n0000000000 65535 f \n";
  for (size_t i = 1; i < offsets.size(); ++i) {
    AppendXrefEntry(&pdf, offsets[i]);
  }
  pdf += "trailer\n<< /Size ";
  pdf += std::to_string(offsets.size());
  pdf += " /Root 1 0 R >>\nstartxref\n";
  pdf += std::to_string(xref_offset);
  pdf += "\n%%EOF\n";
  return pdf;
}
