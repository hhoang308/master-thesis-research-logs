#include <cstdio>
#include <unistd.h>

#include "libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h"
#include "pdf.pb.h"
#include "serializer.h"
#include "GlobalParams.h"
#include "PDFDoc.h"
#include "SplashOutputDev.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    globalParams = new GlobalParams("");
    return 0;
}

DEFINE_PROTO_FUZZER(const pdf_proto::PdfDocument& doc) {
    if (doc.pages_size() == 0 && doc.type3_cache_programs_size() == 0) return;

    std::string bytes = SerializePdf(doc);

    char path[] = "/tmp/pdf_splash_fuzz_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return;
    if (write(fd, bytes.data(), bytes.size()) != (ssize_t)bytes.size()) {
        close(fd); unlink(path); return;
    }
    close(fd);

    PDFDoc *pdf = new PDFDoc(path);
    if (pdf->isOk()) {
        SplashColor paperColor;
        paperColor[0] = paperColor[1] = paperColor[2] = 0xff;
        SplashOutputDev splashOut(splashModeRGB8, 1, gFalse, paperColor);
        splashOut.startDoc(pdf->getXRef());

        int n = pdf->getNumPages();
        if (n > 3) n = 3;
        for (int pg = 1; pg <= n; ++pg) {
            pdf->displayPage(&splashOut, pg, 72, 72, 0, gFalse, gTrue, gFalse);
        }
    }
    delete pdf;
    unlink(path);
}
