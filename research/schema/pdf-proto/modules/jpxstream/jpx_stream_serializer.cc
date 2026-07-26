#include "modules/jpxstream/jpx_stream_serializer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kPageWidth = 612;
constexpr uint32_t kPageHeight = 792;
constexpr uint32_t kMaxJpxExtent = 1u << 20;

uint32_t ClampU32(uint32_t value, uint32_t lo, uint32_t hi) {
  return std::max(lo, std::min(value, hi));
}

void AppendU16(std::string* out, uint32_t value) {
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>(value & 0xff));
}

void AppendU32(std::string* out, uint32_t value) {
  out->push_back(static_cast<char>((value >> 24) & 0xff));
  out->push_back(static_cast<char>((value >> 16) & 0xff));
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>(value & 0xff));
}

std::string Marker(uint8_t marker, const std::string& payload = "") {
  std::string out;
  out.push_back(static_cast<char>(0xff));
  out.push_back(static_cast<char>(marker));
  if (!payload.empty()) {
    AppendU16(&out, static_cast<uint32_t>(payload.size() + 2));
    out += payload;
  }
  return out;
}

std::string PdfColorSpaceName(
    pdf_jpxstream::JpxStreamDocument::PdfColorSpace color_space) {
  switch (color_space) {
    case pdf_jpxstream::JpxStreamDocument::DEVICE_RGB:
      return "DeviceRGB";
    case pdf_jpxstream::JpxStreamDocument::DEVICE_CMYK:
      return "DeviceCMYK";
    case pdf_jpxstream::JpxStreamDocument::DEVICE_GRAY:
    default:
      return "DeviceGray";
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

std::vector<uint32_t> CanonicalQuantSteps(
    const pdf_jpxstream::JpxStreamDocument& doc) {
  const uint32_t decomp = ClampU32(doc.decomposition_levels(), 1, 31);
  const uint32_t min_steps = ClampU32(3 * decomp + 1, 4, 96);
  std::vector<uint32_t> steps;
  if (doc.quant_steps_size() == 0) {
    steps.assign(min_steps, 0x08);
  } else {
    for (uint32_t step : doc.quant_steps()) {
      if (steps.size() >= 96) {
        break;
      }
      steps.push_back(ClampU32(step, 0, 255));
    }
    while (steps.size() < min_steps) {
      steps.push_back(0x08);
    }
  }
  return steps;
}

}  // namespace

pdf_jpxstream::JpxStreamDocument CanonicalizeJpxStreamDocument(
    const pdf_jpxstream::JpxStreamDocument& doc) {
  pdf_jpxstream::JpxStreamDocument canon;

  const uint32_t width = ClampU32(doc.width(), 1, kMaxJpxExtent);
  const uint32_t height = ClampU32(doc.height(), 1, kMaxJpxExtent);
  const uint32_t x_offset = ClampU32(doc.x_offset(), 0, width - 1);
  const uint32_t y_offset = ClampU32(doc.y_offset(), 0, height - 1);
  const uint32_t tile_x_offset = ClampU32(doc.tile_x_offset(), 0, x_offset);
  const uint32_t tile_y_offset = ClampU32(doc.tile_y_offset(), 0, y_offset);
  const uint32_t tile_width =
      ClampU32(doc.tile_width(), x_offset - tile_x_offset + 1, kMaxJpxExtent);
  const uint32_t tile_height =
      ClampU32(doc.tile_height(), y_offset - tile_y_offset + 1, kMaxJpxExtent);

  canon.set_width(width);
  canon.set_height(height);
  canon.set_x_offset(x_offset);
  canon.set_y_offset(y_offset);
  canon.set_tile_width(tile_width);
  canon.set_tile_height(tile_height);
  canon.set_tile_x_offset(tile_x_offset);
  canon.set_tile_y_offset(tile_y_offset);
  canon.set_components(ClampU32(doc.components(), 1, 4));
  canon.set_bits_per_component(ClampU32(doc.bits_per_component(), 1, 32));
  canon.set_x_sample_separation(ClampU32(doc.x_sample_separation(), 1, 255));
  canon.set_y_sample_separation(ClampU32(doc.y_sample_separation(), 1, 255));

  canon.set_progression_order(ClampU32(doc.progression_order(), 0, 4));
  canon.set_layers(ClampU32(doc.layers(), 1, 32));
  canon.set_multi_component_transform(
      ClampU32(doc.multi_component_transform(), 0, 1));
  canon.set_decomposition_levels(
      ClampU32(doc.decomposition_levels(), 1, 31));
  canon.set_code_block_width_exp_minus2(
      ClampU32(doc.code_block_width_exp_minus2(), 0, 8));
  canon.set_code_block_height_exp_minus2(
      ClampU32(doc.code_block_height_exp_minus2(), 0, 8));
  canon.set_code_block_style(ClampU32(doc.code_block_style(), 0, 255));
  canon.set_transform(ClampU32(doc.transform(), 0, 1));

  for (uint32_t step : CanonicalQuantSteps(doc)) {
    canon.add_quant_steps(step);
  }

  canon.set_tile_index(ClampU32(doc.tile_index(), 0, 1024));
  canon.set_tile_part_payload_bytes(
      ClampU32(doc.tile_part_payload_bytes(), 0, 1024));
  canon.set_include_eoc(doc.include_eoc());

  canon.set_pdf_width(ClampU32(doc.pdf_width(), 1, 4096));
  canon.set_pdf_height(ClampU32(doc.pdf_height(), 1, 4096));
  canon.set_pdf_color_space(doc.pdf_color_space());
  canon.set_display_scale(ClampU32(doc.display_scale(), 1, 512));

  return canon;
}

std::string SerializeJpxCodestream(
    const pdf_jpxstream::JpxStreamDocument& doc) {
  pdf_jpxstream::JpxStreamDocument canon = CanonicalizeJpxStreamDocument(doc);

  std::string siz;
  AppendU16(&siz, 0);  // Rsiz
  AppendU32(&siz, canon.width());
  AppendU32(&siz, canon.height());
  AppendU32(&siz, canon.x_offset());
  AppendU32(&siz, canon.y_offset());
  AppendU32(&siz, canon.tile_width());
  AppendU32(&siz, canon.tile_height());
  AppendU32(&siz, canon.tile_x_offset());
  AppendU32(&siz, canon.tile_y_offset());
  AppendU16(&siz, canon.components());
  const uint32_t ssiz = canon.bits_per_component() - 1;
  for (uint32_t i = 0; i < canon.components(); ++i) {
    siz.push_back(static_cast<char>(ssiz & 0x7f));
    siz.push_back(static_cast<char>(canon.x_sample_separation()));
    siz.push_back(static_cast<char>(canon.y_sample_separation()));
  }

  std::string cod;
  cod.push_back(0);  // Scod: default precincts.
  cod.push_back(static_cast<char>(canon.progression_order()));
  AppendU16(&cod, canon.layers());
  cod.push_back(static_cast<char>(canon.multi_component_transform()));
  cod.push_back(static_cast<char>(canon.decomposition_levels()));
  cod.push_back(static_cast<char>(canon.code_block_width_exp_minus2()));
  cod.push_back(static_cast<char>(canon.code_block_height_exp_minus2()));
  cod.push_back(static_cast<char>(canon.code_block_style()));
  cod.push_back(static_cast<char>(canon.transform()));

  std::string qcd;
  qcd.push_back(0);  // no quantization.
  for (uint32_t step : canon.quant_steps()) {
    qcd.push_back(static_cast<char>(step & 0xff));
  }

  std::string sot;
  AppendU16(&sot, canon.tile_index());
  AppendU32(&sot, 14 + canon.tile_part_payload_bytes());
  sot.push_back(0);  // TPsot
  sot.push_back(1);  // TNsot

  std::string out;
  out += Marker(0x4f);
  out += Marker(0x51, siz);
  out += Marker(0x52, cod);
  out += Marker(0x5c, qcd);
  out += Marker(0x90, sot);
  out += Marker(0x93);
  out.append(canon.tile_part_payload_bytes(), '\0');
  if (canon.include_eoc()) {
    out += Marker(0xd9);
  }
  return out;
}

std::string SerializeJpxPdf(const pdf_jpxstream::JpxStreamDocument& doc) {
  pdf_jpxstream::JpxStreamDocument canon = CanonicalizeJpxStreamDocument(doc);
  std::string jpx = SerializeJpxCodestream(canon);

  std::ostringstream content;
  content << "q " << canon.display_scale() << " 0 0 " << canon.display_scale()
          << " 72 600 cm /Im0 Do Q\n";

  std::string image_dict =
      " /Type /XObject /Subtype /Image /Width " +
      std::to_string(canon.pdf_width()) + " /Height " +
      std::to_string(canon.pdf_height()) + " /ColorSpace /" +
      PdfColorSpaceName(canon.pdf_color_space()) +
      " /BitsPerComponent 8 /Filter /JPXDecode";

  std::vector<std::string> objects = {
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " +
          std::to_string(kPageWidth) + " " + std::to_string(kPageHeight) +
          "] /Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>",
      PdfStream(jpx, image_dict),
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
         " /Root 1 0 R >>\n";
  out += "startxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
  return out;
}
