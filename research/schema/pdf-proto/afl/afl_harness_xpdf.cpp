// AFL++ target harness for xpdf (structure-aware PDF fuzzing).
//
// Built with afl-clang-fast++ so xpdf is instrumented for coverage AND this harness gets
// the persistent-mode runtime + fork server. The input it receives is already a PDF (the
// custom mutator's afl_custom_post_process turned the proto into PDF bytes).
//
// Two rendering modes (compile-time):
//   default            -- minimal FuzzOutputDev: runs content streams + drains image streams
//                         (fonts load, decoders run) but does NOT interpret Type 3 char procs.
//                         Fast; good for parser/decoder bugs. Non-ASan.
//   -DAFL_SPLASH_ASAN  -- real SplashOutputDev rasterizer: interprets Type 3 char procs so
//                         SplashOutputDev::begin/endType3Char run. Required to reach the Type 3
//                         font-cache path (e.g. CVE-2020-25725 heap-UAF in endType3Char), which
//                         only surfaces under ASan. Build this variant WITH ASan.
#include <cstdio>
#include <cstring>
#include "GlobalParams.h"
#include "PDFDoc.h"
#include "Object.h"
#include "Stream.h"
#ifdef AFL_SPLASH_ASAN
#include "SplashOutputDev.h"
#else
#include "OutputDev.h"

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
#endif  // AFL_SPLASH_ASAN

__AFL_FUZZ_INIT();

int main() {
    globalParams = new GlobalParams("");

    // Deferred fork server: do one-time init (GlobalParams) before forking, then the fork
    // server snapshots here -- each iteration skips that setup cost.
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
#ifdef AFL_SPLASH_ASAN
                // Real rasterizer: interprets Type 3 char procs -> begin/endType3Char runs.
                SplashColor paperColor;
                paperColor[0] = paperColor[1] = paperColor[2] = 0xff;
                SplashOutputDev splashOut(splashModeRGB8, 1, gFalse, paperColor);
                splashOut.startDoc(doc->getXRef());
                for (int pg = 1; pg <= n; ++pg) {
#ifdef XPDF_LEGACY_DISPLAYPAGES
                    // xpdf <= 4.02: displayPage has NO LocalParams* parameter.
                    doc->displayPage(&splashOut, pg, 72, 72, 0,
                                     gFalse, gTrue, gFalse);  // useMediaBox, crop, printing
#else
                    doc->displayPage(&splashOut, NULL, pg, 72, 72, 0,
                                     gFalse, gTrue, gFalse);
#endif
                }
#else
                FuzzOutputDev dev;
#ifdef XPDF_LEGACY_DISPLAYPAGES
                // xpdf <= 4.02: displayPages has NO LocalParams* parameter.
                doc->displayPages(&dev, 1, n, 72, 72, 0,
                                  gFalse, gTrue, gFalse);  // useMediaBox, crop, printing
#else
                doc->displayPages(&dev, NULL, 1, n, 72, 72, 0,
                                  gFalse, gTrue, gFalse);
#endif
#endif  // AFL_SPLASH_ASAN
            }
        }
        delete doc;                              // also frees str
    }
    return 0;
}
