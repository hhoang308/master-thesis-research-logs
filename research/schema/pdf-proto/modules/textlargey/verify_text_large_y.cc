#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "modules/textlargey/text_large_y.pb.h"
#include "modules/textlargey/text_large_y_serializer.h"

namespace {

constexpr char kDefaultHeaderLine[] = "%PDF-1.4";
constexpr char kDefaultFontName[] = "Helvetica";
constexpr uint32_t kDefaultFontSize = 12;
constexpr uint32_t kDefaultPageWidth = 612;
constexpr uint32_t kDefaultPageHeight = 200000500;
constexpr uint32_t kDefaultNormalX = 72;
constexpr uint32_t kDefaultNormalY = 72;
constexpr char kDefaultNormalText[] = "N";
constexpr uint32_t kDefaultLargeX = 72;
constexpr uint32_t kDefaultLargeY = 200000000;
constexpr char kDefaultLargeText[] = "A";

constexpr char kMediaBoxPrefix[] = "/MediaBox [0 0 ";
constexpr char kFontPrefix[] = "/BaseFont /";
constexpr char kContentPrefix[] = "BT\n/F1 ";
constexpr char kTmPrefix[] = "1 0 0 1 ";

struct ParseResult {
  bool ok = false;
  std::string error;
  pdf_textlargey::TextLargeYDocument doc;
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
  char tmp[] = "/tmp/textlargey_verify_XXXXXX.pdf";
  int fd = mkstemps(tmp, 4);
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

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

bool ParseUnsignedToken(const std::string& token, uint32_t* out) {
  if (token.empty()) {
    return false;
  }
  for (unsigned char ch : token) {
    if (ch < '0' || ch > '9') {
      return false;
    }
  }
  char* end = nullptr;
  unsigned long value = std::strtoul(token.c_str(), &end, 10);
  if (end != token.c_str() + token.size() || value > 0xffffffffUL) {
    return false;
  }
  *out = static_cast<uint32_t>(value);
  return true;
}

bool ExtractUntil(const std::string& pdf, size_t start, char delim,
                  std::string* token, size_t* next) {
  size_t end = pdf.find(delim, start);
  if (end == std::string::npos) {
    return false;
  }
  *token = pdf.substr(start, end - start);
  *next = end + 1;
  return true;
}

bool ParsePdfLiteral(const std::string& pdf, size_t start, std::string* text,
                     size_t* next) {
  if (start >= pdf.size() || pdf[start] != '(') {
    return false;
  }
  std::string out;
  for (size_t i = start + 1; i < pdf.size(); ++i) {
    char ch = pdf[i];
    if (ch == '\\') {
      if (i + 1 >= pdf.size()) {
        return false;
      }
      out.push_back(pdf[i + 1]);
      ++i;
      continue;
    }
    if (ch == ')') {
      *text = out;
      *next = i + 1;
      return true;
    }
    out.push_back(ch);
  }
  return false;
}

ParseResult ParseSerializedPdf(const std::string& pdf) {
  ParseResult result;
  size_t newline = pdf.find('\n');
  if (newline == std::string::npos) {
    result.error = "missing header newline";
    return result;
  }
  result.doc.set_header_line(pdf.substr(0, newline));

  size_t font_pos = pdf.find(kFontPrefix);
  if (font_pos == std::string::npos) {
    result.error = "missing /BaseFont";
    return result;
  }
  font_pos += std::string(kFontPrefix).size();
  size_t font_end = pdf.find_first_of(" \n>", font_pos);
  if (font_end == std::string::npos) {
    result.error = "unterminated /BaseFont token";
    return result;
  }
  result.doc.set_font_name(pdf.substr(font_pos, font_end - font_pos));

  size_t media_box = pdf.find(kMediaBoxPrefix);
  if (media_box == std::string::npos) {
    result.error = "missing /MediaBox";
    return result;
  }
  size_t pos = media_box + std::string(kMediaBoxPrefix).size();
  std::string token;
  if (!ExtractUntil(pdf, pos, ' ', &token, &pos)) {
    result.error = "missing page_width token";
    return result;
  }
  uint32_t page_width = 0;
  if (!ParseUnsignedToken(token, &page_width)) {
    result.error = "invalid page_width token";
    return result;
  }
  if (!ExtractUntil(pdf, pos, ']', &token, &pos)) {
    result.error = "missing page_height token";
    return result;
  }
  uint32_t page_height = 0;
  if (!ParseUnsignedToken(token, &page_height)) {
    result.error = "invalid page_height token";
    return result;
  }
  result.doc.set_page_width(page_width);
  result.doc.set_page_height(page_height);

  size_t content = pdf.find(kContentPrefix);
  if (content == std::string::npos) {
    result.error = "missing content stream prefix";
    return result;
  }
  content += std::string(kContentPrefix).size();
  if (!ExtractUntil(pdf, content, ' ', &token, &content)) {
    result.error = "missing font_size token";
    return result;
  }
  uint32_t font_size = 0;
  if (!ParseUnsignedToken(token, &font_size)) {
    result.error = "invalid font_size token";
    return result;
  }
  result.doc.set_font_size(font_size);

  content = pdf.find(kTmPrefix, content);
  if (content == std::string::npos) {
    result.error = "missing first Tm operator";
    return result;
  }
  content += std::string(kTmPrefix).size();
  if (!ExtractUntil(pdf, content, ' ', &token, &content)) {
    result.error = "missing normal_x token";
    return result;
  }
  uint32_t normal_x = 0;
  if (!ParseUnsignedToken(token, &normal_x)) {
    result.error = "invalid normal_x token";
    return result;
  }
  if (!ExtractUntil(pdf, content, ' ', &token, &content)) {
    result.error = "missing normal_y token";
    return result;
  }
  uint32_t normal_y = 0;
  if (!ParseUnsignedToken(token, &normal_y)) {
    result.error = "invalid normal_y token";
    return result;
  }
  result.doc.set_normal_x(normal_x);
  result.doc.set_normal_y(normal_y);

  size_t literal_pos = pdf.find('(', content);
  if (literal_pos == std::string::npos) {
    result.error = "missing normal text literal";
    return result;
  }
  std::string text;
  if (!ParsePdfLiteral(pdf, literal_pos, &text, &content)) {
    result.error = "invalid normal text literal";
    return result;
  }
  result.doc.set_normal_text(text);

  content = pdf.find(kTmPrefix, content);
  if (content == std::string::npos) {
    result.error = "missing second Tm operator";
    return result;
  }
  content += std::string(kTmPrefix).size();
  if (!ExtractUntil(pdf, content, ' ', &token, &content)) {
    result.error = "missing large_x token";
    return result;
  }
  uint32_t large_x = 0;
  if (!ParseUnsignedToken(token, &large_x)) {
    result.error = "invalid large_x token";
    return result;
  }
  if (!ExtractUntil(pdf, content, ' ', &token, &content)) {
    result.error = "missing large_y token";
    return result;
  }
  uint32_t large_y = 0;
  if (!ParseUnsignedToken(token, &large_y)) {
    result.error = "invalid large_y token";
    return result;
  }
  result.doc.set_large_x(large_x);
  result.doc.set_large_y(large_y);

  literal_pos = pdf.find('(', content);
  if (literal_pos == std::string::npos) {
    result.error = "missing large text literal";
    return result;
  }
  if (!ParsePdfLiteral(pdf, literal_pos, &text, &content)) {
    result.error = "invalid large text literal";
    return result;
  }
  result.doc.set_large_text(text);

  result.ok = true;
  return result;
}

bool StructuralInvariants(const pdf_textlargey::TextLargeYDocument& doc,
                          std::string* why) {
  if (doc.header_line().rfind("%PDF-1.", 0) != 0) {
    *why = "header_line must start with %PDF-1.";
    return false;
  }
  if (doc.font_name().empty()) {
    *why = "font_name must not be empty";
    return false;
  }
  if (doc.font_size() == 0) {
    *why = "font_size must be positive";
    return false;
  }
  if (doc.page_width() == 0) {
    *why = "page_width must be positive";
    return false;
  }
  if (doc.page_height() <= 100000000) {
    *why = "page_height must stay above the large-y threshold";
    return false;
  }
  if (doc.large_y() <= 100000000) {
    *why = "large_y must stay above the large-y threshold";
    return false;
  }
  if (doc.large_y() >= doc.page_height()) {
    *why = "large_y must remain inside the page height";
    return false;
  }
  if (doc.normal_y() >= 100000000) {
    *why = "normal_y should remain in the ordinary text range";
    return false;
  }
  if (doc.normal_text().empty() || doc.large_text().empty()) {
    *why = "text literals must not be empty";
    return false;
  }
  return true;
}

bool ContentAssertions(const std::string& pdf,
                       const pdf_textlargey::TextLargeYDocument& canon,
                       std::string* why) {
  const std::string media_box =
      std::string("/MediaBox [0 0 ") + std::to_string(canon.page_width()) +
      " " + std::to_string(canon.page_height()) + "]";
  const std::string font_anchor =
      std::string("/BaseFont /") + canon.font_name();
  const std::string normal_tm =
      std::string("1 0 0 1 ") + std::to_string(canon.normal_x()) + " " +
      std::to_string(canon.normal_y()) + " Tm";
  const std::string large_tm =
      std::string("1 0 0 1 ") + std::to_string(canon.large_x()) + " " +
      std::to_string(canon.large_y()) + " Tm";
  const std::string font_tf =
      std::string("/F1 ") + std::to_string(canon.font_size()) + " Tf";
  if (!Contains(pdf, canon.header_line()) ||
      !Contains(pdf, media_box) ||
      !Contains(pdf, font_anchor) ||
      !Contains(pdf, font_tf) ||
      !Contains(pdf, normal_tm) ||
      !Contains(pdf, large_tm) ||
      !Contains(pdf, "(" + canon.normal_text() + ") Tj") ||
      !Contains(pdf, "(" + canon.large_text() + ") Tj") ||
      !Contains(pdf, "xref\n0 6\n") ||
      !Contains(pdf, "trailer\n<< /Size 6 /Root 1 0 R >>")) {
    *why = "serialized PDF is missing required CVE-2022-30524 tokens";
    return false;
  }
  return true;
}

bool ParserSanity(const std::string& pdf, std::string* why) {
  std::string path;
  if (!WriteTemp(pdf, &path)) {
    *why = "could not create temp PDF for parser sanity checks";
    return false;
  }

  if (CommandExists("qpdf")) {
    CommandResult qpdf =
        RunCommandCapture(std::string("qpdf --check ") + ShellQuote(path));
    if (qpdf.status == -1 ||
        !WIFEXITED(qpdf.status) ||
        WEXITSTATUS(qpdf.status) != 0) {
      unlink(path.c_str());
      *why = "qpdf rejected the serialized PDF";
      return false;
    }
  }
  if (CommandExists("mutool")) {
    CommandResult mutool =
        RunCommandCapture(std::string("mutool info ") + ShellQuote(path));
    if (mutool.status == -1 ||
        !WIFEXITED(mutool.status) ||
        WEXITSTATUS(mutool.status) != 0) {
      unlink(path.c_str());
      *why = "mutool rejected the serialized PDF";
      return false;
    }
  }
  if (CommandExists("pdfinfo")) {
    CommandResult pdfinfo =
        RunCommandCapture(std::string("pdfinfo ") + ShellQuote(path));
    if (pdfinfo.status == -1 ||
        !WIFEXITED(pdfinfo.status) ||
        WEXITSTATUS(pdfinfo.status) != 0 ||
        !Contains(pdfinfo.output, "Pages:")) {
      unlink(path.c_str());
      *why = "pdfinfo rejected the serialized PDF";
      return false;
    }
  }

  unlink(path.c_str());
  *why = "available parsers accepted the serialized PDF";
  return true;
}

bool RunPdftotextSmoke(const std::string& pdftotext, const std::string& label,
                       const std::string& pdf,
                       const pdf_textlargey::TextLargeYDocument& canon,
                       bool expect_release_crash,
                       bool expect_asan_crash) {
  std::string pdf_path;
  if (!WriteTemp(pdf, &pdf_path)) {
    std::fprintf(stderr, "FAIL: cannot write temp PDF for %s\n", label.c_str());
    return false;
  }
  char out_tmp[] = "/tmp/textlargey_smoke_XXXXXX.txt";
  int out_fd = mkstemps(out_tmp, 4);
  if (out_fd < 0) {
    unlink(pdf_path.c_str());
    std::fprintf(stderr, "FAIL: cannot create temp output for %s\n",
                 label.c_str());
    return false;
  }
  close(out_fd);
  const std::string out_path = out_tmp;

  std::string cmd = ShellQuote(pdftotext) + " " + ShellQuote(pdf_path) + " " +
                    ShellQuote(out_path);
  if (expect_asan_crash || Contains(label, "asan")) {
    cmd = "env ASAN_OPTIONS=detect_leaks=0 " + cmd;
  }
  CommandResult run = RunCommandCapture(cmd);
  std::string text_output;
  (void)ReadFile(out_path, &text_output);
  unlink(pdf_path.c_str());
  unlink(out_path.c_str());

  const bool saw_asan =
      Contains(run.output, "AddressSanitizer") &&
      Contains(run.output, "TextLine::TextLine");
  const bool saw_path =
      Contains(run.output, "TextPage::writeReadingOrder") ||
      Contains(run.output, "TextOutputDev::endPage");
  const bool saw_sigsegv =
      Contains(run.output, "Segmentation fault") ||
      (run.status != -1 && WIFSIGNALED(run.status) &&
       WTERMSIG(run.status) == SIGSEGV);

  std::fprintf(stderr,
               "pdftotext smoke %-12s signaled=%d exited=%d asan=%d path=%d out=%zu\n",
               label.c_str(),
               run.status != -1 && WIFSIGNALED(run.status) ? WTERMSIG(run.status)
                                                           : 0,
               run.status != -1 && WIFEXITED(run.status) ? WEXITSTATUS(run.status)
                                                         : -1,
               saw_asan ? 1 : 0, saw_path ? 1 : 0, text_output.size());

  if (expect_release_crash) {
    return saw_sigsegv;
  }
  if (expect_asan_crash) {
    return saw_asan && saw_path;
  }
  return run.status != -1 && WIFEXITED(run.status) && WEXITSTATUS(run.status) == 0 &&
         !saw_asan && Contains(text_output, canon.large_text());
}

bool RunCase(const char* name, const pdf_textlargey::TextLargeYDocument& doc,
             bool expect_default_shape) {
  std::fprintf(stderr, "\n=== %s ===\n", name);
  const pdf_textlargey::TextLargeYDocument canon =
      CanonicalizeTextLargeYDocument(doc);
  const std::string pdf = SerializeTextLargeYPdf(doc);

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

  const pdf_textlargey::TextLargeYDocument reparsed =
      CanonicalizeTextLargeYDocument(parsed.doc);
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

  if (!ParserSanity(pdf, &why)) {
    std::fprintf(stderr, "FAIL: parser sanity: %s\n", why.c_str());
    return false;
  }
  std::fprintf(stderr, "parser sanity: %s\n", why.c_str());

  if (expect_default_shape) {
    if (canon.header_line() != kDefaultHeaderLine ||
        canon.font_name() != kDefaultFontName ||
        canon.font_size() != kDefaultFontSize ||
        canon.page_width() != kDefaultPageWidth ||
        canon.page_height() != kDefaultPageHeight ||
        canon.normal_x() != kDefaultNormalX ||
        canon.normal_y() != kDefaultNormalY ||
        canon.normal_text() != kDefaultNormalText ||
        canon.large_x() != kDefaultLargeX ||
        canon.large_y() != kDefaultLargeY ||
        canon.large_text() != kDefaultLargeText) {
      std::fprintf(stderr, "FAIL: default CVE-2022-30524 trigger shape changed\n");
      return false;
    }
  }

  std::fprintf(stderr, "PASS: %s (%zu PDF bytes)\n", name, pdf.size());
  return true;
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
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  int failures = 0;

  {
    pdf_textlargey::TextLargeYDocument doc;
    failures += RunCase("default-cve-2022-30524-shape", doc, true) ? 0 : 1;
  }

  {
    pdf_textlargey::TextLargeYDocument doc;
    doc.set_header_line("%PDF-1.7");
    doc.set_font_name("Helvetica-Bold");
    doc.set_page_width(800);
    doc.set_page_height(300000123);
    doc.set_normal_x(90);
    doc.set_normal_y(144);
    doc.set_normal_text("HELLO");
    doc.set_large_x(91);
    doc.set_large_y(200000111);
    doc.set_large_text("BUG");
    failures += RunCase("alternate-valid-large-y-shape", doc, false) ? 0 : 1;
  }

  {
    pdf_textlargey::TextLargeYDocument doc;
    doc.set_header_line("garbage");
    doc.set_font_name("Helv(etica)");
    doc.set_font_size(0);
    doc.set_page_width(0);
    doc.set_page_height(10);
    doc.set_normal_y(500000000);
    doc.set_normal_text("");
    doc.set_large_y(1);
    doc.set_large_text("");
    failures += RunCase("canonicalizes-noisy-fields", doc, false) ? 0 : 1;
  }

  if (argc >= 4) {
    pdf_textlargey::TextLargeYDocument doc;
    const pdf_textlargey::TextLargeYDocument canon =
        CanonicalizeTextLargeYDocument(doc);
    const std::string pdf = SerializeTextLargeYPdf(doc);
    Check(RunPdftotextSmoke(argv[1], "4.04-release", pdf, canon, true, false),
          "xpdf 4.04 release smoke crashes on the large-y trigger",
          &failures);
    Check(RunPdftotextSmoke(argv[2], "4.04-asan", pdf, canon, false, true),
          "xpdf 4.04 ASan smoke reaches TextLine::TextLine crash path",
          &failures);
    Check(RunPdftotextSmoke(argv[3], "4.05-asan", pdf, canon, false, false),
          "xpdf 4.05 ASan smoke no longer crashes on the trigger",
          &failures);
  } else {
    std::fprintf(stderr,
                 "\nsmoke: skipped (pass <xpdf-4.04-release-pdftotext>"
                 " <xpdf-4.04-asan-pdftotext> <xpdf-4.05-asan-pdftotext>)\n");
  }

  google::protobuf::ShutdownProtobufLibrary();
  if (failures) {
    std::fprintf(stderr, "\nverify_text_large_y: %d failure(s)\n", failures);
    return 1;
  }
  std::fprintf(stderr, "\nverify_text_large_y: all checks passed\n");
  return 0;
}
