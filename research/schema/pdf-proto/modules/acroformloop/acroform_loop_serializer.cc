#include "modules/acroformloop/acroform_loop_serializer.h"

#include <algorithm>
#include <cstdint>
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

std::string StreamObject(const std::string& dict_src, const std::string& data) {
  std::ostringstream out;
  out << dict_src << "\nstream\n" << data << "\nendstream";
  return out.str();
}

std::string FieldName(int index) {
  return std::string("loop-") + static_cast<char>('a' + (index % 26));
}

void AppendXrefEntry(std::string* out, uint32_t offset) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%010u 00000 n \n", offset);
  *out += buf;
}

}  // namespace

pdf_acroformloop::AcroFormLoopDocument CanonicalizeAcroFormLoopDocument(
    const pdf_acroformloop::AcroFormLoopDocument& doc) {
  pdf_acroformloop::AcroFormLoopDocument canon;
  canon.set_loop_shape(doc.loop_shape());
  canon.set_parent_mode(doc.parent_mode());
  canon.set_page_width(ClampU32(doc.page_width(), 1, 4096));
  canon.set_page_height(ClampU32(doc.page_height(), 1, 4096));
  canon.set_include_need_appearances(doc.include_need_appearances());
  canon.set_include_field_names(doc.include_field_names());
  canon.set_nonempty_contents(doc.nonempty_contents());

  switch (doc.loop_shape()) {
    case pdf_acroformloop::AcroFormLoopDocument::SELF_LOOP:
      canon.set_loop_nodes(1);
      break;
    case pdf_acroformloop::AcroFormLoopDocument::CYCLE:
      canon.set_loop_nodes(ClampU32(doc.loop_nodes(), 3, 8));
      break;
    case pdf_acroformloop::AcroFormLoopDocument::MUTUAL_LOOP:
    default:
      canon.set_loop_nodes(2);
      break;
  }
  return canon;
}

std::string SerializeAcroFormLoopPdf(
    const pdf_acroformloop::AcroFormLoopDocument& doc) {
  const pdf_acroformloop::AcroFormLoopDocument canon =
      CanonicalizeAcroFormLoopDocument(doc);

  constexpr int kCatalogObj = 1;
  constexpr int kPagesObj = 2;
  constexpr int kPageObj = 3;
  constexpr int kContentsObj = 4;
  constexpr int kAcroFormObj = 5;
  constexpr int kFirstFieldObj = 6;

  const int loop_nodes = static_cast<int>(canon.loop_nodes());
  std::vector<PdfObject> objects;
  objects.push_back(
      {kCatalogObj, "<< /Type /Catalog /Pages 2 0 R /AcroForm 5 0 R >>"});
  objects.push_back({kPagesObj, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});

  std::ostringstream page;
  page << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << canon.page_width()
       << " " << canon.page_height() << "] /Contents 4 0 R >>";
  objects.push_back({kPageObj, page.str()});

  const std::string contents = canon.nonempty_contents() ? "BT ET" : "";
  objects.push_back(
      {kContentsObj, StreamObject("<< /Length " + std::to_string(contents.size()) +
                                      " >>",
                                  contents)});

  std::ostringstream acroform;
  acroform << "<< /Fields [" << kFirstFieldObj << " 0 R]";
  if (canon.include_need_appearances()) {
    acroform << " /NeedAppearances true";
  }
  acroform << " >>";
  objects.push_back({kAcroFormObj, acroform.str()});

  for (int i = 0; i < loop_nodes; ++i) {
    const int obj_num = kFirstFieldObj + i;
    int kid_ref = obj_num;
    switch (canon.loop_shape()) {
      case pdf_acroformloop::AcroFormLoopDocument::SELF_LOOP:
        kid_ref = obj_num;
        break;
      case pdf_acroformloop::AcroFormLoopDocument::MUTUAL_LOOP:
      case pdf_acroformloop::AcroFormLoopDocument::CYCLE:
      default:
        kid_ref = kFirstFieldObj + ((i + 1) % loop_nodes);
        break;
    }

    int parent_ref = obj_num;
    switch (canon.parent_mode()) {
      case pdf_acroformloop::AcroFormLoopDocument::PREVIOUS_PARENT:
        parent_ref = kFirstFieldObj + ((i + loop_nodes - 1) % loop_nodes);
        break;
      case pdf_acroformloop::AcroFormLoopDocument::ROOT_PARENT:
        parent_ref = kAcroFormObj;
        break;
      case pdf_acroformloop::AcroFormLoopDocument::SELF_PARENT:
      default:
        parent_ref = obj_num;
        break;
    }

    std::ostringstream field;
    field << "<< /Kids [" << kid_ref << " 0 R] /Parent " << parent_ref
          << " 0 R";
    if (canon.include_field_names()) {
      field << " /T (" << FieldName(i) << ")";
    }
    field << " >>";
    objects.push_back({obj_num, field.str()});
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
