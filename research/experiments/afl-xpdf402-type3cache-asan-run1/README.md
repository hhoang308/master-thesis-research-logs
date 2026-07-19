# AFL++ Splash+ASan — xpdf 4.02 — Type 3 font-cache (CVE-2020-25725)

Phương án 2: **giữ engine AFL** nhưng đổi harness sang **SplashOutputDev** + build **có ASan**, để
chạm và bắt được `heap-use-after-free` trong `SplashOutputDev::endType3Char` (CVE-2020-25725).

## Vì sao cần variant này
Harness AFL mặc định (`../afl-xpdf402-type3cache-run1`) dùng `FuzzOutputDev` với
`interpretType3Chars()=gFalse` → xpdf **không chạy** Type 3 char proc → `endType3Char` không bao
giờ được gọi. Và nó build **non-ASan** → UAF không thành crash. Vì vậy nó **không thể** tìm ra
CVE này. Variant Splash+ASan sửa cả hai:
- `SplashOutputDev` + `displayPage()` → chạy Type 3 char proc → tới `endType3Char`.
- `-fsanitize=address` (lan cả xpdf) → UAF thành SIGABRT mà AFL bắt được.

## Điều kiện tiên quyết (một lần): ASan runtime cho clang-18
`afl-clang-fast` dùng backend conda clang-18, mà máy này **thiếu** ASan runtime. Cài:
```bash
conda install -c conda-forge compiler-rt
# kiểm chứng runtime đã có đúng chỗ clang mong đợi:
PB=${PB_PREFIX:-$HOME/miniconda3}
ls "$("$PB/bin/clang++" -print-resource-dir)/lib/linux/libclang_rt.asan-x86_64.a"
```
Nếu conda cài `.a` vào thư mục phiên bản khác (vd `.../clang/18.1.8/...`) trong khi resource-dir là
`.../clang/18`, chỉ cần symlink cho khớp path.

---

## Build (các thay đổi mã đã có sẵn — chỉ cần build)

Đã sửa sẵn (không cần bạn động vào):
- `schema/pdf-proto/afl/afl_harness_xpdf.cpp` — thêm nhánh `#ifdef AFL_SPLASH_ASAN` (SplashOutputDev + `displayPage`).
- `schema/pdf-proto/CMakeLists.txt` — thêm option `AFL_SPLASH_ASAN`: bật ASan (lan vào xpdf) + link `splash` + `SplashOutputDev.cc`.

```bash
REPO_ROOT=$(git rev-parse --show-toplevel)
ROOT=$REPO_ROOT/research
PP=$ROOT/schema/pdf-proto ; XPDF402=$ROOT/thesis/xpdf-4.02 ; PB=${PB_PREFIX:-$HOME/miniconda3}
AFL_CC=${AFL_CC:-$HOME/.local/bin/afl-clang-fast}
AFL_CXX=${AFL_CXX:-$HOME/.local/bin/afl-clang-fast++}
EXP=$ROOT/experiments/afl-xpdf402-type3cache-asan-run1
export LD_LIBRARY_PATH=$PB/lib:$LD_LIBRARY_PATH

# 1) configure vào build-afl-asan (KHÔNG đụng build-afl non-ASan đang chạy)
cmake -S "$PP" -B "$PP/build-afl-asan" \
  -DCMAKE_C_COMPILER="$AFL_CC" \
  -DCMAKE_CXX_COMPILER="$AFL_CXX" \
  -DCMAKE_PREFIX_PATH="$PB" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DAFL_BUILD=ON -DAFL_SPLASH_ASAN=ON -DXPDF_LEGACY_DISPLAYPAGES=ON -DXPDF_SRC="$XPDF402"

# 2) build harness (chậm hơn: biên dịch thêm splash + SplashOutputDev, có ASan)
cmake --build "$PP/build-afl-asan" -j"$(nproc)" --target afl_harness_xpdf

# 3) xác minh binary có ASan
nm "$PP/build-afl-asan/afl_harness_xpdf" | grep -c __asan   # > 0 là có ASan
nm "$PP/build-afl-asan/afl_harness_xpdf" | grep -c __afl    # > 0 là có AFL instrument
```
Nếu configure than thiếu `FONTCONFIG_LIBRARY` → **đừng** ép nó (như bản non-ASan, để trống là được;
`if(FONTCONFIG_LIBRARY)` đã guard). Nếu link than thiếu freetype/splash → báo tôi để xử lý.

## Seed
Dùng lại 15 seed Type 3 đã sinh ở run trước:
```bash
mkdir -p "$EXP/seeds"
cp "$ROOT/experiments/afl-xpdf402-type3cache-run1/seeds/"*.pb "$EXP/seeds/"
ls "$EXP/seeds" | wc -l    # 15
```

## Chạy
```bash
chmod +x "$EXP/run.sh"
cd "$EXP"
./run.sh 300        # smoke test 5 phút (ASan+Splash chậm; cho nhiều thời gian hơn)
# kiểm tra:
grep -ciE 'PROGRAM ABORT|Segmentation' "$EXP/afl.log"          # lỗi hạ tầng (mong đợi 0)
LD_LIBRARY_PATH=$PB/lib ${AFL_WHATSUP:-$HOME/.local/bin/afl-whatsup} "$EXP/out"
# nếu ổn -> campaign dài:
./run.sh 86400
```

## Kỳ vọng & lưu ý
- **exec/s tụt mạnh** (ASan + rasterize thật) so với ~11k/s của bản non-ASan — bình thường.
- Nếu tìm ra bug: `saved_crashes > 0`, và **stack ASan `heap-use-after-free ... endType3Char`** nằm trong
  `afl.log` (hoặc chạy lại harness trên file crash để in stack).
- Crash lưu dạng **proto** trong `out/default/crashes/`. Dựng lại PDF để triage:
  `modules/type3cache/tools/type3cache2pdf` hoặc `tools/proto2pdf`.
- Nếu `afl-fuzz` báo lỗi về `ASAN_OPTIONS` lúc khởi động, làm theo dòng gợi ý nó in ra (đã set sẵn
  `abort_on_error=1:detect_leaks=0` trong `run.sh`).
- `-m none` bắt buộc (ASan map nhiều bộ nhớ ảo — đã có trong `run.sh`).

## So với các run khác
- `afl-xpdf402-proto-run1` — AFL byte→proto, PDF chung, non-ASan (đã xong 24h).
- `afl-xpdf402-type3cache-run1` — AFL Type 3, non-ASan/non-Splash → **không bắt được UAF** (đang chạy).
- **run này** — AFL Type 3 + Splash + ASan → **đúng cấu hình để bắt CVE-2020-25725**.
