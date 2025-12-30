import pikepdf

# 1. Tạo một file PDF mới
pdf = pikepdf.new()

# 2. Thêm một trang trắng (để Adobe không báo lỗi file rỗng)
pdf.add_blank_page()

# 3. Viết đoạn code JavaScript bạn muốn nhúng
# Ví dụ: Hiện thông báo Hello World
js_code = """
app.alert("Hello! Code JavaScript da chay thanh cong!");
var a = 10;
var b = 20;
console.println("Ket qua: " + (a+b));
"""

# 4. Gắn code này vào sự kiện "OpenAction" 
# (Tức là vừa mở file lên là chạy ngay - Đây là vector tấn công phổ biến nhất)
pdf.Root.OpenAction = pikepdf.Dictionary(
    S=pikepdf.Name.JavaScript,
    JS=js_code
)

# 5. Lưu file
pdf.save('js_test.pdf')
print("Đã tạo file js_test.pdf")