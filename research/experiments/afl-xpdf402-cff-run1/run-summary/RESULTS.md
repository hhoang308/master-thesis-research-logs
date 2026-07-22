# afl-xpdf402-cff-run1 — kết quả

Structure-aware CFF/Type1C fuzzing (`pdftops` xpdf 4.02, afl-clang-fast + ASan).

## Campaign
| | |
|---|---|
| Thời lượng | 19h 24m (dừng thủ công bằng SIGINT) |
| Execs | ~4.54M |
| Corpus | 4974 |
| Coverage (edge bitmap) | 14.59% |
| Stability | 100% |
| Crashes lưu | 489 · Hangs: 3 |

## Triage (489 crashes → 2 bug thật)

Pipeline: crash proto (binary) → `proto2pdf_bin` (serialize, đủ cff+type3) → PDF
→ `pdftops` 4.02 ASan → dedup theo frame `FoFiType1C`. 100% reproduce.

| # crash | Bug | Loại | Differential |
|--------:|-----|------|--------------|
| **483** | Stack-overflow — `cvtGlyph` đệ quy (callsubr không giới hạn độ sâu) | CWE-674, DoS | **4.02-only** (4.06 sạch) |
| **6** | **FPE — `Type1COp::toInt` chia-cho-0** (`rat.num/rat.den`, `den=0`) | CWE-369, DoS | **4.02-only** (4.06 sạch) |

## Findings
- `findings/xpdf402-cff-cvtglyph-stackoverflow` (đã có, campaign này xác nhận 483 biến thể).
- `findings/xpdf402-cff-type1cop-toint-divzero` (**MỚI** từ campaign này, 6 biến thể).

## Nhận xét
- Không có bug **4.06-live** trong đợt này — cả hai đều đã vá ở 4.06.
- Structure-aware fuzzing chạm sâu vào CFF: tìm được bug FPE mà byte-level (`cve35376-seed`)
  trước đó không ra.
