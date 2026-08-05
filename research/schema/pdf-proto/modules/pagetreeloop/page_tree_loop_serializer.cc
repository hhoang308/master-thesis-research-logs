#include "modules/pagetreeloop/page_tree_loop_serializer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct PdfObject {
  int num;
  std::string body;
};

uint32_t ClampU32(uint32_t value, uint32_t lo, uint32_t hi) {
  return std::max(lo, std::min(value, hi));
}

void AppendXrefEntry(std::string* out, uint32_t offset) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%010u 00000 n \n", offset);
  *out += buf;
}

}  // namespace

pdf_pagetreeloop::PageTreeLoopDocument CanonicalizePageTreeLoopDocument(
    const pdf_pagetreeloop::PageTreeLoopDocument& doc) {
  pdf_pagetreeloop::PageTreeLoopDocument canon;
  canon.set_loop_shape(doc.loop_shape());
  canon.set_count_mode(doc.count_mode());

  switch (doc.loop_shape()) {
    case pdf_pagetreeloop::PageTreeLoopDocument::SELF_LOOP:
      canon.set_loop_nodes(1);
      break;
    case pdf_pagetreeloop::PageTreeLoopDocument::CYCLE:
      canon.set_loop_nodes(ClampU32(doc.loop_nodes(), 3, 8));
      break;
    case pdf_pagetreeloop::PageTreeLoopDocument::MUTUAL_LOOP:
    default:
      canon.set_loop_nodes(2);
      break;
  }

  if (doc.count_mode() ==
      pdf_pagetreeloop::PageTreeLoopDocument::HUGE_COUNT) {
    canon.set_huge_count(ClampU32(doc.huge_count(), 50001, 1000000));
  } else {
    canon.set_huge_count(0);
  }
  return canon;
}

std::string SerializePageTreeLoopPdf(
    const pdf_pagetreeloop::PageTreeLoopDocument& doc) {
  const pdf_pagetreeloop::PageTreeLoopDocument canon =
      CanonicalizePageTreeLoopDocument(doc);

  constexpr int kCatalogObj = 1;
  constexpr int kFirstPagesObj = 2;

  const int loop_nodes = static_cast<int>(canon.loop_nodes());
  const uint32_t count_value =
      canon.count_mode() ==
              pdf_pagetreeloop::PageTreeLoopDocument::HUGE_COUNT
          ? canon.huge_count()
          : 0;

  std::vector<PdfObject> objects;
  objects.push_back(
      {kCatalogObj, "<< /Type /Catalog /Pages 2 0 R >>"});

  for (int i = 0; i < loop_nodes; ++i) {
    const int obj_num = kFirstPagesObj + i;
    int kid_ref = obj_num;
    switch (canon.loop_shape()) {
      case pdf_pagetreeloop::PageTreeLoopDocument::SELF_LOOP:
        kid_ref = obj_num;
        break;
      case pdf_pagetreeloop::PageTreeLoopDocument::MUTUAL_LOOP:
      case pdf_pagetreeloop::PageTreeLoopDocument::CYCLE:
      default:
        kid_ref = kFirstPagesObj + ((i + 1) % loop_nodes);
        break;
    }

    std::ostringstream body;
    body << "<< /Type /Pages /Kids [" << kid_ref << " 0 R] /Count "
         << count_value << " >>";
    objects.push_back({obj_num, body.str()});
  }

  std::vector<std::string> parts;
  parts.push_back("%PDF-1.4\n%\xe2\xe3\xcf\xd3\n");
  std::vector<uint32_t> offsets(objects.size() + 1, 0);
  auto current_size = [&parts]() {
    size_t total = 0;
    for (const auto& part : parts) {
      total += part.size();
    }
    return total;
  };

  for (const auto& obj : objects) {
    offsets[obj.num] = static_cast<uint32_t>(current_size());
    std::ostringstream wrapped;
    wrapped << obj.num << " 0 obj\n" << obj.body << "\nendobj\n";
    parts.push_back(wrapped.str());
  }

  const uint32_t xref_offset = static_cast<uint32_t>(current_size());
  std::string xref = "xref\n0 " + std::to_string(offsets.size()) + "\n";
  xref += "0000000000 65535 f \n";
  for (size_t i = 1; i < offsets.size(); ++i) {
    AppendXrefEntry(&xref, offsets[i]);
  }
  parts.push_back(xref);

  std::ostringstream trailer;
  trailer << "trailer\n<< /Size " << offsets.size() << " /Root 1 0 R >>\n"
          << "startxref\n" << xref_offset << "\n%%EOF\n";
  parts.push_back(trailer.str());

  std::string pdf;
  for (const auto& part : parts) {
    pdf += part;
  }
  return pdf;
}
