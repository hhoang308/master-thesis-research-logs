// AFL++ target harness for xpdf 4.06 (structure-aware PDF fuzzing).
//
// Built with afl-clang-fast++ so xpdf is instrumented for coverage AND this harness
// gets the persistent-mode runtime + fork server. The input it receives is already a
// PDF (the custom mutator's afl_custom_post_process turned the proto into PDF bytes).
//
// Reads the PDF from memory via MemStream (no temp file -> fast persistent loop), then
// renders pages through FuzzOutputDev so content streams execute: Tf -> GfxFont::makeFont
// (-> FoFi parsers), Do -> Gfx::doImage (-> image decoders). Pixels are discarded.
#include <cstdio>
#include <cstring>
#include "GlobalParams.h"
#include "PDFDoc.h"
#include "OutputDev.h"
#include "Stream.h"
#include "Object.h"

// Minimal rendering device: force content execution + drain image streams (decoders run).
class FuzzOutputDev : public OutputDev {
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

__AFL_FUZZ_INIT();

int main() {
    globalParams = new GlobalParams("");

    // Deferred fork server: do one-time init (GlobalParams) before forking, then the
    // fork server snapshots here -- each iteration skips that setup cost.
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        if (len < 10) continue;                 // too small to be a PDF

        Object dict;
        dict.initNull();
        // MemStream references buf (no copy); PDFDoc takes ownership of the stream.
        BaseStream *str = new MemStream((char *)buf, 0, (Guint)len, &dict);
        PDFDoc *doc = new PDFDoc(str);
        if (doc->isOk()) {
            int n = doc->getNumPages();
            if (n > 10) n = 10;                  // bound per-input work
            if (n >= 1) {
                FuzzOutputDev dev;
#ifdef XPDF_LEGACY_DISPLAYPAGES
                doc->displayPages(&dev, 1, n, 72, 72, 0,
                                  gFalse, gTrue, gFalse);  // useMediaBox, crop, printing
#else
                doc->displayPages(&dev, NULL, 1, n, 72, 72, 0,
                                  gFalse, gTrue, gFalse);  // useMediaBox, crop, printing
#endif
            }
        }
        delete doc;                              // also frees str
    }
    return 0;
}
