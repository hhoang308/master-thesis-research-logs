#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "modules/dctstream/dct_stream.pb.h"
#include "modules/dctstream/dct_stream_serializer.h"

namespace {

struct ParseResult {
  bool ok = false;
  std::string error;
  pdf_dctstream::DctStreamDocument doc;
  int sos_count = 0;
  bool saw_dqt = false;
  bool saw_dht = false;
  bool saw_sof0 = false;
  bool saw_eoi = false;
};

uint8_t U8(const std::string& s, size_t pos) {
  return pos < s.size() ? static_cast<uint8_t>(s[pos]) : 0;
}

uint16_t U16(const std::string& s, size_t pos) {
  return static_cast<uint16_t>((U8(s, pos) << 8) | U8(s, pos + 1));
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

bool ExtractInt(const std::string& dict, const std::string& key, uint32_t* out) {
  size_t p = dict.find(key);
  if (p == std::string::npos) {
    return false;
  }
  p += key.size();
  while (p < dict.size() && std::isspace(static_cast<unsigned char>(dict[p]))) {
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

std::string ExtractDctStream(const std::string& pdf, std::string* error) {
  size_t filter = pdf.find("/Filter /DCTDecode");
  if (filter == std::string::npos) {
    *error = "missing /Filter /DCTDecode";
    return {};
  }
  size_t stream = pdf.find("stream\n", filter);
  if (stream == std::string::npos) {
    *error = "DCT image object has no stream";
    return {};
  }
  stream += 7;
  size_t end = pdf.find("\nendstream", stream);
  if (end == std::string::npos) {
    *error = "DCT image stream has no endstream";
    return {};
  }
  return pdf.substr(stream, end - stream);
}

std::string ExtractDctDict(const std::string& pdf, std::string* error) {
  size_t filter = pdf.find("/Filter /DCTDecode");
  if (filter == std::string::npos) {
    *error = "missing /Filter /DCTDecode";
    return {};
  }
  size_t obj = pdf.rfind("obj\n", filter);
  size_t stream = pdf.find("stream\n", filter);
  if (obj == std::string::npos || stream == std::string::npos || obj > stream) {
    *error = "cannot isolate DCT image dictionary";
    return {};
  }
  return pdf.substr(obj + 4, stream - (obj + 4));
}

uint32_t ExtractDisplayScale(const std::string& pdf) {
  size_t image_filter = pdf.find("/Filter /DCTDecode");
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

ParseResult ParseSerializedPdf(const std::string& pdf) {
  ParseResult result;
  std::string error;
  std::string dict = ExtractDctDict(pdf, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }

  uint32_t width = 0;
  uint32_t height = 0;
  if (!ExtractInt(dict, "/Width", &width) ||
      !ExtractInt(dict, "/Height", &height)) {
    result.error = "missing image width/height";
    return result;
  }

  std::string jpeg = ExtractDctStream(pdf, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  if (jpeg.size() < 4 || U8(jpeg, 0) != 0xff || U8(jpeg, 1) != 0xd8) {
    result.error = "JPEG does not start with SOI";
    return result;
  }

  result.doc.set_width(width);
  result.doc.set_height(height);
  if (Contains(dict, "/ColorSpace /DeviceGray")) {
    result.doc.set_pdf_color_space(
        pdf_dctstream::DctStreamDocument::DEVICE_GRAY);
  } else if (Contains(dict, "/ColorSpace /DeviceCMYK")) {
    result.doc.set_pdf_color_space(
        pdf_dctstream::DctStreamDocument::DEVICE_CMYK);
  } else {
    result.doc.set_pdf_color_space(
        pdf_dctstream::DctStreamDocument::DEVICE_RGB);
  }
  result.doc.set_display_scale(ExtractDisplayScale(pdf));

  size_t p = 2;
  while (p + 1 < jpeg.size()) {
    if (U8(jpeg, p) != 0xff) {
      result.error = "expected JPEG marker";
      return result;
    }
    while (p < jpeg.size() && U8(jpeg, p) == 0xff) {
      ++p;
    }
    if (p >= jpeg.size()) {
      result.error = "truncated JPEG marker";
      return result;
    }
    uint8_t marker = U8(jpeg, p++);
    if (marker == 0xd9) {
      result.saw_eoi = true;
      break;
    }
    if (p + 2 > jpeg.size()) {
      result.error = "truncated JPEG segment length";
      return result;
    }
    uint16_t len = U16(jpeg, p);
    p += 2;
    if (len < 2 || p + len - 2 > jpeg.size()) {
      result.error = "bad JPEG segment length";
      return result;
    }
    const size_t payload = p;
    const size_t payload_len = len - 2;

    pdf_dctstream::DctStreamDocument::Scan* parsed_scan = nullptr;
    if (marker == 0xdb) {
      result.saw_dqt = true;
    } else if (marker == 0xc4) {
      result.saw_dht = true;
    } else if (marker == 0xc0) {
      if (payload_len < 6) {
        result.error = "SOF0 too short";
        return result;
      }
      result.saw_sof0 = true;
      result.doc.set_height(U16(jpeg, payload + 1));
      result.doc.set_width(U16(jpeg, payload + 3));
      result.doc.set_frame_components(U8(jpeg, payload + 5));
    } else if (marker == 0xda) {
      if (payload_len < 4) {
        result.error = "SOS too short";
        return result;
      }
      uint32_t n = U8(jpeg, payload);
      if (payload_len != 2 * n + 4) {
        result.error = "SOS length does not match component count";
        return result;
      }
      parsed_scan = result.doc.add_scans();
      for (uint32_t i = 0; i < n; ++i) {
        parsed_scan->add_component_ids(U8(jpeg, payload + 1 + 2 * i));
      }
      parsed_scan->set_entropy_padding_bytes(0);
      result.sos_count++;
    }

    p += payload_len;

    if (marker == 0xda) {
      size_t entropy_start = p;
      while (p + 1 < jpeg.size()) {
        if (U8(jpeg, p) == 0xff && U8(jpeg, p + 1) != 0x00) {
          break;
        }
        p += (U8(jpeg, p) == 0xff) ? 2 : 1;
      }
      if (parsed_scan) {
        const uint32_t mcu_cols = (result.doc.width() + 7) / 8;
        const uint32_t mcu_rows = (result.doc.height() + 7) / 8;
        const uint32_t mcu_count = std::max<uint32_t>(1, mcu_cols * mcu_rows);
        const uint32_t data_units =
            mcu_count * static_cast<uint32_t>(parsed_scan->component_ids_size());
        const uint32_t min_bytes =
            std::max<uint32_t>(1, (data_units * 2 + 7) / 8);
        const uint32_t entropy_len = static_cast<uint32_t>(p - entropy_start);
        parsed_scan->set_entropy_padding_bytes(
            entropy_len > min_bytes ? entropy_len - min_bytes : 0);
      }
    }
  }

  if (!result.saw_dqt || !result.saw_dht || !result.saw_sof0 ||
      !result.saw_eoi || result.sos_count == 0) {
    result.error = "JPEG missing required DQT/DHT/SOF0/SOS/EOI markers";
    return result;
  }
  result.ok = true;
  return result;
}

bool ScanIdsValid(const pdf_dctstream::DctStreamDocument& doc,
                  std::string* why) {
  if (doc.width() == 0 || doc.height() == 0 ||
      doc.frame_components() == 0 || doc.frame_components() > 4) {
    *why = "frame dimensions/components out of range";
    return false;
  }
  if (doc.scans_size() == 0) {
    *why = "no scans";
    return false;
  }
  for (const auto& scan : doc.scans()) {
    std::set<uint32_t> seen;
    if (scan.component_ids_size() == 0 ||
        scan.component_ids_size() > static_cast<int>(doc.frame_components())) {
      *why = "scan component count out of range";
      return false;
    }
    for (uint32_t id : scan.component_ids()) {
      if (id < 1 || id > doc.frame_components()) {
        *why = "component ID outside frame";
        return false;
      }
      if (!seen.insert(id).second) {
        *why = "duplicate component ID in scan";
        return false;
      }
    }
  }
  return true;
}

bool XrefOk(const std::string& s, std::string* why) {
  size_t sx = s.rfind("startxref\n");
  if (sx == std::string::npos) {
    *why = "no startxref";
    return false;
  }
  long off = std::atol(s.c_str() + sx + 10);
  if (off < 0 || static_cast<size_t>(off) + 5 > s.size() ||
      s.compare(static_cast<size_t>(off), 5, "xref\n") != 0) {
    *why = "startxref does not point at xref";
    return false;
  }
  return true;
}

bool CommandExists(const char* cmd) {
  std::string probe = "command -v ";
  probe += cmd;
  probe += " >/dev/null 2>&1";
  return std::system(probe.c_str()) == 0;
}

int RunExit(const std::string& cmd) {
  int rc = std::system((cmd + " >/dev/null 2>&1").c_str());
  if (rc == -1) return -1;
  return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

bool WriteTemp(const std::string& data, std::string* path) {
  char tmp[] = "/tmp/dctstream_verify_XXXXXX";
  int fd = mkstemp(tmp);
  if (fd < 0) return false;
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = write(fd, data.data() + off, data.size() - off);
    if (n <= 0) {
      close(fd);
      unlink(tmp);
      return false;
    }
    off += static_cast<size_t>(n);
  }
  close(fd);
  *path = tmp;
  return true;
}

bool RunCase(const char* name, const pdf_dctstream::DctStreamDocument& doc,
             bool expect_cve_shape) {
  std::fprintf(stderr, "\n=== %s ===\n", name);
  pdf_dctstream::DctStreamDocument canon =
      CanonicalizeDctStreamDocument(doc);
  std::string pdf = SerializeDctStreamPdf(doc);

  for (const char* marker :
       {"%PDF-1.4", "/Subtype /Image", "/DCTDecode", "/Width", "/Height"}) {
    if (!Contains(pdf, marker)) {
      std::fprintf(stderr, "FAIL: serialized PDF missing marker %s\n", marker);
      return false;
    }
  }

  std::string jpeg_error;
  std::string jpeg = ExtractDctStream(pdf, &jpeg_error);
  if (!jpeg_error.empty()) {
    std::fprintf(stderr, "FAIL: %s\n", jpeg_error.c_str());
    return false;
  }
  if (!Contains(jpeg, std::string("\xff\xc0", 2)) ||
      !Contains(jpeg, std::string("\xff\xda", 2)) ||
      !Contains(jpeg, std::string("\xff\xd9", 2))) {
    std::fprintf(stderr, "FAIL: JPEG marker content assertions failed\n");
    return false;
  }

  ParseResult parsed = ParseSerializedPdf(pdf);
  if (!parsed.ok) {
    std::fprintf(stderr, "FAIL: parser rejected serialized output: %s\n",
                 parsed.error.c_str());
    return false;
  }
  std::string why;
  if (!ScanIdsValid(parsed.doc, &why)) {
    std::fprintf(stderr, "FAIL: deserialized field invariant: %s\n",
                 why.c_str());
    return false;
  }

  pdf_dctstream::DctStreamDocument parsed_canon =
      CanonicalizeDctStreamDocument(parsed.doc);
  if (canon.SerializeAsString() != parsed_canon.SerializeAsString()) {
    std::fprintf(stderr, "FAIL: semantic round-trip mismatch\n");
    std::fprintf(stderr, "expected:\n%s\nactual:\n%s\n",
                 canon.DebugString().c_str(), parsed_canon.DebugString().c_str());
    return false;
  }

  if (expect_cve_shape) {
    if (canon.scans_size() < 2 ||
        canon.scans(0).component_ids_size() != 1 ||
        canon.scans(1).component_ids_size() !=
            static_cast<int>(canon.frame_components())) {
      std::fprintf(stderr, "FAIL: expected CVE scan-mode shape not present\n");
      return false;
    }
  }

  if (!XrefOk(pdf, &why)) {
    std::fprintf(stderr, "FAIL: xref invariant: %s\n", why.c_str());
    return false;
  }

  if (CommandExists("qpdf")) {
    std::string path;
    if (!WriteTemp(pdf, &path)) {
      std::fprintf(stderr, "FAIL: cannot write temp PDF for qpdf\n");
      return false;
    }
    int q = RunExit(std::string("qpdf --check ") + path);
    unlink(path.c_str());
    if (!(q == 0 || q == 3)) {
      std::fprintf(stderr, "FAIL: qpdf returned %d\n", q);
      return false;
    }
    std::fprintf(stderr, "qpdf parser: accepted skeleton (exit %d)\n", q);
  } else {
    std::fprintf(stderr, "qpdf parser: skipped (not installed)\n");
  }

  std::fprintf(stderr, "PASS: %s (%zu PDF bytes, %zu JPEG bytes, %d SOS)\n",
               name, pdf.size(), jpeg.size(), parsed.sos_count);
  return true;
}

}  // namespace

int main() {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  int failures = 0;

  {
    pdf_dctstream::DctStreamDocument doc;
    failures += RunCase("default-cve-2022-24106-shape", doc,
                        true) ? 0 : 1;
  }

  {
    pdf_dctstream::DctStreamDocument doc;
    doc.set_width(8);
    doc.set_height(8);
    doc.set_frame_components(3);
    auto* scan = doc.add_scans();
    scan->add_component_ids(1);
    scan->add_component_ids(2);
    scan->add_component_ids(3);
    failures += RunCase("single-interleaved-scan", doc, false) ? 0 : 1;
  }

  {
    pdf_dctstream::DctStreamDocument doc;
    doc.set_width(0);
    doc.set_height(9999);
    doc.set_frame_components(99);
    doc.set_pdf_color_space(pdf_dctstream::DctStreamDocument::DEVICE_CMYK);
    auto* scan = doc.add_scans();
    scan->add_component_ids(0);
    scan->add_component_ids(99);
    scan->add_component_ids(99);
    scan->set_entropy_padding_bytes(999);
    failures += RunCase("canonicalizes-mutator-ish-fields", doc, false) ? 0 : 1;
  }

  google::protobuf::ShutdownProtobufLibrary();
  if (failures) {
    std::fprintf(stderr, "\nverify_dct_stream: %d failure(s)\n", failures);
    return 1;
  }
  std::fprintf(stderr, "\nverify_dct_stream: all checks passed\n");
  return 0;
}
