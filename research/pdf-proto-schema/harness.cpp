#include <cstdio>
#include <unistd.h>
#include "libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h"
#include "pdf.pb.h"
#include "serializer.h"
#include "GlobalParams.h"
#include "PDFDoc.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    globalParams = new GlobalParams("");
    return 0;
}

DEFINE_PROTO_FUZZER(const PdfDocument& doc) {
    if (doc.pages_size() == 0) return;

    std::string bytes = SerializePdf(doc);

    char path[] = "/tmp/pdf_fuzz_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return;
    if (write(fd, bytes.data(), bytes.size()) != (ssize_t)bytes.size()) {
        close(fd); unlink(path); return;
    }
    close(fd);

    PDFDoc *pdf = new PDFDoc(path);
    if (pdf->isOk()) {
        pdf->getNumPages();
    }
    delete pdf;
    unlink(path);
}
