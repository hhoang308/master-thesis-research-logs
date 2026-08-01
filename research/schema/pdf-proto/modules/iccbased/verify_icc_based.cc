#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "modules/iccbased/icc_based.pb.h"
#include "modules/iccbased/icc_based_serializer.h"

namespace {

struct ParseResult {
  bool ok = false;
  std::string error;
  pdf_iccbased::IccBasedDocument doc;
};

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

bool ExtractInt(const std::string& dict, const std::string& key, int* out) {
  size_t p = dict.find(key);
  if (p == std::string::npos) {
    return false;
  }
  p += key.size();
  while (p < dict.size() &&
         (dict[p] == ' ' || dict[p] == '\t' || dict[p] == '\r' || dict[p] == '\n')) {
    ++p;
  }
  char* end = nullptr;
  long value = std::strtol(dict.c_str() + p, &end, 10);
  if (end == dict.c_str() + p) {
    return false;
  }
  *out = static_cast<int>(value);
  return true;
}

std::string ExtractObjectDict(const std::string& pdf, const std::string& anchor,
                              std::string* error) {
  size_t marker = pdf.find(anchor);
  if (marker == std::string::npos) {
    *error = "missing object anchor " + anchor;
    return {};
  }
  size_t stream = pdf.find("stream\n", marker);
  if (stream == std::string::npos) {
    *error = "missing stream after " + anchor;
    return {};
  }
  size_t obj = pdf.rfind("obj\n", marker);
  if (obj == std::string::npos || obj > stream) {
    *error = "cannot isolate dict for " + anchor;
    return {};
  }
  return pdf.substr(obj + 4, stream - (obj + 4));
}

std::string ExtractStreamAfter(const std::string& pdf, const std::string& anchor,
                               std::string* error) {
  size_t marker = pdf.find(anchor);
  if (marker == std::string::npos) {
    *error = "missing stream anchor " + anchor;
    return {};
  }
  size_t stream = pdf.find("stream\n", marker);
  if (stream == std::string::npos) {
    *error = "missing stream after " + anchor;
    return {};
  }
  stream += 7;
  size_t end = pdf.find("\nendstream", stream);
  if (end == std::string::npos) {
    *error = "missing endstream after " + anchor;
    return {};
  }
  return pdf.substr(stream, end - stream);
}

int ExtractDisplayScale(const std::string& pdf) {
  size_t contents_obj = pdf.find("6 0 obj\n");
  if (contents_obj == std::string::npos) {
    return 100;
  }
  std::string error;
  std::string stream = ExtractStreamAfter(pdf, "6 0 obj\n", &error);
  if (!error.empty()) {
    return 100;
  }
  size_t q = stream.find("q ");
  if (q == std::string::npos) {
    return 100;
  }
  char* end = nullptr;
  long value = std::strtol(stream.c_str() + q + 2, &end, 10);
  return value > 0 ? static_cast<int>(value) : 100;
}

ParseResult ParseSerializedPdf(const std::string& pdf) {
  ParseResult result;
  std::string error;

  std::string image_dict = ExtractObjectDict(pdf, "/Subtype /Image", &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  std::string image_data = ExtractStreamAfter(pdf, "/Subtype /Image", &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  std::string icc_dict = ExtractObjectDict(pdf, "/Alternate /", &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  std::string icc_profile = ExtractStreamAfter(pdf, "/Alternate /", &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }

  int width = 0;
  int height = 0;
  int bits = 0;
  int image_length = 0;
  int icc_length = 0;
  int n = 0;
  if (!ExtractInt(image_dict, "/Width", &width) ||
      !ExtractInt(image_dict, "/Height", &height) ||
      !ExtractInt(image_dict, "/BitsPerComponent", &bits) ||
      !ExtractInt(image_dict, "/Length", &image_length) ||
      !ExtractInt(icc_dict, "/Length", &icc_length) ||
      !ExtractInt(icc_dict, "/N", &n)) {
    result.error = "missing required image/ICC integer field";
    return result;
  }

  result.doc.set_width(width);
  result.doc.set_height(height);
  result.doc.set_bits_per_component(bits);
  result.doc.set_icc_components(n);
  result.doc.set_display_scale(ExtractDisplayScale(pdf));
  result.doc.set_image_data(image_data);
  result.doc.set_icc_profile(icc_profile);
  result.doc.set_image_length_delta(
      image_length - static_cast<int>(image_data.size()));
  result.doc.set_icc_length_delta(
      icc_length - static_cast<int>(icc_profile.size()));

  if (Contains(icc_dict, "/Alternate /DeviceCMYK")) {
    result.doc.set_alternate(pdf_iccbased::IccBasedDocument::DEVICE_CMYK);
  } else if (Contains(icc_dict, "/Alternate /DeviceRGB")) {
    result.doc.set_alternate(pdf_iccbased::IccBasedDocument::DEVICE_RGB);
  } else if (Contains(icc_dict, "/Alternate /DeviceGray")) {
    result.doc.set_alternate(pdf_iccbased::IccBasedDocument::DEVICE_GRAY);
  } else {
    result.error = "missing supported /Alternate color space";
    return result;
  }

  size_t range_key = icc_dict.find("/Range [");
  if (range_key != std::string::npos) {
    size_t start = icc_dict.find('[', range_key);
    size_t end = icc_dict.find(']', start);
    if (start == std::string::npos || end == std::string::npos || end < start) {
      result.error = "malformed /Range array";
      return result;
    }
    std::istringstream iss(icc_dict.substr(start + 1, end - start - 1));
    int value = 0;
    while (iss >> value) {
      result.doc.add_range(value);
    }
  }

  result.ok = true;
  return result;
}

bool StructuralInvariants(const pdf_iccbased::IccBasedDocument& doc,
                          std::string* why) {
  if (doc.width() == 0 || doc.height() == 0) {
    *why = "zero image dimension";
    return false;
  }
  if (doc.bits_per_component() == 0 || doc.bits_per_component() > 16) {
    *why = "bits_per_component outside serializer range";
    return false;
  }
  if (doc.icc_components() > 4) {
    *why = "icc_components outside serializer range";
    return false;
  }
  if (doc.display_scale() == 0) {
    *why = "display_scale must be non-zero";
    return false;
  }
  if (doc.image_data().empty()) {
    *why = "image_data is empty after deserialization";
    return false;
  }
  if (doc.range_size() != 0 &&
      doc.range_size() != static_cast<int>(2 * doc.icc_components())) {
    *why = "/Range length does not match 2*N";
    return false;
  }
  return true;
}

bool ContentAssertions(const std::string& pdf,
                       const pdf_iccbased::IccBasedDocument& canon,
                       std::string* why) {
  const std::string alt = canon.alternate() ==
                                  pdf_iccbased::IccBasedDocument::DEVICE_CMYK
                              ? "/Alternate /DeviceCMYK"
                              : canon.alternate() ==
                                        pdf_iccbased::IccBasedDocument::DEVICE_RGB
                                    ? "/Alternate /DeviceRGB"
                                    : "/Alternate /DeviceGray";
  if (!Contains(pdf, "%PDF-1.4") ||
      !Contains(pdf, "/Subtype /Image") ||
      !Contains(pdf, "/ColorSpace [/ICCBased 4 0 R]") ||
      !Contains(pdf, alt) ||
      !Contains(pdf, "/N " + std::to_string(canon.icc_components())) ||
      !Contains(pdf, "/Width " + std::to_string(canon.width())) ||
      !Contains(pdf, "/Height " + std::to_string(canon.height())) ||
      !Contains(pdf, "/BitsPerComponent " +
                         std::to_string(canon.bits_per_component())) ||
      !Contains(pdf, "/Im0 Do")) {
    *why = "serialized PDF missing expected ICCBased/image tokens";
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

bool CommandExists(const char* cmd) {
  std::string probe = "command -v ";
  probe += cmd;
  probe += " >/dev/null 2>&1";
  return std::system(probe.c_str()) == 0;
}

int RunExit(const std::string& cmd) {
  int rc = std::system((cmd + " >/dev/null 2>&1").c_str());
  if (rc == -1) {
    return -1;
  }
  return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

bool WriteTemp(const std::string& data, std::string* path) {
  char tmp[] = "/tmp/iccbased_verify_XXXXXX";
  int fd = mkstemp(tmp);
  if (fd < 0) {
    return false;
  }
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
                  const std::string& pdf, bool expect_vulnerable) {
  std::string pdf_path;
  if (!WriteTemp(pdf, &pdf_path)) {
    std::fprintf(stderr, "FAIL: cannot write temp PDF for %s\n", label.c_str());
    return false;
  }

  std::string out_base = std::string("/tmp/iccbased_verify_") + label;
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
  unlink(pdf_path.c_str());

  const bool exited_zero =
      status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  const bool saw_divzero =
      Contains(output, "division by zero") ||
      Contains(output, "ImageStream::ImageStream");
  const bool saw_fixed_error =
      Contains(output,
               "Number of components in ICCBased color space doesn't match alternate color space");

  std::fprintf(stderr, "xpdf smoke %-8s exit=%s divzero=%d fixed_error=%d\n",
               label.c_str(), exited_zero ? "0" : "nonzero",
               saw_divzero ? 1 : 0, saw_fixed_error ? 1 : 0);

  if (expect_vulnerable) {
    return saw_divzero;
  }
  return !saw_divzero && (exited_zero || saw_fixed_error);
}

void Check(bool condition, const char* what, int* failures) {
  if (condition) {
    std::fprintf(stderr, "PASS: %s\n", what);
  } else {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++*failures;
  }
}

bool RunCase(const char* name, const pdf_iccbased::IccBasedDocument& doc,
             bool expect_cve_shape) {
  std::fprintf(stderr, "\n=== %s ===\n", name);
  const pdf_iccbased::IccBasedDocument canon = CanonicalizeIccBasedDocument(doc);
  const std::string pdf = SerializeIccBasedPdf(doc);

  ParseResult parsed = ParseSerializedPdf(pdf);
  if (!parsed.ok) {
    std::fprintf(stderr, "FAIL: parser rejected serialized PDF: %s\n",
                 parsed.error.c_str());
    return false;
  }

  std::string why;
  if (!StructuralInvariants(parsed.doc, &why)) {
    std::fprintf(stderr, "FAIL: deserialized structural invariant: %s\n",
                 why.c_str());
    return false;
  }

  const pdf_iccbased::IccBasedDocument reparsed =
      CanonicalizeIccBasedDocument(parsed.doc);
  if (canon.SerializeAsString() != reparsed.SerializeAsString()) {
    std::fprintf(stderr, "FAIL: semantic round-trip mismatch\n");
    std::fprintf(stderr, "expected:\n%s\nactual:\n%s\n",
                 canon.DebugString().c_str(), reparsed.DebugString().c_str());
    return false;
  }

  if (!ContentAssertions(pdf, canon, &why)) {
    std::fprintf(stderr, "FAIL: content assertions: %s\n", why.c_str());
    return false;
  }
  if (!XrefOk(pdf, &why)) {
    std::fprintf(stderr, "FAIL: xref invariant: %s\n", why.c_str());
    return false;
  }

  if (expect_cve_shape &&
      !(canon.icc_components() == 0 &&
        canon.alternate() == pdf_iccbased::IccBasedDocument::DEVICE_GRAY)) {
    std::fprintf(stderr, "FAIL: expected CVE /N 0 + DeviceGray shape missing\n");
    return false;
  }

  if (CommandExists("qpdf")) {
    std::string path;
    if (!WriteTemp(pdf, &path)) {
      std::fprintf(stderr, "FAIL: cannot write temp PDF for qpdf\n");
      return false;
    }
    const int q = RunExit(std::string("qpdf --check ") + path);
    unlink(path.c_str());
    if (!(q == 0 || q == 3)) {
      std::fprintf(stderr, "FAIL: qpdf returned %d\n", q);
      return false;
    }
    std::fprintf(stderr, "qpdf parser: accepted skeleton (exit %d)\n", q);
  } else {
    std::fprintf(stderr, "qpdf parser: skipped (not installed)\n");
  }

  std::fprintf(stderr, "PASS: %s (%zu PDF bytes)\n", name, pdf.size());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  int failures = 0;

  {
    pdf_iccbased::IccBasedDocument doc;
    failures += RunCase("default-cve-2023-2662-shape", doc, true) ? 0 : 1;
  }

  {
    pdf_iccbased::IccBasedDocument doc;
    doc.set_width(2);
    doc.set_height(3);
    doc.set_bits_per_component(8);
    doc.set_icc_components(3);
    doc.set_alternate(pdf_iccbased::IccBasedDocument::DEVICE_RGB);
    doc.set_display_scale(144);
    doc.set_image_data(std::string("\x00\x7f\xff", 3));
    doc.set_icc_profile("rgb");
    doc.add_range(0);
    doc.add_range(1);
    doc.add_range(0);
    doc.add_range(1);
    doc.add_range(0);
    doc.add_range(1);
    failures += RunCase("rgb-three-component-shape", doc, false) ? 0 : 1;
  }

  {
    pdf_iccbased::IccBasedDocument doc;
    doc.set_width(0);
    doc.set_height(9999);
    doc.set_bits_per_component(99);
    doc.set_icc_components(99);
    doc.set_alternate(pdf_iccbased::IccBasedDocument::DEVICE_CMYK);
    doc.set_display_scale(0);
    doc.set_image_length_delta(7);
    doc.set_icc_length_delta(-3);
    doc.add_range(-100);
    doc.add_range(100);
    doc.add_range(5);
    failures += RunCase("canonicalizes-mutator-ish-fields", doc, false) ? 0 : 1;
  }

  if (argc >= 3) {
    pdf_iccbased::IccBasedDocument doc;
    const std::string pdf = SerializeIccBasedPdf(doc);
    Check(RunXpdfSmoke(argv[1], "vuln", pdf, true),
          "xpdf 4.04 ASan smoke reproduces the divide-by-zero path",
          &failures);
    Check(RunXpdfSmoke(argv[2], "fixed", pdf, false),
          "xpdf 4.05 ASan smoke reports the fixed ICCBased mismatch path",
          &failures);
  } else {
    std::fprintf(stderr,
                 "\nsmoke: skipped (pass <xpdf-4.04-pdftoppm> <xpdf-4.05-pdftoppm> to enable)\n");
  }

  google::protobuf::ShutdownProtobufLibrary();
  if (failures) {
    std::fprintf(stderr, "\nverify_icc_based: %d failure(s)\n", failures);
    return 1;
  }
  std::fprintf(stderr, "\nverify_icc_based: all checks passed\n");
  return 0;
}
