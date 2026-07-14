// Convert a text-format Type3CacheDocument proto into a raw PDF.
//
// Usage: type3cache2pdf <input.txtpb> [output.pdf]
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <google/protobuf/text_format.h>

#include "modules/type3cache/type3_cache.pb.h"
#include "modules/type3cache/type3_cache_serializer.h"

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <input.txtpb> [output.pdf]\n", argv[0]);
    return 1;
  }

  std::ifstream in(argv[1], std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }
  std::ostringstream ss;
  ss << in.rdbuf();

  google::protobuf::TextFormat::Parser parser;
  parser.AllowPartialMessage(true);
  parser.AllowUnknownField(true);
  pdf_type3cache::Type3CacheDocument doc;
  if (!parser.ParseFromString(ss.str(), &doc)) {
    std::fprintf(stderr,
                 "warning: text-proto parse had errors; serializing what parsed\n");
  }

  std::string pdf = SerializeType3CachePdf(doc);
  if (argc >= 3) {
    std::ofstream out(argv[2], std::ios::binary);
    if (!out) {
      std::fprintf(stderr, "cannot write %s\n", argv[2]);
      return 1;
    }
    out.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
    std::fprintf(stderr, "wrote %zu bytes to %s\n", pdf.size(), argv[2]);
  } else {
    std::cout.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
  }

  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
