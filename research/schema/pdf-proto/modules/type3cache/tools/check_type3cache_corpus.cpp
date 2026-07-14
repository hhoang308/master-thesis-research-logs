// Validate Type3CacheDocument corpus entries by serializing them to PDF and
// checking structural invariants plus optional external parser acceptance.
//
// Usage: check_type3cache_corpus <dir> [max_samples]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include <google/protobuf/text_format.h>

#include "modules/type3cache/type3_cache.pb.h"
#include "modules/type3cache/type3_cache_serializer.h"

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
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

  size_t p = static_cast<size_t>(off) + 5;
  if (s.compare(p, 2, "0 ") != 0) {
    *why = "bad xref subsection header";
    return false;
  }
  long count = std::atol(s.c_str() + p + 2);
  size_t nl = s.find('\n', p);
  if (nl == std::string::npos) {
    *why = "bad xref header eol";
    return false;
  }
  p = nl + 1;
  for (long i = 0; i < count; ++i, p += 20) {
    if (p + 20 > s.size()) {
      *why = "xref truncated";
      return false;
    }
    if (s[p + 17] == 'n') {
      long obj_off = std::atol(s.substr(p, 10).c_str());
      std::string expect = std::to_string(i) + " 0 obj";
      if (obj_off < 0 || static_cast<size_t>(obj_off) + expect.size() > s.size() ||
          s.compare(static_cast<size_t>(obj_off), expect.size(), expect) != 0) {
        *why = "object xref offset mismatch";
        return false;
      }
    }
  }
  std::string need = "/Size " + std::to_string(count);
  if (s.find(need) == std::string::npos) {
    *why = "/Size != xref count";
    return false;
  }
  return true;
}

int RunExit(const std::string& cmd) {
  int rc = std::system((cmd + " >/dev/null 2>&1").c_str());
  if (rc == -1) return -1;
  return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

bool CommandExists(const char* cmd) {
  std::string probe = "command -v ";
  probe += cmd;
  probe += " >/dev/null 2>&1";
  return std::system(probe.c_str()) == 0;
}

bool IsTxtpb(const char* name) {
  const char* suffix = ".txtpb";
  size_t n = std::strlen(name);
  size_t s = std::strlen(suffix);
  return n >= s && std::strcmp(name + n - s, suffix) == 0;
}

}  // namespace

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <dir> [max_samples]\n", argv[0]);
    return 1;
  }

  const char* dir = argv[1];
  long max_samples = (argc >= 3) ? std::atol(argv[2]) : 0;
  DIR* d = opendir(dir);
  if (!d) {
    std::fprintf(stderr, "cannot open %s\n", dir);
    return 1;
  }

  const bool have_qpdf = CommandExists("qpdf");
  const bool have_mutool = CommandExists("mutool");
  const bool have_pdfinfo = CommandExists("pdfinfo");

  google::protobuf::TextFormat::Parser parser;
  parser.AllowPartialMessage(true);
  parser.AllowUnknownField(true);

  long total = 0;
  long skipped = 0;
  long xref_bad = 0;
  long qpdf_ok = 0;
  long mutool_ok = 0;
  long pdfinfo_ok = 0;
  long exact = 0;
  long eviction_ready = 0;

  struct dirent* e;
  while ((e = readdir(d)) != nullptr) {
    if (e->d_name[0] == '.' || !IsTxtpb(e->d_name)) continue;
    if (max_samples && total >= max_samples) break;

    std::string path = std::string(dir) + "/" + e->d_name;
    pdf_type3cache::Type3CacheDocument doc;
    if (!parser.ParseFromString(ReadFile(path), &doc)) {
      skipped++;
      continue;
    }

    std::string pdf = SerializeType3CachePdf(doc);
    total++;
    if (doc.matrix_mode() == pdf_type3cache::Type3CacheDocument::EXACT_REUSE)
      exact++;
    if (doc.filler_fonts() >= 8 && doc.nested_depth() >= 1 &&
        doc.outer_uses_d1() && doc.inner_uses_d1() && doc.fillers_use_d1() &&
        doc.render_fillers_after_nested())
      eviction_ready++;

    std::string why;
    if (!XrefOk(pdf, &why)) {
      xref_bad++;
      if (xref_bad <= 10) {
        std::fprintf(stderr, "[XREF BUG] %s: %s\n", e->d_name, why.c_str());
      }
    }

    char tmp[] = "/tmp/type3cache_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) continue;
    write(fd, pdf.data(), pdf.size());
    close(fd);

    if (have_qpdf) {
      int q = RunExit(std::string("qpdf --check ") + tmp);
      if (q == 0 || q == 3) qpdf_ok++;
    }
    if (have_mutool && RunExit(std::string("mutool info ") + tmp) == 0) {
      mutool_ok++;
    }
    if (have_pdfinfo &&
        RunExit(std::string("pdfinfo ") + tmp + " 2>/dev/null | grep -q Pages:") ==
            0) {
      pdfinfo_ok++;
    }
    unlink(tmp);
  }
  closedir(d);

  std::printf("\n=== Type3 cache corpus validation (%s) ===\n", dir);
  std::printf("samples serialized:       %ld   skipped parse errors: %ld\n",
              total, skipped);
  std::printf("xref invariant ok:        %ld/%ld\n", total - xref_bad, total);
  std::printf("matrix EXACT_REUSE:       %ld/%ld\n", exact, total);
  std::printf("eviction-ready shape:     %ld/%ld\n", eviction_ready, total);
  if (have_qpdf) std::printf("qpdf accepted:            %ld/%ld\n", qpdf_ok, total);
  if (have_mutool)
    std::printf("mutool accepted:          %ld/%ld\n", mutool_ok, total);
  if (have_pdfinfo)
    std::printf("pdfinfo accepted:         %ld/%ld\n", pdfinfo_ok, total);

  google::protobuf::ShutdownProtobufLibrary();
  return xref_bad ? 2 : 0;
}
