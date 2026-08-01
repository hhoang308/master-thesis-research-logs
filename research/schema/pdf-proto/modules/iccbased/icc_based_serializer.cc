#include "modules/iccbased/icc_based_serializer.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kPageWidth = 612;
constexpr int kPageHeight = 792;

uint32_t ClampU32(uint32_t value, uint32_t lo, uint32_t hi) {
  return std::max(lo, std::min(value, hi));
}

std::string AlternateColorSpaceName(
    pdf_iccbased::IccBasedDocument::AlternateColorSpace color_space) {
  switch (color_space) {
    case pdf_iccbased::IccBasedDocument::DEVICE_RGB:
      return "DeviceRGB";
    case pdf_iccbased::IccBasedDocument::DEVICE_CMYK:
      return "DeviceCMYK";
    case pdf_iccbased::IccBasedDocument::DEVICE_GRAY:
    default:
      return "DeviceGray";
  }
}

std::string PdfStream(const std::string& data, const std::string& extra_dict,
                      int length_delta) {
  std::ostringstream out;
  out << "<< /Length " << (static_cast<long>(data.size()) + length_delta)
      << extra_dict << " >>\nstream\n"
      << data << "\nendstream";
  return out.str();
}

void AppendXrefEntry(std::string* out, uint32_t offset) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%010u 00000 n \n", offset);
  *out += buf;
}

}  // namespace

pdf_iccbased::IccBasedDocument CanonicalizeIccBasedDocument(
    const pdf_iccbased::IccBasedDocument& doc) {
  pdf_iccbased::IccBasedDocument canon;
  canon.set_width(ClampU32(doc.width(), 1, 128));
  canon.set_height(ClampU32(doc.height(), 1, 128));
  canon.set_bits_per_component(ClampU32(doc.bits_per_component(), 1, 16));
  canon.set_icc_components(ClampU32(doc.icc_components(), 0, 4));
  canon.set_alternate(doc.alternate());
  canon.set_display_scale(ClampU32(doc.display_scale(), 1, 512));
  canon.set_image_length_delta(doc.image_length_delta());
  canon.set_icc_length_delta(doc.icc_length_delta());

  std::string image_data = doc.image_data();
  if (image_data.empty()) {
    image_data.assign(1, '\0');
  } else if (image_data.size() > 256) {
    image_data.resize(256);
  }
  canon.set_image_data(image_data);

  std::string icc_profile = doc.icc_profile();
  if (icc_profile.size() > 256) {
    icc_profile.resize(256);
  }
  canon.set_icc_profile(icc_profile);

  const int want_range =
      canon.icc_components() == 0 ? 0 : 2 * static_cast<int>(canon.icc_components());
  if (want_range > 0 && doc.range_size() >= want_range) {
    for (int i = 0; i < want_range; ++i) {
      canon.add_range(doc.range(i));
    }
  }

  return canon;
}

std::string SerializeIccBasedPdf(const pdf_iccbased::IccBasedDocument& doc) {
  const pdf_iccbased::IccBasedDocument canon = CanonicalizeIccBasedDocument(doc);

  const int catalog_obj = 1;
  const int pages_obj = 2;
  const int page_obj = 3;
  const int icc_obj = 4;
  const int image_obj = 5;
  const int contents_obj = 6;

  std::ostringstream icc_extra;
  icc_extra << " /N " << canon.icc_components()
            << " /Alternate /" << AlternateColorSpaceName(canon.alternate());
  if (canon.range_size() > 0) {
    icc_extra << " /Range [";
    for (int i = 0; i < canon.range_size(); ++i) {
      if (i) {
        icc_extra << ' ';
      }
      icc_extra << canon.range(i);
    }
    icc_extra << "]";
  }

  std::ostringstream content;
  content << "q " << canon.display_scale() << " 0 0 " << canon.display_scale()
          << " 72 600 cm /Im0 Do Q\n";

  std::ostringstream image_extra;
  image_extra << " /Type /XObject /Subtype /Image"
              << " /Width " << canon.width()
              << " /Height " << canon.height()
              << " /BitsPerComponent " << canon.bits_per_component()
              << " /ColorSpace [/ICCBased " << icc_obj << " 0 R]";

  std::vector<std::string> objects = {
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " +
          std::to_string(kPageWidth) + " " + std::to_string(kPageHeight) +
          "] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 6 0 R >>",
      PdfStream(canon.icc_profile(), icc_extra.str(), canon.icc_length_delta()),
      PdfStream(canon.image_data(), image_extra.str(), canon.image_length_delta()),
      PdfStream(content.str(), "", 0),
  };

  std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
  std::vector<uint32_t> offsets(objects.size() + 1, 0);
  for (size_t i = 0; i < objects.size(); ++i) {
    offsets[i + 1] = static_cast<uint32_t>(out.size());
    out += std::to_string(i + 1) + " 0 obj\n";
    out += objects[i];
    out += "\nendobj\n";
  }

  const uint32_t xref_offset = static_cast<uint32_t>(out.size());
  out += "xref\n0 " + std::to_string(offsets.size()) + "\n";
  out += "0000000000 65535 f \n";
  for (size_t i = 1; i < offsets.size(); ++i) {
    AppendXrefEntry(&out, offsets[i]);
  }
  out += "trailer\n<< /Size " + std::to_string(offsets.size()) +
         " /Root " + std::to_string(catalog_obj) + " 0 R >>\n";
  out += "startxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";

  (void)pages_obj;
  (void)page_obj;
  (void)image_obj;
  (void)contents_obj;
  return out;
}
