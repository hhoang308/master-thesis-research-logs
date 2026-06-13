#include <sstream>
#include <vector>
#include <string>
#include <zlib.h>
#include "pdf.pb.h"

struct XrefEntry {
  uint32_t offset;
  uint32_t generation;
  bool in_use;
};

std::string SerializePdf(const pdf_proto::PdfDocument& doc) {
  std::ostringstream out;
  std::vector<XrefEntry> xref;

  // object 0 is always the free-list head
  xref.push_back({0, 65535, false});

  auto record_offset = [&]() -> uint32_t {
    uint32_t pos = (uint32_t)out.tellp();
    xref.push_back({pos, 0, true});
    return (uint32_t)xref.size() - 1;  // object number just assigned
  };

  out << "%PDF-1.4\n";

  // Object 1: Catalog — always object number 1
  record_offset();  // → object 1
  uint32_t pages_obj_num = 2;  // we know Pages will be object 2
  out << "1 0 obj\n<< /Type /Catalog /Pages " << pages_obj_num << " 0 R >>\nendobj\n";

  // Object 2: Pages (page tree root)
  record_offset();  // → object 2
  int page_count = doc.pages_size();
  // kids will be objects 3, 4, 5, ...
  out << "2 0 obj\n<< /Type /Pages /Count " << page_count << " /Kids [";
  for (int i = 0; i < page_count; i++) {
    out << (3 + i) << " 0 R ";
  }
  out << "] >>\nendobj\n";

  // Objects 3..N: Pages
  for (int i = 0; i < page_count; i++) {
    record_offset();  // → object 3+i
    const pdf_proto::Page& p = doc.pages(i);
    float w = p.has_width()  ? p.width()  : 612.0f;
    float h = p.has_height() ? p.height() : 792.0f;
    // clamp to sane range — LPM will try NaN, negative, huge values
    w = std::max(1.0f, std::min(w, 14400.0f));
    h = std::max(1.0f, std::min(h, 14400.0f));
    out << (3 + i) << " 0 obj\n"
        << "<< /Type /Page /Parent 2 0 R "
        << "/MediaBox [0 0 " << w << " " << h << "] >>\n"
        << "endobj\n";
  }

  // Xref table
  uint32_t xref_offset = (uint32_t)out.tellp();
  out << "xref\n";
  out << "0 " << xref.size() << "\n";
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