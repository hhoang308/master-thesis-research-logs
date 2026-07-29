#include "modules/dctstream/dct_stream_serializer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kPageWidth = 612;
constexpr int kPageHeight = 792;

uint32_t ClampU32(uint32_t value, uint32_t lo, uint32_t hi) {
  return std::max(lo, std::min(value, hi));
}

void AppendU16(std::string* out, uint32_t value) {
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>(value & 0xff));
}

std::string Segment(uint8_t marker, const std::string& payload) {
  std::string out;
  out.push_back(static_cast<char>(0xff));
  out.push_back(static_cast<char>(marker));
  AppendU16(&out, static_cast<uint32_t>(payload.size() + 2));
  out += payload;
  return out;
}

std::string BuildDhtTable(uint8_t table_class, uint8_t table_id,
                          uint8_t symbol) {
  std::string out;
  out.push_back(static_cast<char>((table_class << 4) | table_id));
  out.push_back(1);  // one code of bit length 1
  for (int i = 1; i < 16; ++i) {
    out.push_back(0);
  }
  out.push_back(static_cast<char>(symbol));
  return out;
}

std::vector<uint32_t> DefaultScanIds(uint32_t frame_components,
                                     bool second_scan) {
  if (!second_scan || frame_components == 1) {
    return {1};
  }
  std::vector<uint32_t> ids;
  for (uint32_t id = 1; id <= frame_components; ++id) {
    ids.push_back(id);
  }
  return ids;
}

std::vector<uint32_t> CanonicalScanIds(
    const pdf_dctstream::DctStreamDocument::Scan& scan,
    uint32_t frame_components, bool second_default) {
  std::vector<uint32_t> ids;
  std::set<uint32_t> seen;
  if (scan.component_ids_size() == 0) {
    ids = DefaultScanIds(frame_components, second_default);
  } else {
    for (int i = 0; i < scan.component_ids_size() && ids.size() < 4; ++i) {
      uint32_t id = ClampU32(scan.component_ids(i), 1, frame_components);
      if (seen.insert(id).second) {
        ids.push_back(id);
      }
    }
  }
  if (ids.empty()) {
    ids.push_back(1);
  }
  return ids;
}

std::string PdfColorSpaceName(
    pdf_dctstream::DctStreamDocument::PdfColorSpace color_space) {
  switch (color_space) {
    case pdf_dctstream::DctStreamDocument::DEVICE_GRAY:
      return "DeviceGray";
    case pdf_dctstream::DctStreamDocument::DEVICE_CMYK:
      return "DeviceCMYK";
    case pdf_dctstream::DctStreamDocument::DEVICE_RGB:
    default:
      return "DeviceRGB";
  }
}

std::string PdfStream(const std::string& data, const std::string& extra_dict) {
  std::ostringstream out;
  out << "<< /Length " << data.size() << extra_dict << " >>\nstream\n"
      << data << "\nendstream";
  return out.str();
}

void AppendXrefEntry(std::string* out, uint32_t offset) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%010u 00000 n \n", offset);
  *out += buf;
}

}  // namespace

pdf_dctstream::DctStreamDocument CanonicalizeDctStreamDocument(
    const pdf_dctstream::DctStreamDocument& doc) {
  pdf_dctstream::DctStreamDocument canon;
  const uint32_t width = ClampU32(doc.width(), 1, 256);
  const uint32_t height = ClampU32(doc.height(), 1, 256);
  const uint32_t frame_components = ClampU32(doc.frame_components(), 1, 4);

  canon.set_width(width);
  canon.set_height(height);
  canon.set_frame_components(frame_components);
  canon.set_pdf_color_space(doc.pdf_color_space());
  canon.set_display_scale(ClampU32(doc.display_scale(), 1, 512));

  const int scan_count =
      doc.scans_size() == 0 ? 2 : static_cast<int>(ClampU32(doc.scans_size(), 1, 8));
  for (int i = 0; i < scan_count; ++i) {
    pdf_dctstream::DctStreamDocument::Scan empty_scan;
    const auto& src = doc.scans_size() == 0 ? empty_scan : doc.scans(i);
    auto* dst = canon.add_scans();
    for (uint32_t id : CanonicalScanIds(src, frame_components, i == 1)) {
      dst->add_component_ids(id);
    }
    dst->set_entropy_padding_bytes(
        ClampU32(src.entropy_padding_bytes(), 0, 32));
  }

  return canon;
}

std::string SerializeDctStreamJpeg(
    const pdf_dctstream::DctStreamDocument& doc) {
  pdf_dctstream::DctStreamDocument canon = CanonicalizeDctStreamDocument(doc);
  const uint32_t width = canon.width();
  const uint32_t height = canon.height();
  const uint32_t frame_components = canon.frame_components();

  std::string jpeg("\xff\xd8", 2);

  std::string dqt;
  dqt.push_back(0);  // 8-bit precision, table 0
  dqt.append(64, '\x01');
  jpeg += Segment(0xdb, dqt);

  jpeg += Segment(0xc4,
                  BuildDhtTable(0, 0, 0x00) + BuildDhtTable(1, 0, 0x00));

  std::string sof0;
  sof0.push_back(8);  // precision
  AppendU16(&sof0, height);
  AppendU16(&sof0, width);
  sof0.push_back(static_cast<char>(frame_components));
  for (uint32_t id = 1; id <= frame_components; ++id) {
    sof0.push_back(static_cast<char>(id));
    sof0.push_back(0x11);  // h=1, v=1
    sof0.push_back(0);     // quant table 0
  }
  jpeg += Segment(0xc0, sof0);

  const uint32_t mcu_cols = (width + 7) / 8;
  const uint32_t mcu_rows = (height + 7) / 8;
  const uint32_t mcu_count = std::max<uint32_t>(1, mcu_cols * mcu_rows);
  for (const auto& scan : canon.scans()) {
    std::string sos;
    sos.push_back(static_cast<char>(scan.component_ids_size()));
    for (uint32_t id : scan.component_ids()) {
      sos.push_back(static_cast<char>(id));
      sos.push_back(0);  // DC table 0, AC table 0
    }
    sos.push_back(0);     // first coeff
    sos.push_back(0x3f);  // last coeff
    sos.push_back(0);     // Ah/Al
    jpeg += Segment(0xda, sos);

    const uint32_t data_units =
        mcu_count * static_cast<uint32_t>(scan.component_ids_size());
    const uint32_t min_bytes = std::max<uint32_t>(1, (data_units * 2 + 7) / 8);
    jpeg.append(min_bytes + scan.entropy_padding_bytes(), '\0');
  }

  jpeg.append("\xff\xd9", 2);
  return jpeg;
}

std::string SerializeDctStreamPdf(
    const pdf_dctstream::DctStreamDocument& doc) {
  pdf_dctstream::DctStreamDocument canon = CanonicalizeDctStreamDocument(doc);
  std::string jpeg = SerializeDctStreamJpeg(canon);

  const int catalog_obj = 1;
  const int pages_obj = 2;
  const int page_obj = 3;
  const int image_obj = 4;
  const int contents_obj = 5;

  std::ostringstream content;
  content << "q " << canon.display_scale() << " 0 0 " << canon.display_scale()
          << " 72 600 cm /Im0 Do Q\n";

  std::string image_dict =
      " /Type /XObject /Subtype /Image /Width " +
      std::to_string(canon.width()) + " /Height " +
      std::to_string(canon.height()) +
      " /ColorSpace /" + PdfColorSpaceName(canon.pdf_color_space()) +
      " /BitsPerComponent 8 /Filter /DCTDecode";

  std::vector<std::string> objects = {
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " +
          std::to_string(kPageWidth) + " " + std::to_string(kPageHeight) +
          "] /Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>",
      PdfStream(jpeg, image_dict),
      PdfStream(content.str(), ""),
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
