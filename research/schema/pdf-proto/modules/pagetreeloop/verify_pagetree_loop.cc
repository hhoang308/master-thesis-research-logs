#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "modules/pagetreeloop/page_tree_loop.pb.h"
#include "modules/pagetreeloop/page_tree_loop_serializer.h"

namespace {

struct PageNodeInfo {
  int obj_num = 0;
  int kid_ref = 0;
  int count = 0;
};

struct ParseResult {
  bool ok = false;
  std::string error;
  pdf_pagetreeloop::PageTreeLoopDocument doc;
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
  char tmp[] = "/tmp/pagetreeloop_verify_XXXXXX";
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

bool ParseCatalogPagesRef(const std::string& body, int* ref) {
  const std::string key = "/Pages ";
  size_t pos = body.find(key);
  if (pos == std::string::npos) {
    return false;
  }
  pos += key.size();
  size_t end = body.find(" 0 R", pos);
  if (end == std::string::npos) {
    return false;
  }
  return ParseNonNegativeIntToken(Trim(body.substr(pos, end - pos)), ref);
}

bool ParsePageNode(const std::string& body, PageNodeInfo* info) {
  if (!Contains(body, "/Type /Pages")) {
    return false;
  }

  const std::string kids_key = "/Kids [";
  size_t kids = body.find(kids_key);
  if (kids == std::string::npos) {
    return false;
  }
  kids += kids_key.size();
  size_t kids_end = body.find(" 0 R", kids);
  if (kids_end == std::string::npos ||
      !ParseNonNegativeIntToken(Trim(body.substr(kids, kids_end - kids)),
                                &info->kid_ref)) {
    return false;
  }

  const std::string count_key = "/Count ";
  size_t count = body.find(count_key);
  if (count == std::string::npos) {
    return false;
  }
  count += count_key.size();
  size_t count_end = body.find_first_of(" \n>", count);
  if (count_end == std::string::npos) {
    count_end = body.size();
  }
  return ParseNonNegativeIntToken(Trim(body.substr(count, count_end - count)),
                                  &info->count);
}

ParseResult ParseSerializedPdf(const std::string& pdf) {
  ParseResult result;
  if (!Contains(pdf, "%PDF-1.4")) {
    result.error = "missing %PDF-1.4 header";
    return result;
  }

  std::string error;
  const std::string catalog_body = ExtractObjectBody(pdf, 1, &error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }

  int root_ref = 0;
  if (!ParseCatalogPagesRef(catalog_body, &root_ref)) {
    result.error = "missing or malformed catalog /Pages reference";
    return result;
  }
  if (root_ref != 2) {
    result.error = "unsupported root /Pages reference";
    return result;
  }

  std::vector<PageNodeInfo> nodes;
  std::vector<int> order;
  int current = root_ref;
  int common_count = -1;
  for (int step = 0; step < 64; ++step) {
    auto seen = std::find(order.begin(), order.end(), current);
    if (seen != order.end()) {
      if (seen != order.begin()) {
        result.error = "page-tree loop does not close back to the root node";
        return result;
      }
      break;
    }

    PageNodeInfo node;
    node.obj_num = current;
    const std::string body = ExtractObjectBody(pdf, current, &error);
    if (!error.empty()) {
      result.error = error;
      return result;
    }
    if (!ParsePageNode(body, &node)) {
      result.error = "page-tree node " + std::to_string(current) +
                     " missing /Type /Pages, /Kids, or /Count";
      return result;
    }
    if (common_count == -1) {
      common_count = node.count;
    } else if (node.count != common_count) {
      result.error = "page-tree nodes do not share a consistent /Count";
      return result;
    }
    order.push_back(current);
    nodes.push_back(node);
    current = node.kid_ref;
  }

  if (order.empty()) {
    result.error = "no /Pages nodes reachable from the catalog";
    return result;
  }
  if (current != root_ref) {
    result.error = "page-tree traversal exceeded parser limit without a cycle";
    return result;
  }

  result.doc.set_loop_nodes(static_cast<uint32_t>(nodes.size()));
  if (nodes.size() == 1) {
    result.doc.set_loop_shape(
        pdf_pagetreeloop::PageTreeLoopDocument::SELF_LOOP);
  } else if (nodes.size() == 2) {
    result.doc.set_loop_shape(
        pdf_pagetreeloop::PageTreeLoopDocument::MUTUAL_LOOP);
  } else {
    result.doc.set_loop_shape(pdf_pagetreeloop::PageTreeLoopDocument::CYCLE);
  }

  if (common_count == 0) {
    result.doc.set_count_mode(
        pdf_pagetreeloop::PageTreeLoopDocument::ZERO_COUNT);
    result.doc.set_huge_count(0);
  } else if (common_count > 50000) {
    result.doc.set_count_mode(
        pdf_pagetreeloop::PageTreeLoopDocument::HUGE_COUNT);
    result.doc.set_huge_count(static_cast<uint32_t>(common_count));
  } else {
    result.error =
        "page-tree /Count does not match a supported countPageTree trigger";
    return result;
  }

  result.ok = true;
  return result;
}

bool StructuralInvariants(const pdf_pagetreeloop::PageTreeLoopDocument& doc,
                          std::string* why) {
  switch (doc.loop_shape()) {
    case pdf_pagetreeloop::PageTreeLoopDocument::SELF_LOOP:
      if (doc.loop_nodes() != 1) {
        *why = "SELF_LOOP must have exactly one /Pages node";
        return false;
      }
      break;
    case pdf_pagetreeloop::PageTreeLoopDocument::MUTUAL_LOOP:
      if (doc.loop_nodes() != 2) {
        *why = "MUTUAL_LOOP must have exactly two /Pages nodes";
        return false;
      }
      break;
    case pdf_pagetreeloop::PageTreeLoopDocument::CYCLE:
      if (doc.loop_nodes() < 3) {
        *why = "CYCLE must have at least three /Pages nodes";
        return false;
      }
      break;
    default:
      *why = "unsupported loop_shape enum";
      return false;
  }

  switch (doc.count_mode()) {
    case pdf_pagetreeloop::PageTreeLoopDocument::ZERO_COUNT:
      if (doc.huge_count() != 0) {
        *why = "ZERO_COUNT must canonicalize huge_count to zero";
        return false;
      }
      break;
    case pdf_pagetreeloop::PageTreeLoopDocument::HUGE_COUNT:
      if (doc.huge_count() <= 50000) {
        *why = "HUGE_COUNT must be greater than 50000";
        return false;
      }
      break;
    default:
      *why = "unsupported count_mode enum";
      return false;
  }
  return true;
}

bool ContentAssertions(const std::string& pdf,
                       const pdf_pagetreeloop::PageTreeLoopDocument& canon,
                       std::string* why) {
  if (!Contains(pdf, "%PDF-1.4") ||
      !Contains(pdf, "/Type /Catalog /Pages 2 0 R") ||
      !Contains(pdf, "/Type /Pages") ||
      !Contains(pdf, "/Kids [")) {
    *why = "serialized PDF missing required page-tree loop tokens";
    return false;
  }

  const std::string count_token =
      canon.count_mode() ==
              pdf_pagetreeloop::PageTreeLoopDocument::HUGE_COUNT
          ? "/Count " + std::to_string(canon.huge_count())
          : "/Count 0";
  if (!Contains(pdf, count_token)) {
    *why = "serialized PDF missing the expected /Count trigger token";
    return false;
  }

  if (canon.loop_shape() ==
          pdf_pagetreeloop::PageTreeLoopDocument::SELF_LOOP &&
      !Contains(pdf, "/Kids [2 0 R]")) {
    *why = "default self-loop is missing its self-referential /Kids edge";
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

bool QpdfMeaningful(const std::string& pdf, std::string* why) {
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
  if (code == 0) {
    *why = "qpdf accepted the serialized PDF";
    return true;
  }
  if (Contains(result.output, "Loop detected in /Pages structure")) {
    *why = "qpdf reported the expected /Pages loop diagnostic";
    return true;
  }
  *why = "qpdf rejected the PDF without a meaningful /Pages-loop diagnostic";
  return false;
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
      std::string("/tmp/pagetreeloop_smoke_") + label + ".txt";

  std::string inner = "ulimit -s 256; ";
  if (expectation != SmokeExpectation::kReleaseVulnerable) {
    inner += "ASAN_OPTIONS=detect_leaks=0 ";
  }
  inner += "timeout 10s " + ShellQuote(pdftotext) + " " + ShellQuote(pdf_path) +
           " " + ShellQuote(out_path);

  CommandResult run = RunCommandCapture("bash -lc " + ShellQuote(inner));
  unlink(pdf_path.c_str());

  const int exit_code = run.status != -1 && WIFEXITED(run.status)
                            ? WEXITSTATUS(run.status)
                            : -1;
  const bool saw_asan = Contains(run.output, "AddressSanitizer");
  const bool saw_stack = Contains(run.output, "stack-overflow");
  const bool saw_count_page_tree =
      Contains(run.output, "Catalog::countPageTree");
  const bool saw_loop_error = Contains(run.output, "Loop in Pages tree");
  const bool segv = exit_code == 139 || Contains(run.output, "Segmentation fault");

  std::fprintf(stderr,
               "xpdf smoke %-12s exit=%d segv=%d asan=%d stack=%d "
               "countPageTree=%d fixed_loop_error=%d\n",
               label.c_str(), exit_code, segv ? 1 : 0, saw_asan ? 1 : 0,
               saw_stack ? 1 : 0, saw_count_page_tree ? 1 : 0,
               saw_loop_error ? 1 : 0);

  switch (expectation) {
    case SmokeExpectation::kReleaseVulnerable:
      return segv;
    case SmokeExpectation::kAsanVulnerable:
      return saw_asan && saw_stack && saw_count_page_tree;
    case SmokeExpectation::kAsanFixed:
      return exit_code == 0 && !saw_asan && saw_loop_error;
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
             const pdf_pagetreeloop::PageTreeLoopDocument& doc,
             bool expect_default_shape) {
  std::fprintf(stderr, "\n=== %s ===\n", name);
  const pdf_pagetreeloop::PageTreeLoopDocument canon =
      CanonicalizePageTreeLoopDocument(doc);
  const std::string pdf = SerializePageTreeLoopPdf(doc);

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

  const pdf_pagetreeloop::PageTreeLoopDocument reparsed =
      CanonicalizePageTreeLoopDocument(parsed.doc);
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
  if (!QpdfMeaningful(pdf, &why)) {
    std::fprintf(stderr, "FAIL: qpdf sanity: %s\n", why.c_str());
    return false;
  }
  std::fprintf(stderr, "qpdf parser: %s\n", why.c_str());

  if (expect_default_shape &&
      !(canon.loop_shape() ==
            pdf_pagetreeloop::PageTreeLoopDocument::SELF_LOOP &&
        canon.loop_nodes() == 1 &&
        canon.count_mode() ==
            pdf_pagetreeloop::PageTreeLoopDocument::ZERO_COUNT)) {
    std::fprintf(stderr, "FAIL: expected default CVE-2019-9587 shape missing\n");
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
    pdf_pagetreeloop::PageTreeLoopDocument doc;
    failures += RunCase("default-cve-2019-9587-shape", doc, true) ? 0 : 1;
  }

  {
    pdf_pagetreeloop::PageTreeLoopDocument doc;
    doc.set_loop_shape(pdf_pagetreeloop::PageTreeLoopDocument::MUTUAL_LOOP);
    doc.set_loop_nodes(99);
    doc.set_count_mode(pdf_pagetreeloop::PageTreeLoopDocument::HUGE_COUNT);
    doc.set_huge_count(60000);
    failures += RunCase("mutual-loop-huge-count", doc, false) ? 0 : 1;
  }

  {
    pdf_pagetreeloop::PageTreeLoopDocument doc;
    doc.set_loop_shape(pdf_pagetreeloop::PageTreeLoopDocument::CYCLE);
    doc.set_loop_nodes(5);
    doc.set_count_mode(pdf_pagetreeloop::PageTreeLoopDocument::HUGE_COUNT);
    doc.set_huge_count(42);
    failures += RunCase("five-node-cycle-canonicalizes-huge-count", doc, false)
                    ? 0
                    : 1;
  }

  if (argc >= 4) {
    pdf_pagetreeloop::PageTreeLoopDocument doc;
    const std::string pdf = SerializePageTreeLoopPdf(doc);
    Check(RunXpdfSmoke(argv[1], "release-4.04", pdf,
                       SmokeExpectation::kReleaseVulnerable),
          "xpdf 4.04 release smoke crashes on the page-tree loop",
          &failures);
    Check(RunXpdfSmoke(argv[2], "asan-4.04", pdf,
                       SmokeExpectation::kAsanVulnerable),
          "xpdf 4.04 ASan smoke reports Catalog::countPageTree stack overflow",
          &failures);
    Check(RunXpdfSmoke(argv[3], "asan-4.05", pdf,
                       SmokeExpectation::kAsanFixed),
          "xpdf 4.05 ASan smoke exits cleanly with Loop in Pages tree",
          &failures);
  } else {
    std::fprintf(stderr,
                 "\nsmoke: skipped (pass <xpdf-4.04-release-pdftotext> "
                 "<xpdf-4.04-asan-pdftotext> <xpdf-4.05-asan-pdftotext> to "
                 "enable)\n");
  }

  google::protobuf::ShutdownProtobufLibrary();
  if (failures) {
    std::fprintf(stderr, "\nverify_pagetreeloop: %d failure(s)\n", failures);
    return 1;
  }
  std::fprintf(stderr, "\nverify_pagetreeloop: all checks passed\n");
  return 0;
}
