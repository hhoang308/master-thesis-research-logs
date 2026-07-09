## Mục tiêu luận văn

**Đóng góp chính (sau khi thống nhất với GVHD, 2026-06):** thiết kế một **grammar/schema PDF
có định hướng theo LỚP ĐIỂM YẾU** — *weakness-class-directed grammar* (ý tưởng "attack-pattern
based" của thầy). Khác với grammar structure-aware *thông thường* (AFLSmart, Nautilus,
FormatFuzzer) vốn chỉ lo giữ input **hợp lệ về cú pháp**, grammar ở đây được thiết kế để **chủ
động sinh ra input chạm tới / kích hoạt các LỚP lỗi bảo mật đã biết** (integer/size overflow,
use-after-free / tham chiếu object đã xoá, parameter tampering / type confusion, dữ liệu rác cho
decoder...).

> **Về tên gọi:** patterns nhắm vào **lớp điểm yếu (CWE class)**, không phải một CVE cụ thể —
> CVE chỉ là *ví dụ mẫu* để rút ra pattern. Vì vậy gọi là **"weakness-class-directed grammar"**
> (hoặc "attack-pattern-driven, weakness-class-directed") để nhấn mạnh tính **tổng quát hoá**
> trong một lớp lỗi, tránh hiểu nhầm là chỉ "replay" đúng CVE cũ (overfitting).

**Engine (libFuzzer / AFL++ + libprotobuf-mutator) chỉ là HẠ TẦNG** để chạy grammar, KHÔNG phải
đóng góp của luận văn. Có thể hoán đổi; chọn cái nào chạy ổn + so sánh được với baseline.

**Định nghĩa "grammar hiệu quả" + CÁCH ĐO (quan trọng — phải định lượng được):**
1. **Rediscovery CVE đã biết** trên các phiên bản *cũ, có lỗi* của xpdf/PoDoFo/poppler — thí
   nghiệm thuyết phục nhất: grammar weakness-directed chạm/tái hiện CVE **nhanh hơn / ổn định
   hơn** AFL++ thuần.
2. **Coverage của các hàm/parser NHẠY CẢM bảo mật** (font/image/xref/objstm parser), KHÔNG phải
   tổng coverage (tổng coverage khó hơn baseline vì real-world seed có đủ feature).
3. **Tỉ lệ input hợp lệ (valid-input rate)** — hiện ~89% (qpdf nghiêm ngặt).
4. (phụ) So coverage / số bug với AFL++ baseline trên cùng target.

**Đủ để bảo vệ thạc sĩ.** Nếu tìm được CVE mới trong xpdf/PoDoFo/qpdf → có thể nâng thành
workshop/short paper.

### Phạm vi (CHỐT 2026-06-26)

- **Target chính (reproduction): xpdf 4.02** — còn ~20 CVE parse-path chưa vá → benchmark giàu
  để chứng minh grammar tái hiện được lỗi đã biết.
- **Target "đã vá cứng" (săn lỗi mới): xpdf 4.06** — gần như mọi CVE cũ đã vá (chỉ còn
  CVE-2026-4407) → crash ở đây = lỗi mới.
- **Target phụ (transfer, optional): PoDoFo** — chạy cùng grammar để chứng minh tổng quát hoá
  (đã có harness + 1 finding). qpdf: bỏ/để sau (giảm phạm vi cho khả thi).
- **Benchmark: ~8–10 CVE** trải 4 lớp điểm yếu (A int-overflow / B OOB / C corrupt-decoder /
  E DoS object-loop), chi tiết ở `research/xpdf-cve-benchmark.md`. 7 CVE chạm được ngay với
  schema hiện tại; phần còn lại mở khoá theo roadmap grammar (P2 / ObjStm / ColorSpace / JBIG2).
- **Tiêu chí "tái hiện CVE":** signature ASan khớp hàm/CWE của CVE **VÀ** crash bản 4.02 nhưng
  không crash bản đã vá (không phải "có crash bất kỳ").
- **Engine:** hạ tầng, không phải đóng góp; libFuzzer đang chạy được, AFL++ để so cùng-engine.

---

## Phương pháp (3 lớp — chỉ lớp 1 là đóng góp)

```
pdf.proto (GRAMMAR)  ->  LPM mutate PdfDocument  ->  SerializePdf()  ->  raw PDF  ->  xpdf / PoDoFo / qpdf
   (đóng góp)              (engine/hạ tầng)         (enabler)            (target)
```

1. **Grammar / schema (`pdf.proto`) — ĐÓNG GÓP CHÍNH.** Mô tả cấu trúc PDF *và* mã hoá sẵn các
   pattern điểm yếu vào các field (xem mục "Bản đồ pattern" bên dưới).
2. **Serializer (`serializer.cpp`) — enabler.** Chuyển protobuf message -> raw PDF bytes, **tự
   tính xref offset và `/Length`** (điểm khác AFLSmart và chunk-level tool). Đã kiểm chứng kỹ.
3. **Engine host — hạ tầng.** libprotobuf-mutator chạy trên libFuzzer (`DEFINE_PROTO_FUZZER`)
   hoặc AFL++ (custom mutator). Chi tiết ở mục "Hạ tầng engine".

**Lý do bỏ phương án ban đầu (AFL++ + lopdf IR):** lopdf round-trip chỉ ~60% (không tin cậy);
tự viết L1/L2/L3 mutation cùng parser quá lớn; phương án protobuf tách bạch schema + serializer
+ LPM, chỉ còn 1 bài toán khó là serializer.

---

## ★ TRÁI TIM LUẬN VĂN: Bản đồ Attack-pattern → Grammar field → CWE/CVE

Mỗi field "bất thường" trong schema KHÔNG phải ngẫu nhiên mà ứng với một **lớp điểm yếu** và có
**nguồn gốc CVE**. Đây là nội dung chính của Chương 4.

| Lớp điểm yếu (CWE) | Pattern trong grammar | Field schema | CVE tham chiếu | Trạng thái |
|---|---|---|---|---|
| **Integer / size overflow** (CWE-190, CWE-789) | giá trị kích thước/đếm rất lớn, âm, 0 | `ImageXObject.width/height/bits_per_component`; `ContentStream.length_delta` | (PoDoFo alloc-size — finding của ta) | ✅ một phần |
| ↳ cùng lớp, theo CVE cụ thể | `/N` của `/ObjStm`, `/Index` của `/XRef` | **[TODO]** ObjStm/XRef stream object | CVE-2009-3608 (xpdf `/ObjStm /N`), CVE-2017-15587 (MuPDF `/XRef /Index`) | ⏳ |
| **Use-after-free / tham chiếu object đã xoá** (CWE-416) | `/Contents`, `/Font`, `/Kids` trỏ tới object **không tồn tại / đã free**; recursive ref | **[TODO P2]** `dangling_refs`, free-object trong xref, `parent_ref` vòng | CVE-2025-52885 (poppler UAF `StructTreeRoot::parseNumberTreeNode`) | ⏳ **ưu tiên cao** |
| **Parameter tampering / type confusion** (CWE-843) | giá trị "gần hợp lệ" sai kiểu, sai `/Subtype`, thiếu key bắt buộc, chèn delimiter | `omit_type/omit_subtype` (đã có); **[TODO]** near-valid token injection | CVE-2024-4367 (PDF.js `/FontMatrix` injection) | ⏳ một phần |
| **Corrupt-decoder input** | byte rác đưa vào decoder dù header hợp lệ | `skip_compression` (Flate rác); `ImageXObject.data` rác cho DCT/JPX/CCITT/LZW | (lớp lỗi parser ảnh/filter nói chung) | ✅ |
| **(optional) Code/JS injection** | JS trong `/OpenAction`, `/AA`; `/FontMatrix` string | **[TODO]** Action / FontMatrix string | CVE-2024-4367 | ⏳ optional |

**Chống "overfitting":** pattern là **mẫu của LỚP lỗi**, không phải replay 1 CVE. VD pattern
integer-overflow nhắm *mọi* field kích thước/đếm, không chỉ field của 1 CVE. Bằng chứng tổng
quát hoá: finding PoDoFo (alloc-size) **không** đến từ một CVE PDF cụ thể nào.

---

## Hiện trạng grammar (đã implement — kèm lý do attack-pattern)

| Object | Pattern điểm yếu phục vụ | Trạng thái |
|---|---|---|
| Skeleton (Catalog/Pages/Page, MediaBox) | nền tảng | ✅ |
| ContentStream + 4 gap-fix (`length_delta`, `skip_compression`, bỏ clamp MediaBox, `repeated`) | size overflow, corrupt-decoder, geometry overflow | ✅ |
| Font đầy đủ (P1 dummy → P1.5 nhúng FontFile/FoFi → StepA Encoding/Widths/ToUnicode → StepB CID/Type0) | parser font (FoFiType1/TrueType/CFF), CMap, CID width | ✅ |
| Image XObject (DCT/JPX/CCITT/LZW) | image decoder (JPEG2000 nhiều CVE), size overflow | ✅ |

**Kiểm chứng serializer (`check_serializer`, nhiều oracle):** xref byte-offset đúng **2000/2000
(100%)** trên proto đa dạng (sinh bằng `gen_corpus`, có histogram đo độ đa dạng); valid-rate
ngoài: poppler/mupdf 100%, **qpdf nghiêm ngặt ~89%** (phần còn lại do malformation cố ý). Bài
học: không đo "hợp lệ" bằng 1 parser khoan dung (pdfinfo).

---

## Việc tiếp theo (ưu tiên theo framing mới — grammar là trọng tâm)

- [ ] **[CWE-416] P2 — UAF / tham chiếu object đã xoá**: thêm dangling refs (`/Contents`,`/Font`
      trỏ object không có trong xref), free-object bị reference, recursive `parent`/`kids`. *(thiếu
      rõ nhất, làm trước)*
- [ ] **[CWE-190] Integer-overflow theo CVE**: model `/ObjStm` (`/N`,`/First`) và `/XRef` stream
      (`/Index`,`/W`) để tái hiện CVE-2009-3608, CVE-2017-15587.
- [ ] **[CWE-843] Parameter tampering**: near-valid token injection (chèn `(` `{` `;` `<<` sau
      giá trị số/name) — đã defer trước đây, giờ là nội dung chính.
- [ ] **Đánh giá hiệu quả**: dựng phiên bản *cũ có CVE* của xpdf/PoDoFo/poppler; đo
      rediscovery + coverage hàm nhạy cảm so với AFL++ thuần (lặp 3 lần, mean/stddev).
- [ ] **(hạ tầng)** chốt engine để chạy campaign: libFuzzer hiện chạy ổn (run5 đang chạy);
      AFL++ để so "cùng engine" nếu cần (đang vướng lỗi heap-corruption ở integration — xem mục hạ tầng).
- [ ] (optional) Action/JS, FontMatrix injection; hot-code-based grammar (thầy ghi optional).

---

## Điều kiện bảo vệ (reframed quanh grammar)

1. **Kỹ thuật:** grammar + serializer + harness chạy được trên ≥1 target (✅ xpdf, PoDoFo).
2. **Đóng góp:** grammar **weakness-class-directed**, có bản đồ pattern → field → CWE/CVE rõ ràng
   (không phải structure-aware generic).
3. **Hiệu quả (≥1 trong các bằng chứng):** rediscovery ≥k CVE đã biết; HOẶC reach hàm nhạy cảm
   tốt hơn baseline; HOẶC valid-rate cao (~89%) + tìm được lỗi (đã có 1 finding PoDoFo).
4. **Phân tích:** vì sao hiệu quả/không; giới hạn (overfitting → đã có lập luận tổng quát hoá);
   hướng mở rộng.

---

## Cấu trúc luận văn (cập nhật theo framing mới)

| Chương | Nội dung | Trang |
|---|---|---|
| 1 — Giới thiệu | Bài toán, **đóng góp = weakness-class-directed grammar**, cấu trúc | 5-8 |
| 2 — Nền tảng | Coverage-guided fuzzing, định dạng PDF (object/xref/stream/filter), LPM, structure-aware fuzzing, **phân loại CWE/CAPEC** | 15-20 |
| 3 — Related work | AFLSmart/Superion/Nautilus/FormatFuzzer — bảng so sánh; **khoảng trống: generic structure-aware vs security/weakness-directed grammar** | 8-12 |
| 4 — Thiết kế | **★ Bản đồ attack-pattern → field → CWE/CVE (trọng tâm)**; serializer (xref+`/Length`); engine (hạ tầng) | 25-35 |
| 5 — Thực nghiệm | Setup; **rediscovery CVE trên bản cũ**; coverage hàm nhạy cảm vs AFL++; valid-rate; case study finding | 20-25 |
| 6 — Kết luận | Tóm tắt, giới hạn, mở rộng | 3-5 |

---

# ───────────────  HẠ TẦNG (ENGINE) — không phải đóng góp, để tham khảo  ───────────────

## Lựa chọn engine: libFuzzer vs AFL++ + libprotobuf-mutator

Engine chỉ là nơi chạy grammar. Hai lựa chọn, đều so sánh được với baseline (Nautilus, AFLSmart
cũng so cross-strategy với AFL — điều kiện là *cùng target + cùng cách đo coverage*, không bắt
buộc cùng engine):

- **libFuzzer + LPM** (`DEFINE_PROTO_FUZZER`): đang **chạy ổn** (run5). So với AFL++ baseline thì
  cần `llvm-cov` đo coverage trên cùng build xpdf (đã có tool `cov_driver`/`cov_compare.sh`).
- **AFL++ + custom mutator** (`afl_pdf_mutator.so`): so sánh trực tiếp (cùng edge-bitmap như
  baseline), nhưng **đang vướng lỗi `corrupted double-linked list`** ở integration AFL↔.so (đã
  loại trừ: không phải OOM, không phải bug logic mutator — ASan sạch 50k vòng; nghi ABI/toolchain
  giữa .so clang-14 và afl-fuzz conda-clang-18, đang xử lý).

### Ghi chú kỹ thuật AFL++ + LPM (giữ nguyên)

- Build serializer thành thư viện, dùng làm custom mutator AFL++. Không ảnh hưởng seed selection
  & scheduling của AFL++, chỉ thay mutation operators (thay hẳn bit-flip/havoc/splice nếu
  `AFL_CUSTOM_MUTATOR_ONLY=1`). LPM không coverage-aware: AFL đưa seed, LPM tự quyết field nào mutate.
- **CmpLog**: AFL++ patch giá trị comparison vào input theo byte-offset. Với LPM, input qua
  serialize nên byte-offset khác — CmpLog không biết chèn vào đâu. **Tắt CmpLog.**
- Mutator của LPM dựa trên message tree (add/remove/change/repeat), không phải byte-level.
- AFL++ + LPM ≈ Nautilus (coverage-guided structure-aware): AFL chọn seed tăng coverage, LPM giữ
  đột biến hợp lệ cú pháp. Khác Nautilus:
  - Nautilus dùng context-free grammar (recursion tự nhiên); LPM dùng protobuf schema (recursion
    bị giới hạn trong message).
  - Nautilus giữ input ở dạng derivation tree (splice/minimize subtree); LPM phải serialize/
    deserialize liên tục.
- Lý do không dùng fuzzer context-free grammar thuần (Nautilus, Gramatron): không tự tính được
  byte-offset (phụ thuộc final layout) → vẫn phải thêm bước hậu xử lý → không khác gì LPM.
  - Muốn tập trung recursion/tree-splice → AFL++ + grammar mutator (≈ Nautilus). LPM có hỗ trợ
    recursive (giới hạn trong schema), nhưng chưa thấy CVE PDF về object-graph topology (nếu có
    cũng chỉ DoS); lỗi PDF hầu hết nằm *bên trong* object.
  - Muốn cả grammar mutation + serialization trong 1 công cụ → libAFL + nautilus mutator + bước
    serialize phía sau.
  - Muốn tập trung mutate *bên trong* object → LPM.
- Cần harness cho AFL++ persistent mode (né overhead khởi tạo/huỷ process). Harness phải ổn định,
  không tự crash.
- Nhiều stream object nhận byte nhưng phải nén deflate (zlib) đúng filter, nếu không reader báo
  lỗi giải nén. Riêng codec ảnh (`/JPXDecode`,`/DCTDecode`) thì truyền byte dị dạng thẳng được.

### Định dạng seed cho AFL++ + libprotobuf-mutator (QUAN TRỌNG — dễ quên)

**Seed cho AFL++ phải là `PdfDocument` proto đã serialize, KHÔNG phải file PDF** — vì custom
mutator phải *deserialize* mỗi input về `PdfDocument` mới mutate được; seed là PDF thô → deserialize
fail → message rỗng/rác.

Có **HAI phép serialize** trong pipeline, đừng nhầm:

| Phép | Chiều | Đảo ngược? | Dùng để |
|---|---|---|---|
| **protobuf (de)serialize** | `PdfDocument` ⟷ bytes | Có | định dạng *input/seed* AFL lưu & mutator round-trip |
| **`SerializePdf()`** | `PdfDocument` → PDF bytes | Không (1 chiều) | file PDF thật target đọc |

```
SEED = PdfDocument proto đã serialize        <- gen_corpus tạo cái NÀY
   v  AFL lưu/schedule blob proto-bytes
custom mutator: deserialize -> mutate tree -> serialize -> trả AFL
   v  afl_custom_post_process (hoặc harness): deserialize -> SerializePdf() -> PDF bytes
   v  TARGET parse PDF
```
PDF chỉ tồn tại *tạm thời* lúc execution; thế giới của AFL toàn bộ là **proto**.

Hệ quả: (1) seed = proto serialize → `gen_corpus` tạo đúng; (2) **không** tái dùng được corpus
PDF của baseline làm seed (khác input domain); (3) đồng bộ định dạng text/binary giữa gen_corpus,
mutator deserialize, post_process (`gen_corpus --binary` cho AFL).

### Bài học phương pháp luận (QUAN TRỌNG — ghi vào Chương 5)

Harness phải **thực thi content stream thật** (render trang) thì mới chạm tới font/image parser.
Harness cũ chỉ `getNumPages` / `LoadFromBuffer`+`GetPageCount` → `Tf`/`Do` không chạy → coverage
"tăng" trước đó chủ yếu là code serializer/protobuf, không phải độ sâu target. Đã sửa (xpdf:
`displayPages`+`OutputDev`; PoDoFo: tokenize content + `GetFilteredCopy`), verify bằng gdb.

---

## Target & Baseline

- **Target ưu tiên** (ít fuzzing liên tục): xpdf, PoDoFo, qpdf. **Tránh** (đã có đội fuzz liên
  tục): pdfium, poppler, mupdf, pdf.js. *(Lưu ý: bản CŨ của poppler/mupdf vẫn dùng được cho thí
  nghiệm rediscovery CVE.)*
- **Baseline AFL++** (xpdf 4.06 `pdftotext`, ~86h): 47.69% (15,006/31,464 edges), 0 crash, 297
  hang. Dùng làm mốc so sánh.

---

## Khái niệm liên quan

1. **Weakness-class-directed grammar (đóng góp):** grammar được thiết kế để sinh input nhắm tới
   các *lớp điểm yếu* (CWE), không chỉ giữ input hợp lệ. CVE là ví dụ mẫu để rút pattern.
2. **CWE / CAPEC:** CWE = phân loại *điểm yếu* (CWE-190 int overflow, CWE-416 UAF...); CAPEC =
   phân loại *attack pattern*. Grammar nhắm theo CWE class để tổng quát hoá.
3. **Structure-aware fuzzing:** fuzzer hiểu cấu trúc qua schema, sinh input hợp lệ cú pháp để vượt
   format-check và chạm logic sâu. (Generic — chưa định hướng lỗi.)
4. **libprotobuf-mutator (LPM):** thư viện Google mutate protobuf message theo schema, giữ output
   hợp lệ. Tích hợp libFuzzer (`DEFINE_PROTO_FUZZER`) hoặc AFL++ (custom mutator).
5. **Serializer:** protobuf → raw bytes định dạng đích; khó nhất vì tự tính xref offset & `/Length`
   (phụ thuộc vị trí byte thực tế).
6. **xref table:** ánh xạ object number → byte offset; phải tính lại sau mỗi mutation.
