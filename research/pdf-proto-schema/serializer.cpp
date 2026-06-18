#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <zlib.h>
#include "pdf.pb.h"

struct XrefEntry {
  uint32_t offset;
  uint32_t generation;
  bool in_use;
};

// Compress src with zlib (deflate). Returns empty string on failure.
static std::string zlib_compress(const std::string& src) {
  uLongf bound = compressBound((uLong)src.size());
  std::string dst(bound, '\0');
  int rc = compress2(
      reinterpret_cast<Bytef*>(&dst[0]), &bound,
      reinterpret_cast<const Bytef*>(src.data()), (uLong)src.size(),
      Z_DEFAULT_COMPRESSION);
  if (rc != Z_OK) return {};
  dst.resize(bound);
  return dst;
}

std::string SerializePdf(const pdf_proto::PdfDocument& doc) {
  std::ostringstream out;
  std::vector<XrefEntry> xref;

  // object 0 is always the free-list head
  xref.push_back({0, 65535, false});

  auto record_offset = [&]() -> uint32_t {
    uint32_t pos = (uint32_t)out.tellp();
    xref.push_back({pos, 0, true});
    return (uint32_t)xref.size() - 1;
  };

  out << "%PDF-1.4\n";

  // Pre-assign content-stream object numbers.
  // Layout: obj1=Catalog, obj2=Pages, obj3..2+N=Page objects, then content streams.
  int page_count = doc.pages_size();
  int cs_base = 3 + page_count;
  std::vector<int> cs_obj(page_count, 0);
  {
    int next = cs_base;
    for (int i = 0; i < page_count; i++) {
      if (doc.pages(i).has_content_stream())
        cs_obj[i] = next++;
    }
  }

  // Object 1: Catalog
  record_offset();
  out << "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

  // Object 2: Pages (page tree root)
  record_offset();
  out << "2 0 obj\n<< /Type /Pages /Count " << page_count << " /Kids [";
  for (int i = 0; i < page_count; i++)
    out << (3 + i) << " 0 R ";
  out << "] >>\nendobj\n";

  // Objects 3..2+N: Page objects
  for (int i = 0; i < page_count; i++) {
    record_offset();
    const pdf_proto::Page& p = doc.pages(i);
    float w = p.has_width()  ? p.width()  : 612.0f;
    float h = p.has_height() ? p.height() : 792.0f;
    out << (3 + i) << " 0 obj\n"
        << "<< /Type /Page /Parent 2 0 R"
        << " /MediaBox [0 0 " << w << " " << h << "]";
    if (cs_obj[i] != 0)
      out << " /Contents " << cs_obj[i] << " 0 R";
    out << " >>\nendobj\n";
  }

  // Content-stream objects
  for (int i = 0; i < page_count; i++) {
    if (cs_obj[i] == 0) continue;
    record_offset();
    const pdf_proto::ContentStream& cs = doc.pages(i).content_stream();
    const std::string& raw = cs.raw_content();

    bool want_flate = (cs.filter() == pdf_proto::ContentStream::FLATE);
    bool do_compress = want_flate && !cs.skip_compression();

    std::string compressed;
    if (do_compress) {
      compressed = zlib_compress(raw);
      if (compressed.empty())
        do_compress = false;  // fall back to raw on compress failure
    }

    const std::string& payload = do_compress ? compressed : raw;

    // PDF spec 7.3.8.1: /Length is the byte count between the line ending
    // after "stream" and the keyword "endstream" (not counting the final \n).
    // length_delta shifts the written value to produce over-read (positive) or
    // under-read (negative) -- exercises stream boundary error handling in parsers.
    long written_len = (long)payload.size() + cs.length_delta();
    out << cs_obj[i] << " 0 obj\n<< /Length " << written_len;
    // Declare /Filter /FlateDecode even when skip_compression=true so xpdf feeds
    // the raw (non-zlib) bytes into its FlateStream decoder -- hitting corrupt-data paths.
    if (want_flate)
      out << " /Filter /FlateDecode";
    out << " >>\nstream\n"
        << payload
        << "\nendstream\nendobj\n";
  }

  // Xref table
  uint32_t xref_offset = (uint32_t)out.tellp();
  out << "xref\n0 " << xref.size() << "\n";
  for (auto& e : xref) {
    char buf[21];
    if (e.in_use)
      snprintf(buf, sizeof(buf), "%010u %05u n \n", e.offset, e.generation);
    else
      snprintf(buf, sizeof(buf), "%010u %05u f \n", e.offset, e.generation);
    out << buf;
  }

  // Trailer
  out << "trailer\n"
      << "<< /Size " << xref.size() << " /Root 1 0 R >>\n"
      << "startxref\n"
      << xref_offset << "\n"
      << "%%EOF\n";

  return out.str();
}
