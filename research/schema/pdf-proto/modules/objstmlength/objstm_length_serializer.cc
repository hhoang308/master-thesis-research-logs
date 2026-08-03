#include "modules/objstmlength/objstm_length_serializer.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr char kDefaultHeader[] = "%PDF-1.5";
constexpr int kMaxPageDim = 4096;
constexpr size_t kMaxPageContent = 512;
constexpr uint32_t kMaxPaddingSpaces = 32;

uint32_t ClampU32(uint32_t value, uint32_t lo, uint32_t hi) {
  return std::max(lo, std::min(value, hi));
}

std::string Spaces(uint32_t count) {
  return std::string(count, ' ');
}

std::string StreamObject(const std::string& dict_body,
                         const std::string& stream_data) {
  std::ostringstream out;
  out << "<< " << dict_body << " >>\nstream\n"
      << stream_data << "\nendstream";
  return out.str();
}

std::string HeaderLine(const std::string& candidate) {
  if (candidate.size() == 8 && candidate.rfind("%PDF-1.", 0) == 0 &&
      candidate[7] >= '0' && candidate[7] <= '9') {
    return candidate;
  }
  return kDefaultHeader;
}

std::string PackXrefEntry(uint8_t type, uint32_t field2, uint16_t field3) {
  std::string entry;
  entry.push_back(static_cast<char>(type));
  entry.push_back(static_cast<char>((field2 >> 24) & 0xff));
  entry.push_back(static_cast<char>((field2 >> 16) & 0xff));
  entry.push_back(static_cast<char>((field2 >> 8) & 0xff));
  entry.push_back(static_cast<char>(field2 & 0xff));
  entry.push_back(static_cast<char>((field3 >> 8) & 0xff));
  entry.push_back(static_cast<char>(field3 & 0xff));
  return entry;
}

}  // namespace

pdf_objstmlength::ObjstmLengthDocument CanonicalizeObjstmLengthDocument(
    const pdf_objstmlength::ObjstmLengthDocument& doc) {
  pdf_objstmlength::ObjstmLengthDocument canon;
  canon.set_pdf_header(HeaderLine(doc.pdf_header()));
  canon.set_include_binary_comment(doc.include_binary_comment());
  canon.set_page_width(ClampU32(doc.page_width(), 1, kMaxPageDim));
  canon.set_page_height(ClampU32(doc.page_height(), 1, kMaxPageDim));
  canon.set_catalog_padding_spaces(
      ClampU32(doc.catalog_padding_spaces(), 0, kMaxPaddingSpaces));
  canon.set_length_object_trailing_spaces(
      ClampU32(doc.length_object_trailing_spaces(), 0, kMaxPaddingSpaces));

  std::string page_content = doc.page_content();
  if (page_content.size() > kMaxPageContent) {
    page_content.resize(kMaxPageContent);
  }
  canon.set_page_content(page_content);
  return canon;
}

std::string SerializeObjstmLengthPdf(
    const pdf_objstmlength::ObjstmLengthDocument& doc) {
  const pdf_objstmlength::ObjstmLengthDocument canon =
      CanonicalizeObjstmLengthDocument(doc);

  std::ostringstream objstm_a;
  objstm_a << "1 0 " << Spaces(canon.catalog_padding_spaces())
           << "<< /Type /Catalog /Pages 2 0 R >>";
  const std::string objstm_a_data = objstm_a.str();

  std::ostringstream objstm_b;
  objstm_b << "6 0 " << objstm_a_data.size()
           << Spaces(canon.length_object_trailing_spaces());
  const std::string objstm_b_data = objstm_b.str();

  std::vector<std::pair<int, std::string>> objects;
  objects.push_back(
      {2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});

  std::ostringstream page;
  page << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
       << canon.page_width() << " " << canon.page_height()
       << "] /Resources << >> /Contents 7 0 R >>";
  objects.push_back({3, page.str()});

  objects.push_back(
      {4, StreamObject("/Type /ObjStm /N 1 /First 4 /Length 6 0 R",
                       objstm_a_data)});

  std::ostringstream objstm_b_dict;
  objstm_b_dict << "/Type /ObjStm /N 1 /First 4 /Length "
                << objstm_b_data.size();
  objects.push_back({5, StreamObject(objstm_b_dict.str(), objstm_b_data)});

  std::ostringstream contents_dict;
  contents_dict << "/Length " << canon.page_content().size();
  objects.push_back({7, StreamObject(contents_dict.str(), canon.page_content())});

  std::string out = canon.pdf_header() + "\n";
  if (canon.include_binary_comment()) {
    out += "%\xE2\xE3\xCF\xD3\n";
  }

  std::vector<uint32_t> offsets(9, 0);
  for (const auto& [obj_num, body] : objects) {
    offsets[obj_num] = static_cast<uint32_t>(out.size());
    out += std::to_string(obj_num) + " 0 obj\n";
    out += body;
    out += "\nendobj\n";
  }

  offsets[8] = static_cast<uint32_t>(out.size());
  std::string xref_entries;
  xref_entries += PackXrefEntry(0, 0, 65535);
  xref_entries += PackXrefEntry(2, 4, 0);
  xref_entries += PackXrefEntry(1, offsets[2], 0);
  xref_entries += PackXrefEntry(1, offsets[3], 0);
  xref_entries += PackXrefEntry(1, offsets[4], 0);
  xref_entries += PackXrefEntry(1, offsets[5], 0);
  xref_entries += PackXrefEntry(2, 5, 0);
  xref_entries += PackXrefEntry(1, offsets[7], 0);
  xref_entries += PackXrefEntry(1, offsets[8], 0);

  std::ostringstream xref_dict;
  xref_dict << "/Type /XRef /Size 9 /Root 1 0 R /W [1 4 2] /Index [0 9]"
            << " /Length " << xref_entries.size();
  out += "8 0 obj\n";
  out += StreamObject(xref_dict.str(), xref_entries);
  out += "\nendobj\n";
  out += "startxref\n" + std::to_string(offsets[8]) + "\n%%EOF\n";
  return out;
}
