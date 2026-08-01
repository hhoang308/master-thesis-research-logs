#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "modules/textpagesize/text_page_size.pb.h"
#include "modules/textpagesize/text_page_size_serializer.h"

namespace {

constexpr char kDefaultHeaderMagic[] = "%iDF";
constexpr char kDefaultPageWidth[] = "612.0000";
constexpr char kDefaultPageHeight[] = "79299999999999999999.0000";
constexpr char kMediaBoxPrefix[] = "/MediaBox [0 0 ";
constexpr char kPageAnchor[] = "/Type /Page";
constexpr char kContentsAnchor[] = "/Contents 7 0 R";
constexpr char kFlateTailAnchor[] = "/Filter /FlateDecode";

struct ParseResult {
  bool ok = false;
  std::string error;
  pdf_textpagesize::TextPageSizeDocument doc;
};

struct CommandResult {
  int status = -1;
  std::string output;
};

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
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

bool CommandExists(const char* cmd) {
  std::string probe = "command -v ";
  probe += cmd;
  probe += " >/dev/null 2>&1";
  return std::system(probe.c_str()) == 0;
}

CommandResult RunCommandCapture(const std::string& cmd) {
  CommandResult result;
  FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
  if (!pipe) {
    return result;
  }
  char buf[512];
  while (fgets(buf, sizeof(buf), pipe)) {
    result.output += buf;
  }
  result.status = pclose(pipe);
  return result;
}

bool WriteTemp(const std::string& data, std::string* path) {
  char tmp[] = "/tmp/textpagesize_verify_XXXXXX";
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

bool ParsePositiveFinite(const std::string& token, double* out) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  const double value = std::strtod(token.c_str(), &end);
  if (end != token.c_str() + token.size() || !std::isfinite(value) ||
      value <= 0.0) {
    return false;
  }
  *out = value;
  return true;
}

ParseResult ParseSerializedPdf(const std::string& pdf) {
  ParseResult result;

  size_t newline = pdf.find('\n');
  if (newline == std::string::npos || newline < 4) {
    result.error = "missing header newline";
    return result;
  }

  const size_t media_box = pdf.find(kMediaBoxPrefix);
  if (media_box == std::string::npos) {
    result.error = "missing oversized /MediaBox anchor";
    return result;
  }
  size_t pos = media_box + std::string(kMediaBoxPrefix).size();
  const size_t width_end = pdf.find(' ', pos);
  if (width_end == std::string::npos) {
    result.error = "missing /MediaBox width terminator";
    return result;
  }
  const size_t height_end = pdf.find(']', width_end + 1);
  if (height_end == std::string::npos) {
    result.error = "missing /MediaBox height terminator";
    return result;
  }

  result.doc.set_header_magic(pdf.substr(0, 4));
  result.doc.set_page_width_decimal(pdf.substr(pos, width_end - pos));
  result.doc.set_page_height_decimal(
      pdf.substr(width_end + 1, height_end - (width_end + 1)));
  result.doc.set_keep_flate_tail(Contains(pdf, kFlateTailAnchor));
  result.ok = true;
  return result;
}

bool StructuralInvariants(const pdf_textpagesize::TextPageSizeDocument& doc,
                          std::string* why) {
  if (doc.header_magic().size() != 4 || doc.header_magic()[0] != '%') {
    *why = "header_magic must be four printable bytes starting with '%'";
    return false;
  }
  double width = 0.0;
  double height = 0.0;
  if (!ParsePositiveFinite(doc.page_width_decimal(), &width)) {
    *why = "page_width_decimal is not a positive finite decimal";
    return false;
  }
  if (!ParsePositiveFinite(doc.page_height_decimal(), &height)) {
    *why = "page_height_decimal is not a positive finite decimal";
    return false;
  }
  if (height <= width) {
    *why = "page_height_decimal should dominate width for this trigger family";
    return false;
  }
  return true;
}

bool ContentAssertions(const std::string& pdf,
                       const pdf_textpagesize::TextPageSizeDocument& canon,
                       std::string* why) {
  const std::string media_box = std::string(kMediaBoxPrefix) +
                                canon.page_width_decimal() + " " +
                                canon.page_height_decimal() + "]";
  if (!Contains(pdf, canon.header_magic()) ||
      !Contains(pdf, kPageAnchor) ||
      !Contains(pdf, kContentsAnchor) ||
      !Contains(pdf, media_box)) {
    *why = "serialized damaged PDF is missing required CVE-2023-3044 tokens";
    return false;
  }
  if (canon.keep_flate_tail()) {
    if (!Contains(pdf, kFlateTailAnchor) || !Contains(pdf, "startx>")) {
      *why = "public fuzzed tail markers are missing";
      return false;
    }
  } else if (Contains(pdf, kFlateTailAnchor)) {
    *why = "keep_flate_tail=false but /Filter /FlateDecode still present";
    return false;
  }
  return true;
}

bool MeaningfulQpdfOutcome(const std::string& pdf, std::string* why) {
  if (!CommandExists("qpdf")) {
    *why = "qpdf not installed";
    return true;
  }

  std::string path;
  if (!WriteTemp(pdf, &path)) {
    *why = "could not create temp PDF for qpdf";
    return false;
  }
  CommandResult qpdf =
      RunCommandCapture(std::string("qpdf --check ") + ShellQuote(path));
  unlink(path.c_str());

  if (qpdf.status == -1) {
    *why = "qpdf invocation failed";
    return false;
  }
  if (WIFEXITED(qpdf.status) && WEXITSTATUS(qpdf.status) == 0) {
    *why = "qpdf accepted the file";
    return true;
  }

  const bool meaningful =
      Contains(qpdf.output, "PDF header") ||
      Contains(qpdf.output, "file is damaged") ||
      Contains(qpdf.output, "startxref") ||
      Contains(qpdf.output, "cross-reference table") ||
      Contains(qpdf.output, "stream dictionary") ||
      Contains(qpdf.output, "trailer");
  if (!meaningful) {
    *why = "qpdf failed without a meaningful damaged-PDF diagnostic";
    return false;
  }
  *why = "qpdf reported meaningful damaged-PDF diagnostics";
  return true;
}

bool RunPdftotextSmoke(const std::string& pdftotext, const std::string& label,
                       const std::string& pdf, bool expect_release_crash,
                       bool expect_asan_crash) {
  std::string pdf_path;
  if (!WriteTemp(pdf, &pdf_path)) {
    std::fprintf(stderr, "FAIL: cannot write temp PDF for %s\n", label.c_str());
    return false;
  }

  std::string out_path = std::string("/tmp/textpagesize_smoke_") + label + ".txt";
  std::string cmd = "timeout 10s " + ShellQuote(pdftotext) + " " +
                    ShellQuote(pdf_path) + " " + ShellQuote(out_path);
  if (Contains(label, "asan")) {
    cmd = "env ASAN_OPTIONS=detect_leaks=0 " + cmd;
  }
  CommandResult run = RunCommandCapture(cmd);
  unlink(pdf_path.c_str());

  const int exit_code =
      run.status != -1 && WIFEXITED(run.status) ? WEXITSTATUS(run.status) : -1;
  const bool saw_asan =
      Contains(run.output, "AddressSanitizer") &&
      Contains(run.output, "TextLine::TextLine");
  const bool saw_path =
      Contains(run.output, "TextPage::writeReadingOrder") ||
      Contains(run.output, "TextOutputDev::endPage");
  const bool saw_syntax =
      Contains(run.output, "Syntax Error") || Contains(run.output, "Syntax Warning");

  std::fprintf(stderr,
               "pdftotext smoke %-12s exit=%d asan=%d path=%d syntax=%d\n",
               label.c_str(), exit_code, saw_asan ? 1 : 0, saw_path ? 1 : 0,
               saw_syntax ? 1 : 0);

  if (expect_release_crash) {
    return exit_code == 139 || Contains(run.output, "Segmentation fault");
  }
  if (expect_asan_crash) {
    return saw_asan && saw_path;
  }
  return exit_code == 0 && !saw_asan && saw_syntax;
}

void Check(bool condition, const char* what, int* failures) {
  if (condition) {
    std::fprintf(stderr, "PASS: %s\n", what);
  } else {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++*failures;
  }
}

bool RunCase(const char* name, const pdf_textpagesize::TextPageSizeDocument& doc,
             bool expect_exact_public_shape) {
  std::fprintf(stderr, "\n=== %s ===\n", name);
  const pdf_textpagesize::TextPageSizeDocument canon =
      CanonicalizeTextPageSizeDocument(doc);
  const std::string pdf = SerializeTextPageSizePdf(doc);

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

  const pdf_textpagesize::TextPageSizeDocument reparsed =
      CanonicalizeTextPageSizeDocument(parsed.doc);
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

  if (!MeaningfulQpdfOutcome(pdf, &why)) {
    std::fprintf(stderr, "FAIL: qpdf sanity: %s\n", why.c_str());
    return false;
  }
  std::fprintf(stderr, "qpdf sanity: %s\n", why.c_str());

  if (expect_exact_public_shape) {
    if (canon.header_magic() != kDefaultHeaderMagic ||
        canon.page_width_decimal() != kDefaultPageWidth ||
        canon.page_height_decimal() != kDefaultPageHeight ||
        !canon.keep_flate_tail()) {
      std::fprintf(stderr, "FAIL: default CVE-2023-3044 trigger shape changed\n");
      return false;
    }
    if (pdf.size() != 2521) {
      std::fprintf(stderr, "FAIL: public trigger size drifted (%zu bytes)\n",
                   pdf.size());
      return false;
    }
  }

  std::fprintf(stderr, "PASS: %s (%zu PDF bytes)\n", name, pdf.size());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  int failures = 0;

  {
    pdf_textpagesize::TextPageSizeDocument doc;
    failures += RunCase("default-public-cve-2023-3044-shape", doc, true) ? 0 : 1;
  }

  {
    pdf_textpagesize::TextPageSizeDocument doc;
    doc.set_header_magic("%PDF");
    doc.set_page_width_decimal("595");
    doc.set_page_height_decimal("100000001.5");
    failures += RunCase("valid-header-still-oversized-page", doc, false) ? 0 : 1;
  }

  {
    pdf_textpagesize::TextPageSizeDocument doc;
    doc.set_header_magic("%PD");
    doc.set_page_width_decimal("xx612..0000");
    doc.set_page_height_decimal("bad79299999999999999999.0000");
    doc.set_keep_flate_tail(false);
    failures += RunCase("canonicalizes-noisy-fields-and-truncated-tail", doc,
                        false)
                    ? 0
                    : 1;
  }

  if (argc >= 4) {
    pdf_textpagesize::TextPageSizeDocument doc;
    const std::string pdf = SerializeTextPageSizePdf(doc);
    Check(RunPdftotextSmoke(argv[1], "4.04-release", pdf, true, false),
          "xpdf 4.04 release smoke crashes on the public trigger",
          &failures);
    Check(RunPdftotextSmoke(argv[2], "4.04-asan", pdf, false, true),
          "xpdf 4.04 ASan smoke reaches TextLine::TextLine crash path",
          &failures);
    Check(RunPdftotextSmoke(argv[3], "4.05-asan", pdf, false, false),
          "xpdf 4.05 ASan smoke no longer crashes on the trigger",
          &failures);
  } else {
    std::fprintf(stderr,
                 "\nsmoke: skipped (pass <xpdf-4.04-release-pdftotext>"
                 " <xpdf-4.04-asan-pdftotext> <xpdf-4.05-asan-pdftotext>)\n");
  }

  google::protobuf::ShutdownProtobufLibrary();
  if (failures) {
    std::fprintf(stderr, "\nverify_text_page_size: %d failure(s)\n", failures);
    return 1;
  }
  std::fprintf(stderr, "\nverify_text_page_size: all checks passed\n");
  return 0;
}
