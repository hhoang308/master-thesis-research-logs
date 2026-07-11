// Coverage driver for the FAIR AFL++-vs-libFuzzer comparison (thesis criterion 2).
//
// Renders one or more PDF FILES through exactly the same xpdf path the fuzzer drives
// (PDFDoc + displayPages + a stream-draining OutputDev), but built with LLVM
// source-based coverage (-fprofile-instr-generate -fcoverage-mapping) and NO fuzzer/
// ASan. Run this over (a) the AFL++ final corpus (raw PDFs) and (b) the libFuzzer
// corpus serialized to PDFs, merge the profiles, and `llvm-cov report` on the xpdf
// source -- giving one identical yardstick (lines/functions/regions) for both.
//
// Usage: cov_driver <pdf_file> [<pdf_file> ...]
#include <cstdio>
#include "GlobalParams.h"
#include "PDFDoc.h"
#include "OutputDev.h"
#include "Stream.h"

class CovOutputDev : public OutputDev {
public:
    GBool upsideDown() override { return gTrue; }
    GBool useDrawChar() override { return gFalse; }
    GBool interpretType3Chars() override { return gFalse; }
    void drawImage(GfxState*, Object*, Stream *str, int, int,
                   GfxImageColorMap*, int*, GBool, GBool) override { drain(str); }
    void drawImageMask(GfxState*, Object*, Stream *str, int, int,
                       GBool, GBool, GBool) override { drain(str); }
private:
    static void drain(Stream *str) {
        if (!str) return;
        str->reset();
        while (str->getChar() != EOF) {}
        str->close();
    }
};

int main(int argc, char** argv) {
    globalParams = new GlobalParams("");
    CovOutputDev dev;
    for (int i = 1; i < argc; i++) {
        PDFDoc *pdf = new PDFDoc(argv[i]);
        if (pdf->isOk()) {
            int n = pdf->getNumPages();
            if (n > 10) n = 10;
            if (n >= 1)
                pdf->displayPages(&dev, NULL, 1, n, 72, 72, 0, gFalse, gTrue, gFalse);
        }
        delete pdf;
    }
    delete globalParams;
    return 0;
}
