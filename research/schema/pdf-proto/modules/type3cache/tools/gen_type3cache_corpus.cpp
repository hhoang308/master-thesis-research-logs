// Deterministic corpus generator for the Type 3 cache lifecycle grammar.
//
// Usage: gen_type3cache_corpus <out_dir> [count=1000] [seed=12345]
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include <google/protobuf/text_format.h>

#include "modules/type3cache/type3_cache.pb.h"

using pdf_type3cache::Type3CacheDocument;

namespace {

std::mt19937 rng;

int Ri(int lo, int hi) {
  return std::uniform_int_distribution<int>(lo, hi)(rng);
}

bool Rb(double p) {
  return std::uniform_real_distribution<double>(0.0, 1.0)(rng) < p;
}

void MakeTriggerSeed(Type3CacheDocument* doc) {
  doc->set_filler_fonts(8);
  doc->set_nested_depth(1);
  doc->set_matrix_mode(Type3CacheDocument::EXACT_REUSE);
  doc->set_outer_uses_d1(true);
  doc->set_inner_uses_d1(true);
  doc->set_fillers_use_d1(true);
  doc->set_render_fillers_after_nested(true);
  doc->set_glyph_program_style(Type3CacheDocument::RECT_FILL);
  doc->set_page_font_size(72);
  doc->set_glyph_size(1000);
}

void MakeVariant(Type3CacheDocument* doc) {
  doc->set_filler_fonts(Ri(0, 20));
  doc->set_nested_depth(Ri(0, 4));
  doc->set_matrix_mode(static_cast<Type3CacheDocument::MatrixMode>(Ri(0, 2)));
  doc->set_outer_uses_d1(Rb(0.90));
  doc->set_inner_uses_d1(Rb(0.85));
  doc->set_fillers_use_d1(Rb(0.85));
  doc->set_render_fillers_after_nested(Rb(0.80));
  doc->set_glyph_program_style(
      static_cast<Type3CacheDocument::GlyphProgramStyle>(Ri(0, 2)));
  doc->set_page_font_size(static_cast<uint32_t>(Ri(1, 180)));
  doc->set_glyph_size(static_cast<uint32_t>(Ri(200, 3000)));

  // Keep a useful fraction close to the CVE path so corpus validation can see
  // cache reuse + post-nested eviction without depending on rare mutation luck.
  if (Rb(0.35)) {
    doc->set_filler_fonts(static_cast<uint32_t>(Ri(8, 20)));
    doc->set_nested_depth(static_cast<uint32_t>(Ri(1, 3)));
    doc->set_matrix_mode(Type3CacheDocument::EXACT_REUSE);
    doc->set_outer_uses_d1(true);
    doc->set_inner_uses_d1(true);
    doc->set_fillers_use_d1(true);
    doc->set_render_fillers_after_nested(true);
    doc->set_glyph_size(1000);
  }
}

bool WriteTextProto(const std::string& path, const Type3CacheDocument& doc) {
  std::string text;
  google::protobuf::TextFormat::PrintToString(doc, &text);
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << text;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <out_dir> [count=1000] [seed=12345]\n",
                 argv[0]);
    return 1;
  }

  std::string dir = argv[1];
  long count = (argc >= 3) ? std::atol(argv[2]) : 1000;
  unsigned seed = (argc >= 4) ? static_cast<unsigned>(std::atol(argv[3]))
                              : 12345u;
  if (count < 1) count = 1;

  mkdir(dir.c_str(), 0775);
  rng.seed(seed);

  long exact = 0;
  long eviction_ready = 0;
  for (long i = 0; i < count; ++i) {
    Type3CacheDocument doc;
    if (i == 0) {
      MakeTriggerSeed(&doc);
    } else {
      MakeVariant(&doc);
    }
    if (doc.matrix_mode() == Type3CacheDocument::EXACT_REUSE) exact++;
    if (doc.filler_fonts() >= 8 && doc.nested_depth() >= 1 &&
        doc.outer_uses_d1() && doc.inner_uses_d1() && doc.fillers_use_d1() &&
        doc.render_fillers_after_nested()) {
      eviction_ready++;
    }

    char name[64];
    std::snprintf(name, sizeof(name), "type3cache_%06ld.txtpb", i);
    if (!WriteTextProto(dir + "/" + name, doc)) {
      std::fprintf(stderr, "cannot write %s/%s\n", dir.c_str(), name);
      return 1;
    }
  }

  std::printf("wrote %ld Type3CacheDocument seeds to %s\n", count, dir.c_str());
  std::printf("matrix_mode=EXACT_REUSE: %ld/%ld\n", exact, count);
  std::printf("eviction-ready shape:    %ld/%ld\n", eviction_ready, count);

  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
