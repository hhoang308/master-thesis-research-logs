#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "modules/jpxstream/jpx_stream.pb.h"
#include "modules/jpxstream/jpx_stream_serializer.h"

namespace {

struct ParseResult {
  bool ok = false;
  bool saw_soc = false;
  bool saw_siz = false;
  bool saw_cod = false;
  bool saw_qcd = false;
  bool saw_sot = false;
  bool saw_sod = false;
  bool saw_eoc = false;
  std::string error;
  pdf_jpxstream::JpxStreamDocument doc;
};

uint8_t U8(const std::string& s, size_t pos) {
  return pos < s.size() ? static_cast<uint8_t>(s[pos]) : 0;
}

uint16_t U16(const std::string& s, size_t pos) {
  return static_cast<uint16_t>((U8(s, pos) << 8) | U8(s, pos + 1));
}

uint32_t U32(const std::string& s, size_t pos) {
  return (static_cast<uint32_t>(U8(s, pos)) << 24) |
         (static_cast<uint32_t>(U8(s, pos + 1)) << 16) |
         (static_cast<uint32_t>(U8(s, pos + 2)) << 8) |
         static_cast<uint32_t>(U8(s, pos + 3));
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

std::string ExtractJpxStream(const std::string& pdf, std::string* error) {
  size_t filter = pdf.find("/Filter /JPXDecode");
  if (filter == std::string::npos) {
    *error = "missing /Filter /JPXDecode";
    return {};
  }
  size_t stream = pdf.find("stream\n", filter);
  if (stream == std::string::npos) {
    *error = "JPX image object has no stream";
    return {};
  }
  stream += 7;
  size_t end = pdf.find("\nendstream", stream);
  if (end == std::string::npos) {
    *error = "JPX image stream has no endstream";
    return {};
  }
  return pdf.substr(stream, end - stream);
}

std::string ExtractJpxDict(const std::string& pdf, std::string* error) {
  size_t filter = pdf.find("/Filter /JPXDecode");
  if (filter == std::string::npos) {
    *error = "missing /Filter /JPXDecode";
    return {};
  }
  size_t obj = pdf.rfind("obj\n", filter);
  size_t stream = pdf.find("stream\n", filter);
  if (obj == std::string::npos || stream == std::string::npos || obj > stream) {
    *error = "cannot isolate JPX image dictionary";
    return {};
  }
  return pdf.substr(obj + 4, stream - (obj + 4));
}

bool ExtractInt(const std::string& dict, const std::string& key,
                uint32_t* out) {
  size_t p = dict.find(key);
  if (p == std::string::npos) {
    return false;
  }
  p += key.size();
  while (p < dict.size() &&
         std::isspace(static_cast<unsigned char>(dict[p]))) {
    ++p;
  }
  char* end = nullptr;
  long value = std::strtol(dict.c_str() + p, &end, 10);
  if (end == dict.c_str() + p || value < 0) {
    return false;
  }
  *out = static_cast<uint32_t>(value);
  return true;
}

uint32_t ExtractDisplayScale(const std::string& pdf) {
  size_t image_filter = pdf.find("/Filter /JPXDecode");
  if (image_filter == std::string::npos) {
    return 128;
  }
  size_t image_end = pdf.find("\nendstream", image_filter);
  if (image_end == std::string::npos) {
    return 128;
  }
  size_t stream = pdf.find("stream\n", image_end);
  if (stream == std::string::npos) {
    return 128;
  }
  stream += 7;
  size_t end = pdf.find("\nendstream", stream);
  if (end == std::string::npos) {
    return 128;
  }
  std::string content = pdf.substr(stream, end - stream);
  size_t q = content.find("q ");
  if (q == std::string::npos) {
    return 128;
  }
  char* parse_end = nullptr;
  long scale = std::strtol(content.c_str() + q + 2, &parse_end, 10);
  return scale > 0 ? static_cast<uint32_t>(scale) : 128;
}

bool ReadMarker(const std::string& jpx, size_t* pos, uint8_t* marker,
                size_t* payload, size_t* payload_len, std::string* error) {
  while (*pos < jpx.size() && U8(jpx, *pos) != 0xff) {
    ++*pos;
  }
  if (*pos + 2 > jpx.size()) {
    *error = "truncated marker";
    return false;
  }
  while (*pos < jpx.size() && U8(jpx, *pos) == 0xff) {
    ++*pos;
  }
  if (*pos >= jpx.size()) {
    *error = "truncated marker id";
    return false;
  }
  *marker = U8(jpx, (*pos)++);
  if ((*marker >= 0x30 && *marker <= 0x3f) || *marker == 0x4f ||
      *marker == 0x92 || *marker == 0x93 || *marker == 0xd9) {
    *payload = *pos;
    *payload_len = 0;
    return true;
  }
  if (*pos + 2 > jpx.size()) {
    *error = "truncated marker length";
    return false;
  }
  uint16_t len = U16(jpx, *pos);
  *pos += 2;
  if (len < 2 || *pos + len - 2 > jpx.size()) {
    *error = "bad marker length";
    return false;
  }
  *payload = *pos;
  *payload_len = len - 2;
  *pos += *payload_len;
  return true;
}

ParseResult ParseCodestream(const std::string& jpx) {
  ParseResult result;
  size_t pos = 0;
  while (pos < jpx.size()) {
    const size_t marker_start = pos;
    uint8_t marker = 0;
    size_t payload = 0;
    size_t payload_len = 0;
    if (!ReadMarker(jpx, &pos, &marker, &payload, &payload_len,
                    &result.error)) {
      return result;
    }

    switch (marker) {
      case 0x4f:
        result.saw_soc = true;
        break;
      case 0x51: {
        if (payload_len < 39) {
          result.error = "SIZ too short";
          return result;
        }
        result.saw_siz = true;
        result.doc.set_width(U32(jpx, payload + 2));
        result.doc.set_height(U32(jpx, payload + 6));
        result.doc.set_x_offset(U32(jpx, payload + 10));
        result.doc.set_y_offset(U32(jpx, payload + 14));
        result.doc.set_tile_width(U32(jpx, payload + 18));
        result.doc.set_tile_height(U32(jpx, payload + 22));
        result.doc.set_tile_x_offset(U32(jpx, payload + 26));
        result.doc.set_tile_y_offset(U32(jpx, payload + 30));
        uint32_t comps = U16(jpx, payload + 34);
        result.doc.set_components(comps);
        if (comps == 0 || payload_len < 36 + 3 * comps) {
          result.error = "SIZ component table truncated";
          return result;
        }
        result.doc.set_bits_per_component((U8(jpx, payload + 36) & 0x7f) + 1);
        result.doc.set_x_sample_separation(U8(jpx, payload + 37));
        result.doc.set_y_sample_separation(U8(jpx, payload + 38));
        break;
      }
      case 0x52:
        if (payload_len < 10) {
          result.error = "COD too short";
          return result;
        }
        result.saw_cod = true;
        result.doc.set_progression_order(U8(jpx, payload + 1));
        result.doc.set_layers(U16(jpx, payload + 2));
        result.doc.set_multi_component_transform(U8(jpx, payload + 4));
        result.doc.set_decomposition_levels(U8(jpx, payload + 5));
        result.doc.set_code_block_width_exp_minus2(U8(jpx, payload + 6));
        result.doc.set_code_block_height_exp_minus2(U8(jpx, payload + 7));
        result.doc.set_code_block_style(U8(jpx, payload + 8));
        result.doc.set_transform(U8(jpx, payload + 9));
        break;
      case 0x5c:
        if (payload_len < 2) {
          result.error = "QCD too short";
          return result;
        }
        result.saw_qcd = true;
        result.doc.clear_quant_steps();
        for (size_t i = 1; i < payload_len; ++i) {
          result.doc.add_quant_steps(U8(jpx, payload + i));
        }
        break;
      case 0x90:
        if (payload_len < 8) {
          result.error = "SOT too short";
          return result;
        }
        result.saw_sot = true;
        result.doc.set_tile_index(U16(jpx, payload));
        break;
      case 0x93: {
        result.saw_sod = true;
        size_t next_marker = pos;
        while (next_marker + 1 < jpx.size()) {
          if (U8(jpx, next_marker) == 0xff &&
              U8(jpx, next_marker + 1) != 0x00) {
            break;
          }
          next_marker += (U8(jpx, next_marker) == 0xff) ? 2 : 1;
        }
        result.doc.set_tile_part_payload_bytes(
            static_cast<uint32_t>(next_marker - pos));
        pos = next_marker;
        break;
      }
      case 0xd9:
        result.saw_eoc = true;
        result.doc.set_include_eoc(true);
        pos = jpx.size();
        break;
      default:
        result.error = "unexpected marker 0x" +
                       std::to_string(static_cast<unsigned>(marker));
        return result;
    }

    if (pos <= marker_start) {
      result.error = "parser made no progress";
      return result;
    }
  }

  if (!result.saw_eoc) {
    result.doc.set_include_eoc(false);
  }
  result.ok = result.saw_soc && result.saw_siz && result.saw_cod &&
              result.saw_qcd && result.saw_sot && result.saw_sod;
  if (!result.ok) {
    result.error = "missing required SOC/SIZ/COD/QCD/SOT/SOD markers";
  }
  return result;
}

ParseResult ParseSerializedPdf(const std::string& pdf) {
  ParseResult result;
  std::string error;
  std::string dict = ExtractJpxDict(pdf, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }

  uint32_t pdf_width = 0;
  uint32_t pdf_height = 0;
  if (!ExtractInt(dict, "/Width", &pdf_width) ||
      !ExtractInt(dict, "/Height", &pdf_height)) {
    result.error = "missing image width/height";
    return result;
  }

  std::string jpx = ExtractJpxStream(pdf, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  result = ParseCodestream(jpx);
  if (!result.ok) {
    return result;
  }
  result.doc.set_pdf_width(pdf_width);
  result.doc.set_pdf_height(pdf_height);
  result.doc.set_display_scale(ExtractDisplayScale(pdf));
  if (Contains(dict, "/ColorSpace /DeviceRGB")) {
    result.doc.set_pdf_color_space(pdf_jpxstream::JpxStreamDocument::DEVICE_RGB);
  } else if (Contains(dict, "/ColorSpace /DeviceCMYK")) {
    result.doc.set_pdf_color_space(
        pdf_jpxstream::JpxStreamDocument::DEVICE_CMYK);
  } else {
    result.doc.set_pdf_color_space(
        pdf_jpxstream::JpxStreamDocument::DEVICE_GRAY);
  }
  return result;
}

bool StructuralInvariants(const pdf_jpxstream::JpxStreamDocument& doc,
                          std::string* why) {
  if (doc.width() == 0 || doc.height() == 0 ||
      doc.tile_width() == 0 || doc.tile_height() == 0) {
    *why = "zero image/tile dimension";
    return false;
  }
  if (doc.x_offset() >= doc.width() || doc.y_offset() >= doc.height()) {
    *why = "image offset outside image";
    return false;
  }
  if (doc.tile_x_offset() > doc.x_offset() ||
      doc.tile_y_offset() > doc.y_offset()) {
    *why = "tile offset after image offset";
    return false;
  }
  if (doc.tile_width() + doc.tile_x_offset() <= doc.x_offset() ||
      doc.tile_height() + doc.tile_y_offset() <= doc.y_offset()) {
    *why = "tile grid does not cover image origin";
    return false;
  }
  if (doc.components() == 0 || doc.components() > 4) {
    *why = "component count outside supported fuzz range";
    return false;
  }
  if (doc.bits_per_component() == 0 || doc.bits_per_component() > 32 ||
      doc.x_sample_separation() == 0 || doc.y_sample_separation() == 0) {
    *why = "invalid component precision or sample separation";
    return false;
  }
  if (doc.progression_order() > 4 || doc.layers() == 0 ||
      doc.decomposition_levels() == 0 || doc.decomposition_levels() > 31 ||
      doc.code_block_width_exp_minus2() > 8 ||
      doc.code_block_height_exp_minus2() > 8) {
    *why = "invalid COD coding style";
    return false;
  }
  const uint32_t min_steps = 3 * doc.decomposition_levels() + 1;
  if (doc.quant_steps_size() < static_cast<int>(min_steps)) {
    *why = "too few QCD quant steps for xpdf 4.04";
    return false;
  }
  return true;
}

bool ContentAssertions(const std::string& pdf, std::string* why) {
  std::string error;
  std::string jpx = ExtractJpxStream(pdf, &error);
  if (!error.empty()) {
    *why = error;
    return false;
  }
  const std::string markers[] = {
      std::string("\xff\x4f", 2), std::string("\xff\x51", 2),
      std::string("\xff\x52", 2), std::string("\xff\x5c", 2),
      std::string("\xff\x90", 2), std::string("\xff\x93", 2),
      std::string("\xff\xd9", 2)};
  for (const auto& marker : markers) {
    if (!Contains(jpx, marker)) {
      *why = "missing expected JPX marker";
      return false;
    }
  }
  if (!Contains(pdf, "/Filter /JPXDecode") ||
      !Contains(pdf, "/Width 1 /Height 1") ||
      !Contains(pdf, "/ColorSpace /DeviceGray") ||
      !Contains(pdf, "/Im0 Do")) {
    *why = "PDF wrapper missing expected image/content tokens";
    return false;
  }
  return true;
}

bool XrefOk(const std::string& pdf, std::string* why) {
  size_t sx = pdf.rfind("startxref\n");
  if (sx == std::string::npos) {
    *why = "missing startxref";
    return false;
  }
  long off = std::atol(pdf.c_str() + sx + 10);
  if (off < 0 || static_cast<size_t>(off) + 5 > pdf.size() ||
      pdf.compare(static_cast<size_t>(off), 5, "xref\n") != 0) {
    *why = "startxref does not point to xref";
    return false;
  }
  return true;
}

std::string ShellQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

bool RunXpdfSmoke(const std::string& pdftoppm, const std::string& label,
                  const std::string& pdf, bool expect_vulnerable_crash) {
  char pdf_path[] = "/tmp/jpx_verify_pdf_XXXXXX";
  int fd = mkstemp(pdf_path);
  if (fd < 0) {
    std::fprintf(stderr, "FAIL: mkstemp failed for %s\n", label.c_str());
    return false;
  }
  (void)!write(fd, pdf.data(), pdf.size());
  close(fd);

  std::string out_base = std::string("/tmp/jpx_verify_") + label;
  std::string cmd = "env ASAN_OPTIONS=detect_leaks=0 timeout 10s " +
                    ShellQuote(pdftoppm) + " " + ShellQuote(pdf_path) + " " +
                    ShellQuote(out_base) + " 2>&1";
  FILE* pipe = popen(cmd.c_str(), "r");
  std::string output;
  char buf[512];
  if (pipe) {
    while (fgets(buf, sizeof(buf), pipe)) {
      output += buf;
    }
  }
  int status = pipe ? pclose(pipe) : -1;
  unlink(pdf_path);

  const bool exited_zero = status != -1 && WIFEXITED(status) &&
                           WEXITSTATUS(status) == 0;
  const bool saw_fixed_error =
      Contains(output, "Invalid tile size or sample separation in JPX stream");
  const bool saw_vuln_crash =
      Contains(output, "JPXStream::readTilePart") &&
      (Contains(output, "AddressSanitizer") ||
       Contains(output, "store to null pointer"));

  std::fprintf(stderr, "xpdf smoke %-8s exit=%s fixed_error=%d vuln_crash=%d\n",
               label.c_str(), exited_zero ? "0" : "nonzero",
               saw_fixed_error ? 1 : 0, saw_vuln_crash ? 1 : 0);

  if (expect_vulnerable_crash) {
    return saw_vuln_crash;
  }
  return exited_zero && saw_fixed_error;
}

void Check(bool condition, const char* what, int* failures) {
  if (condition) {
    std::fprintf(stderr, "PASS: %s\n", what);
  } else {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++*failures;
  }
}

}  // namespace

int main(int argc, char** argv) {
  int failures = 0;

  pdf_jpxstream::JpxStreamDocument seed;
  pdf_jpxstream::JpxStreamDocument canon =
      CanonicalizeJpxStreamDocument(seed);
  std::string pdf = SerializeJpxPdf(canon);
  std::string jpx = SerializeJpxCodestream(canon);

  ParseResult parsed_pdf = ParseSerializedPdf(pdf);
  Check(parsed_pdf.ok, "PDF wrapper parser accepts serialized JPX PDF",
        &failures);
  if (!parsed_pdf.ok) {
    std::fprintf(stderr, "  parser error: %s\n", parsed_pdf.error.c_str());
  }

  ParseResult parsed_jpx = ParseCodestream(jpx);
  Check(parsed_jpx.ok, "codestream parser accepts serialized JPX markers",
        &failures);

  if (parsed_pdf.ok) {
    pdf_jpxstream::JpxStreamDocument reparsed =
        CanonicalizeJpxStreamDocument(parsed_pdf.doc);
    Check(canon.SerializeAsString() == reparsed.SerializeAsString(),
          "semantic round-trip preserves canonical JpxStreamDocument",
          &failures);
  }

  std::string wire = canon.SerializeAsString();
  pdf_jpxstream::JpxStreamDocument deserialized;
  Check(deserialized.ParseFromString(wire),
        "protobuf deserializer accepts canonical message", &failures);
  std::string why;
  Check(StructuralInvariants(deserialized, &why),
        "deserialized message satisfies JPX structural invariants", &failures);
  if (!why.empty()) {
    std::fprintf(stderr, "  invariant detail: %s\n", why.c_str());
  }

  why.clear();
  Check(ContentAssertions(pdf, &why),
        "serialized PDF contains required JPX/PDF tokens", &failures);
  if (!why.empty()) {
    std::fprintf(stderr, "  content detail: %s\n", why.c_str());
  }

  why.clear();
  Check(XrefOk(pdf, &why), "PDF startxref points to xref table", &failures);
  if (!why.empty()) {
    std::fprintf(stderr, "  xref detail: %s\n", why.c_str());
  }

  if (argc >= 2) {
    Check(RunXpdfSmoke(argv[1], "4.04", pdf, false),
          "xpdf fixed-version smoke returns meaningful JPX syntax error",
          &failures);
  }
  if (argc >= 3) {
    Check(RunXpdfSmoke(argv[2], "4.03", pdf, true),
          "xpdf vulnerable-version smoke reaches JPXStream crash signature",
          &failures);
  }

  if (failures) {
    std::fprintf(stderr, "verify_jpx_stream: %d failure(s)\n", failures);
    return 1;
  }
  std::fprintf(stderr, "verify_jpx_stream: all checks passed\n");
  return 0;
}
