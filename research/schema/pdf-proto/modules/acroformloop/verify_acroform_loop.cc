#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "modules/acroformloop/acroform_loop.pb.h"
#include "modules/acroformloop/acroform_loop_serializer.h"

namespace {

struct FieldInfo {
  int obj_num = 0;
  int kid_ref = 0;
  int parent_ref = 0;
  bool has_name = false;
};

struct ParseResult {
  bool ok = false;
  std::string error;
  pdf_acroformloop::AcroFormLoopDocument doc;
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
  char tmp[] = "/tmp/acroformloop_verify_XXXXXX";
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

std::string Trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

std::string ExtractObjectBody(const std::string& pdf, int obj_num,
                              std::string* error) {
  const std::string anchor = std::to_string(obj_num) + " 0 obj\n";
  size_t start = pdf.find(anchor);
  if (start == std::string::npos) {
    *error = "missing object anchor " + anchor;
    return {};
  }
  start += anchor.size();
  size_t end = pdf.find("\nendobj\n", start);
  if (end == std::string::npos) {
    *error = "missing endobj for object " + std::to_string(obj_num);
    return {};
  }
  return pdf.substr(start, end - start);
}

std::string ExtractStreamData(const std::string& body, std::string* error) {
  size_t stream = body.find("stream\n");
  if (stream == std::string::npos) {
    *error = "missing stream keyword";
    return {};
  }
  stream += 7;
  size_t end = body.find("\nendstream", stream);
  if (end == std::string::npos) {
    *error = "missing endstream keyword";
    return {};
  }
  return body.substr(stream, end - stream);
}

bool ExtractMediaBox(const std::string& page_body, int* width, int* height) {
  const std::string key = "/MediaBox [0 0 ";
  size_t pos = page_body.find(key);
  if (pos == std::string::npos) {
    return false;
  }
  pos += key.size();
  size_t width_end = page_body.find(' ', pos);
  if (width_end == std::string::npos) {
    return false;
  }
  size_t height_end = page_body.find(']', width_end + 1);
  if (height_end == std::string::npos) {
    return false;
  }
  return ParsePositiveIntToken(page_body.substr(pos, width_end - pos), width) &&
         ParsePositiveIntToken(
             Trim(page_body.substr(width_end + 1,
                                   height_end - (width_end + 1))),
             height);
}

bool ParseFieldRefsArray(const std::string& body, int* first_ref) {
  const std::string key = "/Fields [";
  size_t pos = body.find(key);
  if (pos == std::string::npos) {
    return false;
  }
  pos += key.size();
  size_t ref_end = body.find(" 0 R", pos);
  if (ref_end == std::string::npos) {
    return false;
  }
  return ParsePositiveIntToken(Trim(body.substr(pos, ref_end - pos)), first_ref);
}

bool ParseFieldInfo(const std::string& body, FieldInfo* field) {
  const std::string kids_key = "/Kids [";
  size_t kids = body.find(kids_key);
  if (kids == std::string::npos) {
    return false;
  }
  kids += kids_key.size();
  size_t kids_end = body.find(" 0 R", kids);
  if (kids_end == std::string::npos ||
      !ParsePositiveIntToken(Trim(body.substr(kids, kids_end - kids)),
                             &field->kid_ref)) {
    return false;
  }

  const std::string parent_key = "/Parent ";
  size_t parent = body.find(parent_key);
  if (parent == std::string::npos) {
    return false;
  }
  parent += parent_key.size();
  size_t parent_end = body.find(" 0 R", parent);
  if (parent_end == std::string::npos ||
      !ParsePositiveIntToken(Trim(body.substr(parent, parent_end - parent)),
                             &field->parent_ref)) {
    return false;
  }
  field->has_name = body.find("/T (") != std::string::npos;
  return true;
}

ParseResult ParseSerializedPdf(const std::string& pdf) {
  ParseResult result;
  if (!Contains(pdf, "%PDF-1.4")) {
    result.error = "missing %PDF-1.4 header";
    return result;
  }

  std::string error;
  const std::string page_body = ExtractObjectBody(pdf, 3, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  const std::string contents_body = ExtractObjectBody(pdf, 4, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }
  const std::string acroform_body = ExtractObjectBody(pdf, 5, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }

  int width = 0;
  int height = 0;
  if (!ExtractMediaBox(page_body, &width, &height)) {
    result.error = "missing or malformed /MediaBox";
    return result;
  }

  int first_ref = 0;
  if (!ParseFieldRefsArray(acroform_body, &first_ref)) {
    result.error = "missing or malformed /Fields array";
    return result;
  }

  std::string contents_error;
  const std::string contents_data = ExtractStreamData(contents_body, &contents_error);
  if (!contents_error.empty()) {
    result.error = contents_error;
    return result;
  }

  std::vector<FieldInfo> fields;
  std::vector<int> order;
  int current = first_ref;
  for (int step = 0; step < 64; ++step) {
    auto seen = std::find(order.begin(), order.end(), current);
    if (seen != order.end()) {
      if (seen != order.begin()) {
        result.error = "loop does not close back to the root field";
        return result;
      }
      break;
    }
    FieldInfo info;
    info.obj_num = current;
    const std::string body = ExtractObjectBody(pdf, current, &error);
    if (!error.empty()) {
      result.error = error;
      return result;
    }
    if (!ParseFieldInfo(body, &info)) {
      result.error =
          "field object " + std::to_string(current) + " missing /Kids or /Parent";
      return result;
    }
    order.push_back(current);
    fields.push_back(info);
    current = info.kid_ref;
  }

  if (order.empty()) {
    result.error = "no field objects reachable from /Fields";
    return result;
  }
  if (current != first_ref) {
    result.error = "field traversal exceeded parser limit without a cycle";
    return result;
  }

  result.doc.set_page_width(static_cast<uint32_t>(width));
  result.doc.set_page_height(static_cast<uint32_t>(height));
  result.doc.set_include_need_appearances(
      Contains(acroform_body, "/NeedAppearances true"));
  result.doc.set_include_field_names(
      std::all_of(fields.begin(), fields.end(),
                  [](const FieldInfo& field) { return field.has_name; }));
  result.doc.set_nonempty_contents(!Trim(contents_data).empty());
  result.doc.set_loop_nodes(static_cast<uint32_t>(fields.size()));

  if (fields.size() == 1) {
    result.doc.set_loop_shape(pdf_acroformloop::AcroFormLoopDocument::SELF_LOOP);
  } else if (fields.size() == 2) {
    result.doc.set_loop_shape(
        pdf_acroformloop::AcroFormLoopDocument::MUTUAL_LOOP);
  } else {
    result.doc.set_loop_shape(pdf_acroformloop::AcroFormLoopDocument::CYCLE);
  }

  const bool self_parent = std::all_of(
      fields.begin(), fields.end(),
      [](const FieldInfo& field) { return field.parent_ref == field.obj_num; });
  const bool root_parent = std::all_of(
      fields.begin(), fields.end(),
      [](const FieldInfo& field) { return field.parent_ref == 5; });
  bool previous_parent = true;
  for (size_t i = 0; i < fields.size(); ++i) {
    const int prev_obj = order[(i + fields.size() - 1) % fields.size()];
    if (fields[i].parent_ref != prev_obj) {
      previous_parent = false;
      break;
    }
  }
  if (self_parent) {
    result.doc.set_parent_mode(
        pdf_acroformloop::AcroFormLoopDocument::SELF_PARENT);
  } else if (previous_parent) {
    result.doc.set_parent_mode(
        pdf_acroformloop::AcroFormLoopDocument::PREVIOUS_PARENT);
  } else if (root_parent) {
    result.doc.set_parent_mode(
        pdf_acroformloop::AcroFormLoopDocument::ROOT_PARENT);
  } else {
    result.error = "field parent references do not match a supported parent mode";
    return result;
  }

  result.ok = true;
  return result;
}

bool StructuralInvariants(const pdf_acroformloop::AcroFormLoopDocument& doc,
                          std::string* why) {
  if (doc.page_width() == 0 || doc.page_height() == 0) {
    *why = "page dimensions must be non-zero";
    return false;
  }
  switch (doc.loop_shape()) {
    case pdf_acroformloop::AcroFormLoopDocument::SELF_LOOP:
      if (doc.loop_nodes() != 1) {
        *why = "SELF_LOOP must have exactly one field node";
        return false;
      }
      break;
    case pdf_acroformloop::AcroFormLoopDocument::MUTUAL_LOOP:
      if (doc.loop_nodes() != 2) {
        *why = "MUTUAL_LOOP must have exactly two field nodes";
        return false;
      }
      break;
    case pdf_acroformloop::AcroFormLoopDocument::CYCLE:
      if (doc.loop_nodes() < 3) {
        *why = "CYCLE must have at least three field nodes";
        return false;
      }
      break;
    default:
      *why = "unsupported loop_shape enum";
      return false;
  }
  return true;
}

bool ContentAssertions(const std::string& pdf,
                       const pdf_acroformloop::AcroFormLoopDocument& canon,
                       std::string* why) {
  const std::string media_box = "/MediaBox [0 0 " +
                                std::to_string(canon.page_width()) + " " +
                                std::to_string(canon.page_height()) + "]";
  if (!Contains(pdf, "%PDF-1.4") ||
      !Contains(pdf, "/Type /Catalog /Pages 2 0 R /AcroForm 5 0 R") ||
      !Contains(pdf, "/Fields [6 0 R]") ||
      !Contains(pdf, "/Kids [") ||
      !Contains(pdf, "/Parent ") ||
      !Contains(pdf, "/Type /Page") ||
      !Contains(pdf, media_box)) {
    *why = "serialized PDF missing required AcroForm loop tokens";
    return false;
  }
  if (canon.include_need_appearances() &&
      !Contains(pdf, "/NeedAppearances true")) {
    *why = "NeedAppearances requested but missing";
    return false;
  }
  if (canon.include_field_names() && !Contains(pdf, "/T (loop-")) {
    *why = "field names requested but missing";
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

bool QpdfAccepts(const std::string& pdf, std::string* why) {
  if (!CommandExists("qpdf")) {
    *why = "qpdf not installed";
    return true;
  }
  std::string path;
  if (!WriteTemp(pdf, &path)) {
    *why = "cannot write temp PDF for qpdf";
    return false;
  }
  CommandResult result =
      RunCommandCapture("qpdf --check " + ShellQuote(path));
  unlink(path.c_str());
  if (result.status == -1 || !WIFEXITED(result.status)) {
    *why = "qpdf did not run to completion";
    return false;
  }
  const int code = WEXITSTATUS(result.status);
  if (code != 0) {
    *why = "qpdf rejected the generated PDF";
    return false;
  }
  *why = "qpdf accepted the serialized PDF";
  return true;
}

enum class SmokeExpectation {
  kReleaseVulnerable,
  kAsanVulnerable,
  kAsanFixed,
};

bool RunXpdfSmoke(const std::string& pdftotext, const std::string& label,
                  const std::string& pdf, SmokeExpectation expectation) {
  std::string pdf_path;
  if (!WriteTemp(pdf, &pdf_path)) {
    std::fprintf(stderr, "FAIL: cannot write temp PDF for %s\n", label.c_str());
    return false;
  }
  const std::string out_path =
      std::string("/tmp/acroformloop_smoke_") + label + ".txt";

  std::string inner = "ulimit -s 256; ";
  if (expectation != SmokeExpectation::kReleaseVulnerable) {
    inner += "ASAN_OPTIONS=detect_leaks=0 ";
  }
  inner += "timeout 10s " + ShellQuote(pdftotext) + " " + ShellQuote(pdf_path) +
           " " + ShellQuote(out_path);

  CommandResult run =
      RunCommandCapture("bash -lc " + ShellQuote(inner));
  unlink(pdf_path.c_str());

  const int exit_code = run.status != -1 && WIFEXITED(run.status)
                            ? WEXITSTATUS(run.status)
                            : -1;
  const bool saw_asan = Contains(run.output, "AddressSanitizer");
  const bool saw_stack = Contains(run.output, "stack-overflow");
  const bool saw_scanfield = Contains(run.output, "AcroForm::scanField");
  const bool segv = exit_code == 139 || Contains(run.output, "Segmentation fault");

  std::fprintf(stderr,
               "xpdf smoke %-12s exit=%d segv=%d asan=%d stack=%d scanfield=%d\n",
               label.c_str(), exit_code, segv ? 1 : 0, saw_asan ? 1 : 0,
               saw_stack ? 1 : 0, saw_scanfield ? 1 : 0);

  switch (expectation) {
    case SmokeExpectation::kReleaseVulnerable:
      return segv;
    case SmokeExpectation::kAsanVulnerable:
      return saw_asan && saw_stack && saw_scanfield;
    case SmokeExpectation::kAsanFixed:
      return exit_code == 0 && !saw_asan;
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
             const pdf_acroformloop::AcroFormLoopDocument& doc,
             bool expect_default_shape) {
  std::fprintf(stderr, "\n=== %s ===\n", name);
  const pdf_acroformloop::AcroFormLoopDocument canon =
      CanonicalizeAcroFormLoopDocument(doc);
  const std::string pdf = SerializeAcroFormLoopPdf(doc);

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

  const pdf_acroformloop::AcroFormLoopDocument reparsed =
      CanonicalizeAcroFormLoopDocument(parsed.doc);
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
  if (!QpdfAccepts(pdf, &why)) {
    std::fprintf(stderr, "FAIL: qpdf sanity: %s\n", why.c_str());
    return false;
  }
  std::fprintf(stderr, "qpdf parser: %s\n", why.c_str());

  if (expect_default_shape &&
      !(canon.loop_shape() ==
            pdf_acroformloop::AcroFormLoopDocument::MUTUAL_LOOP &&
        canon.loop_nodes() == 2 &&
        canon.parent_mode() ==
            pdf_acroformloop::AcroFormLoopDocument::SELF_PARENT &&
        canon.include_field_names() &&
        !canon.nonempty_contents())) {
    std::fprintf(stderr, "FAIL: expected default CVE-2018-7453 shape missing\n");
    return false;
  }

  std::fprintf(stderr, "PASS: %s (%zu PDF bytes)\n", name, pdf.size());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  int failures = 0;

  {
    pdf_acroformloop::AcroFormLoopDocument doc;
    failures += RunCase("default-cve-2018-7453-shape", doc, true) ? 0 : 1;
  }

  {
    pdf_acroformloop::AcroFormLoopDocument doc;
    doc.set_loop_shape(pdf_acroformloop::AcroFormLoopDocument::SELF_LOOP);
    doc.set_loop_nodes(99);
    doc.set_parent_mode(pdf_acroformloop::AcroFormLoopDocument::ROOT_PARENT);
    doc.set_include_field_names(false);
    doc.set_nonempty_contents(true);
    failures += RunCase("self-loop-root-parent", doc, false) ? 0 : 1;
  }

  {
    pdf_acroformloop::AcroFormLoopDocument doc;
    doc.set_loop_shape(pdf_acroformloop::AcroFormLoopDocument::CYCLE);
    doc.set_loop_nodes(4);
    doc.set_parent_mode(
        pdf_acroformloop::AcroFormLoopDocument::PREVIOUS_PARENT);
    doc.set_include_need_appearances(true);
    doc.set_page_width(0);
    doc.set_page_height(99999);
    failures += RunCase("four-node-cycle-canonicalizes-page-size", doc, false)
                    ? 0
                    : 1;
  }

  if (argc >= 4) {
    pdf_acroformloop::AcroFormLoopDocument doc;
    const std::string pdf = SerializeAcroFormLoopPdf(doc);
    Check(RunXpdfSmoke(argv[1], "release-4.04", pdf,
                       SmokeExpectation::kReleaseVulnerable),
          "xpdf 4.04 release smoke crashes on the AcroForm loop",
          &failures);
    Check(RunXpdfSmoke(argv[2], "asan-4.04", pdf,
                       SmokeExpectation::kAsanVulnerable),
          "xpdf 4.04 ASan smoke reports AcroForm::scanField stack overflow",
          &failures);
    Check(RunXpdfSmoke(argv[3], "asan-4.05", pdf,
                       SmokeExpectation::kAsanFixed),
          "xpdf 4.05 ASan smoke exits cleanly on the same input",
          &failures);
  } else {
    std::fprintf(stderr,
                 "\nsmoke: skipped (pass <xpdf-4.04-release-pdftotext> "
                 "<xpdf-4.04-asan-pdftotext> <xpdf-4.05-asan-pdftotext> to enable)\n");
  }

  google::protobuf::ShutdownProtobufLibrary();
  if (failures) {
    std::fprintf(stderr, "\nverify_acroform_loop: %d failure(s)\n", failures);
    return 1;
  }
  std::fprintf(stderr, "\nverify_acroform_loop: all checks passed\n");
  return 0;
}
