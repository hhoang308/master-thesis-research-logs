#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "modules/objstmlength/objstm_length.pb.h"
#include "modules/objstmlength/objstm_length_serializer.h"

namespace {

constexpr char kDefaultHeader[] = "%PDF-1.5";
constexpr uint32_t kMaxPaddingSpaces = 32;
constexpr size_t kXrefEntryWidth = 7;

struct ParseResult {
  bool ok = false;
  std::string error;
  pdf_objstmlength::ObjstmLengthDocument doc;
};

struct CommandResult {
  int status = -1;
  std::string output;
};

struct XrefEntry {
  uint8_t type = 0;
  uint32_t field2 = 0;
  uint16_t field3 = 0;
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
  char tmp[] = "/tmp/objstmlength_verify_XXXXXX";
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

std::string Trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() &&
         std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

bool ParsePositiveIntToken(const std::string& token, int* out) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  long value = std::strtol(token.c_str(), &end, 10);
  if (end != token.c_str() + token.size() || value <= 0 ||
      value > 1'000'000L) {
    return false;
  }
  *out = static_cast<int>(value);
  return true;
}

bool ParseNonNegativeIntToken(const std::string& token, int* out) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  long value = std::strtol(token.c_str(), &end, 10);
  if (end != token.c_str() + token.size() || value < 0 ||
      value > 1'000'000L) {
    return false;
  }
  *out = static_cast<int>(value);
  return true;
}

std::string ExtractObjectBody(const std::string& pdf, int obj_num,
                              std::string* error) {
  const std::string anchor = std::to_string(obj_num) + " 0 obj\n";
  const size_t start = pdf.find(anchor);
  if (start == std::string::npos) {
    *error = "missing object anchor " + anchor;
    return {};
  }
  const size_t body_start = start + anchor.size();
  const size_t end = pdf.find("\nendobj\n", body_start);
  if (end == std::string::npos) {
    *error = "missing endobj for object " + std::to_string(obj_num);
    return {};
  }
  return pdf.substr(body_start, end - body_start);
}

std::string ExtractObjectDict(const std::string& body, std::string* error) {
  const size_t stream = body.find("\nstream\n");
  if (stream == std::string::npos) {
    if (!Contains(body, "<<")) {
      *error = "missing object dictionary";
      return {};
    }
    return body;
  }
  return body.substr(0, stream);
}

std::string ExtractStreamData(const std::string& body, std::string* error) {
  const size_t stream = body.find("stream\n");
  if (stream == std::string::npos) {
    *error = "missing stream keyword";
    return {};
  }
  const size_t data_start = stream + 7;
  const size_t end = body.find("\nendstream", data_start);
  if (end == std::string::npos) {
    *error = "missing endstream keyword";
    return {};
  }
  return body.substr(data_start, end - data_start);
}

bool ExtractMediaBoxInts(const std::string& page_body, int* width, int* height) {
  const std::string key = "/MediaBox [0 0 ";
  size_t pos = page_body.find(key);
  if (pos == std::string::npos) {
    return false;
  }
  pos += key.size();
  const size_t width_end = page_body.find(' ', pos);
  if (width_end == std::string::npos) {
    return false;
  }
  const size_t height_end = page_body.find(']', width_end + 1);
  if (height_end == std::string::npos) {
    return false;
  }
  return ParsePositiveIntToken(page_body.substr(pos, width_end - pos), width) &&
         ParsePositiveIntToken(
             Trim(page_body.substr(width_end + 1,
                                   height_end - (width_end + 1))),
             height);
}

bool ExtractSingleInt(const std::string& dict, const std::string& key, int* out) {
  size_t pos = dict.find(key);
  if (pos == std::string::npos) {
    return false;
  }
  pos += key.size();
  while (pos < dict.size() &&
         std::isspace(static_cast<unsigned char>(dict[pos]))) {
    ++pos;
  }
  size_t end = pos;
  while (end < dict.size() &&
         std::isdigit(static_cast<unsigned char>(dict[end]))) {
    ++end;
  }
  return ParseNonNegativeIntToken(dict.substr(pos, end - pos), out);
}

bool ParseObjstmAData(const std::string& stream_data, uint32_t* spaces,
                      std::string* error) {
  const std::string prefix = "1 0 ";
  if (stream_data.rfind(prefix, 0) != 0) {
    *error = "object stream 4 payload missing '1 0 ' prefix";
    return false;
  }
  size_t pos = prefix.size();
  while (pos < stream_data.size() && stream_data[pos] == ' ') {
    ++pos;
  }
  *spaces = static_cast<uint32_t>(pos - prefix.size());
  if (stream_data.substr(pos) != "<< /Type /Catalog /Pages 2 0 R >>") {
    *error = "object stream 4 payload is not the expected catalog object";
    return false;
  }
  return true;
}

bool ParseObjstmBData(const std::string& stream_data, int* referenced_length,
                      uint32_t* trailing_spaces, std::string* error) {
  const std::string prefix = "6 0 ";
  if (stream_data.rfind(prefix, 0) != 0) {
    *error = "object stream 5 payload missing '6 0 ' prefix";
    return false;
  }
  size_t pos = prefix.size();
  size_t digits_end = pos;
  while (digits_end < stream_data.size() &&
         std::isdigit(static_cast<unsigned char>(stream_data[digits_end]))) {
    ++digits_end;
  }
  if (!ParsePositiveIntToken(stream_data.substr(pos, digits_end - pos),
                             referenced_length)) {
    *error = "object stream 5 payload missing integer object 6 value";
    return false;
  }
  for (size_t i = digits_end; i < stream_data.size(); ++i) {
    if (stream_data[i] != ' ') {
      *error = "object stream 5 payload has non-space trailing bytes";
      return false;
    }
  }
  *trailing_spaces = static_cast<uint32_t>(stream_data.size() - digits_end);
  return true;
}

uint32_t ReadBe32(const std::string& s, size_t off) {
  return (static_cast<uint32_t>(static_cast<unsigned char>(s[off])) << 24) |
         (static_cast<uint32_t>(static_cast<unsigned char>(s[off + 1])) << 16) |
         (static_cast<uint32_t>(static_cast<unsigned char>(s[off + 2])) << 8) |
         static_cast<uint32_t>(static_cast<unsigned char>(s[off + 3]));
}

uint16_t ReadBe16(const std::string& s, size_t off) {
  return static_cast<uint16_t>(
      (static_cast<uint16_t>(static_cast<unsigned char>(s[off])) << 8) |
      static_cast<uint16_t>(static_cast<unsigned char>(s[off + 1])));
}

bool ParseXrefEntries(const std::string& data, std::vector<XrefEntry>* entries,
                      std::string* error) {
  if (data.size() != 9 * kXrefEntryWidth) {
    *error = "xref stream payload does not contain 9 fixed-width entries";
    return false;
  }
  entries->clear();
  for (size_t i = 0; i < data.size(); i += kXrefEntryWidth) {
    XrefEntry entry;
    entry.type = static_cast<uint8_t>(data[i]);
    entry.field2 = ReadBe32(data, i + 1);
    entry.field3 = ReadBe16(data, i + 5);
    entries->push_back(entry);
  }
  return true;
}

uint32_t FindObjectOffset(const std::string& pdf, int obj_num) {
  const std::string anchor = std::to_string(obj_num) + " 0 obj\n";
  const size_t pos = pdf.find(anchor);
  return pos == std::string::npos ? 0 : static_cast<uint32_t>(pos);
}

ParseResult ParseSerializedPdf(const std::string& pdf) {
  ParseResult result;

  const size_t header_end = pdf.find('\n');
  if (header_end == std::string::npos) {
    result.error = "missing header newline";
    return result;
  }
  result.doc.set_pdf_header(pdf.substr(0, header_end));
  result.doc.set_include_binary_comment(
      header_end + 6 <= pdf.size() &&
      pdf.compare(header_end + 1, 5, "%\xE2\xE3\xCF\xD3") == 0);

  std::string error;
  const std::string page_body = ExtractObjectBody(pdf, 3, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  const std::string objstm_a_body = ExtractObjectBody(pdf, 4, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  const std::string objstm_b_body = ExtractObjectBody(pdf, 5, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  const std::string contents_body = ExtractObjectBody(pdf, 7, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  const std::string xref_body = ExtractObjectBody(pdf, 8, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }

  int width = 0;
  int height = 0;
  if (!ExtractMediaBoxInts(page_body, &width, &height)) {
    result.error = "missing or malformed /MediaBox";
    return result;
  }
  result.doc.set_page_width(width);
  result.doc.set_page_height(height);

  const std::string contents_dict = ExtractObjectDict(contents_body, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  const std::string contents_data = ExtractStreamData(contents_body, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  int contents_length = 0;
  if (!ExtractSingleInt(contents_dict, "/Length", &contents_length) ||
      contents_length != static_cast<int>(contents_data.size())) {
    result.error = "content stream /Length is missing or inconsistent";
    return result;
  }
  result.doc.set_page_content(contents_data);

  const std::string objstm_a_dict = ExtractObjectDict(objstm_a_body, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  if (!Contains(objstm_a_dict, "/Type /ObjStm") ||
      !Contains(objstm_a_dict, "/N 1") ||
      !Contains(objstm_a_dict, "/First 4") ||
      !Contains(objstm_a_dict, "/Length 6 0 R")) {
    result.error = "object stream 4 dictionary drifted away from the CVE shape";
    return result;
  }
  const std::string objstm_a_data = ExtractStreamData(objstm_a_body, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  uint32_t catalog_padding = 0;
  if (!ParseObjstmAData(objstm_a_data, &catalog_padding, &error)) {
    result.error = error;
    return result;
  }
  result.doc.set_catalog_padding_spaces(catalog_padding);

  const std::string objstm_b_dict = ExtractObjectDict(objstm_b_body, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  int objstm_b_declared_length = 0;
  if (!Contains(objstm_b_dict, "/Type /ObjStm") ||
      !Contains(objstm_b_dict, "/N 1") ||
      !Contains(objstm_b_dict, "/First 4") ||
      !ExtractSingleInt(objstm_b_dict, "/Length", &objstm_b_declared_length)) {
    result.error = "object stream 5 dictionary is malformed";
    return result;
  }
  const std::string objstm_b_data = ExtractStreamData(objstm_b_body, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  if (objstm_b_declared_length != static_cast<int>(objstm_b_data.size())) {
    result.error = "object stream 5 /Length does not match its payload";
    return result;
  }
  int referenced_length = 0;
  uint32_t trailing_spaces = 0;
  if (!ParseObjstmBData(objstm_b_data, &referenced_length, &trailing_spaces,
                        &error)) {
    result.error = error;
    return result;
  }
  if (referenced_length != static_cast<int>(objstm_a_data.size())) {
    result.error =
        "compressed integer object 6 does not match object stream 4 payload size";
    return result;
  }
  result.doc.set_length_object_trailing_spaces(trailing_spaces);

  const std::string xref_dict = ExtractObjectDict(xref_body, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  if (!Contains(xref_dict, "/Type /XRef") ||
      !Contains(xref_dict, "/Size 9") ||
      !Contains(xref_dict, "/Root 1 0 R") ||
      !Contains(xref_dict, "/W [1 4 2]") ||
      !Contains(xref_dict, "/Index [0 9]")) {
    result.error = "xref stream dictionary drifted away from the CVE shape";
    return result;
  }
  const std::string xref_data = ExtractStreamData(xref_body, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  std::vector<XrefEntry> entries;
  if (!ParseXrefEntries(xref_data, &entries, &error)) {
    result.error = error;
    return result;
  }
  if (entries[0].type != 0 || entries[0].field3 != 65535 ||
      entries[1].type != 2 || entries[1].field2 != 4 || entries[1].field3 != 0 ||
      entries[6].type != 2 || entries[6].field2 != 5 || entries[6].field3 != 0) {
    result.error = "compressed-object xref entries are not in the expected slots";
    return result;
  }

  const uint32_t off2 = FindObjectOffset(pdf, 2);
  const uint32_t off3 = FindObjectOffset(pdf, 3);
  const uint32_t off4 = FindObjectOffset(pdf, 4);
  const uint32_t off5 = FindObjectOffset(pdf, 5);
  const uint32_t off7 = FindObjectOffset(pdf, 7);
  const uint32_t off8 = FindObjectOffset(pdf, 8);
  if (off2 == 0 || off3 == 0 || off4 == 0 || off5 == 0 || off7 == 0 ||
      off8 == 0) {
    result.error = "failed to recover direct-object offsets from serialized PDF";
    return result;
  }
  if (!(entries[2].type == 1 && entries[2].field2 == off2 &&
        entries[3].type == 1 && entries[3].field2 == off3 &&
        entries[4].type == 1 && entries[4].field2 == off4 &&
        entries[5].type == 1 && entries[5].field2 == off5 &&
        entries[7].type == 1 && entries[7].field2 == off7 &&
        entries[8].type == 1 && entries[8].field2 == off8)) {
    result.error = "xref stream direct-object offsets do not match serialized layout";
    return result;
  }

  result.ok = true;
  return result;
}

bool StructuralInvariants(
    const pdf_objstmlength::ObjstmLengthDocument& doc, std::string* why) {
  if (doc.pdf_header().size() != 8 || doc.pdf_header().rfind("%PDF-1.", 0) != 0) {
    *why = "pdf_header is not a canonical %PDF-1.x line";
    return false;
  }
  if (doc.page_width() == 0 || doc.page_height() == 0) {
    *why = "page dimensions must be positive";
    return false;
  }
  if (doc.catalog_padding_spaces() > kMaxPaddingSpaces ||
      doc.length_object_trailing_spaces() > kMaxPaddingSpaces) {
    *why = "padding fields exceeded serializer clamp";
    return false;
  }
  if (doc.page_content().size() > 512) {
    *why = "page_content exceeded serializer cap";
    return false;
  }
  return true;
}

bool ContentAssertions(const std::string& pdf,
                       const pdf_objstmlength::ObjstmLengthDocument& canon,
                       std::string* why) {
  std::ostringstream objstm_a_data;
  objstm_a_data << "1 0 " << std::string(canon.catalog_padding_spaces(), ' ')
                << "<< /Type /Catalog /Pages 2 0 R >>";
  std::ostringstream objstm_b_data;
  objstm_b_data << "6 0 " << objstm_a_data.str().size()
                << std::string(canon.length_object_trailing_spaces(), ' ');

  const std::string media_box = "/MediaBox [0 0 " +
                                std::to_string(canon.page_width()) + " " +
                                std::to_string(canon.page_height()) + "]";
  if (!Contains(pdf, canon.pdf_header()) ||
      !Contains(pdf, "/Type /ObjStm /N 1 /First 4 /Length 6 0 R") ||
      !Contains(pdf, "/Type /XRef /Size 9 /Root 1 0 R /W [1 4 2] /Index [0 9]") ||
      !Contains(pdf, media_box) ||
      !Contains(pdf, objstm_a_data.str()) ||
      !Contains(pdf, objstm_b_data.str())) {
    *why = "serialized PDF is missing required ObjStm/xref/content tokens";
    return false;
  }
  if (canon.include_binary_comment() &&
      !Contains(pdf, std::string("%\xE2\xE3\xCF\xD3\n", 6))) {
    *why = "binary-comment line requested but not present";
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

  if (qpdf.status == -1 || !WIFEXITED(qpdf.status)) {
    *why = "qpdf invocation failed";
    return false;
  }
  if (WEXITSTATUS(qpdf.status) != 0) {
    *why = "qpdf rejected the serialized PDF";
    return false;
  }
  *why = "qpdf accepted the serialized PDF";
  return true;
}

bool RunPdftoppmSmoke(const std::string& pdftoppm, const std::string& label,
                      const std::string& pdf, bool expect_timeout,
                      bool expect_success) {
  std::string pdf_path;
  if (!WriteTemp(pdf, &pdf_path)) {
    std::fprintf(stderr, "FAIL: cannot write temp PDF for %s\n", label.c_str());
    return false;
  }

  const std::string out_base =
      std::string("/tmp/objstmlength_smoke_") + label;
  const std::string cmd = (Contains(label, "asan")
                               ? "env ASAN_OPTIONS=detect_leaks=0 "
                               : "") +
                          std::string("timeout 5s ") + ShellQuote(pdftoppm) +
                          " " + ShellQuote(pdf_path) + " " +
                          ShellQuote(out_base);
  CommandResult run = RunCommandCapture(cmd);
  unlink(pdf_path.c_str());

  const int exit_code =
      run.status != -1 && WIFEXITED(run.status) ? WEXITSTATUS(run.status) : -1;
  const bool ppm_exists =
      access((out_base + "-000001.ppm").c_str(), F_OK) == 0;

  std::fprintf(stderr, "pdftoppm smoke %-12s exit=%d ppm=%d\n", label.c_str(),
               exit_code, ppm_exists ? 1 : 0);

  if (ppm_exists) {
    unlink((out_base + "-000001.ppm").c_str());
  }

  if (expect_timeout) {
    return exit_code == 124;
  }
  if (expect_success) {
    return exit_code == 0 && ppm_exists;
  }
  return false;
}

void Check(bool condition, const char* what, int* failures) {
  if (condition) {
    std::fprintf(stderr, "PASS: %s\n", what);
  } else {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++*failures;
  }
}

bool RunCase(const char* name,
             const pdf_objstmlength::ObjstmLengthDocument& doc,
             bool expect_default_shape) {
  std::fprintf(stderr, "\n=== %s ===\n", name);
  const pdf_objstmlength::ObjstmLengthDocument canon =
      CanonicalizeObjstmLengthDocument(doc);
  const std::string pdf = SerializeObjstmLengthPdf(doc);

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

  const pdf_objstmlength::ObjstmLengthDocument reparsed =
      CanonicalizeObjstmLengthDocument(parsed.doc);
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

  if (expect_default_shape) {
    if (canon.pdf_header() != kDefaultHeader ||
        !canon.include_binary_comment() ||
        canon.page_width() != 16 || canon.page_height() != 16 ||
        canon.catalog_padding_spaces() != 0 ||
        canon.length_object_trailing_spaces() != 0 ||
        !canon.page_content().empty()) {
      std::fprintf(stderr, "FAIL: default CVE-2023-3436 trigger shape changed\n");
      return false;
    }
    if (pdf.size() != 613) {
      std::fprintf(stderr, "FAIL: default trigger size drifted (%zu bytes)\n",
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
    pdf_objstmlength::ObjstmLengthDocument doc;
    failures += RunCase("default-cve-2023-3436-shape", doc, true) ? 0 : 1;
  }

  {
    pdf_objstmlength::ObjstmLengthDocument doc;
    doc.set_page_width(32);
    doc.set_page_height(48);
    doc.set_page_content("q\nQ\n");
    doc.set_catalog_padding_spaces(4);
    doc.set_length_object_trailing_spaces(3);
    failures += RunCase("non-empty-content-with-objstm-padding", doc, false)
                    ? 0
                    : 1;
  }

  {
    pdf_objstmlength::ObjstmLengthDocument doc;
    doc.set_pdf_header("%notpdf!");
    doc.set_page_width(0);
    doc.set_page_height(999999);
    doc.set_catalog_padding_spaces(999999);
    doc.set_length_object_trailing_spaces(999999);
    doc.set_page_content(std::string(800, 'A'));
    failures += RunCase("canonicalizes-noisy-mutator-fields", doc, false) ? 0 : 1;
  }

  if (argc >= 4) {
    pdf_objstmlength::ObjstmLengthDocument doc;
    const std::string pdf = SerializeObjstmLengthPdf(doc);
    Check(RunPdftoppmSmoke(argv[1], "4.04-release", pdf, true, false),
          "xpdf 4.04 release smoke hangs on the object-stream /Length trigger",
          &failures);
    Check(RunPdftoppmSmoke(argv[2], "4.04-asan", pdf, true, false),
          "xpdf 4.04 ASan smoke also hangs on the same trigger",
          &failures);
    Check(RunPdftoppmSmoke(argv[3], "4.05-asan", pdf, false, true),
          "xpdf 4.05 ASan smoke no longer deadlocks on the trigger",
          &failures);
  } else {
    std::fprintf(stderr,
                 "\nsmoke: skipped (pass <xpdf-4.04-release-pdftoppm>"
                 " <xpdf-4.04-asan-pdftoppm> <xpdf-4.05-asan-pdftoppm>)\n");
  }

  google::protobuf::ShutdownProtobufLibrary();
  if (failures) {
    std::fprintf(stderr, "\nverify_objstm_length: %d failure(s)\n", failures);
    return 1;
  }
  std::fprintf(stderr, "\nverify_objstm_length: all checks passed\n");
  return 0;
}
