import subprocess
import pikepdf
import sys
import os

NAUTILUS_ROOT_DIR = "/home/hoangnh8/nautilus"
NAUTILUS_CMD = ["cargo", "run", "--bin", "generator", "--", "-g", "/home/hoangnh8/master-thesis-daily-logs/logs/masterAcroJS.py", "-t", "100"]
OUTPUT_PDF = "fuzz_output.pdf"
OUTPUT_JS = "fuzz_js_output.txt"
SEED_PDF = "seed.pdf"

def generate_js_from_nautilus():
    """Chạy Nautilus và lấy code JS đầu ra"""
    try:
        print("[*] Đang gọi Nautilus để sinh code AcroJS...")
        # Gọi lệnh cargo run, hứng kết quả từ stdout (màn hình)
        result = subprocess.run(
            NAUTILUS_CMD, 
            capture_output=True, 
            text=True, 
            check=True,
            encoding='utf-8', # Quan trọng để xử lý các ký tự Unicode lạ
            cwd=NAUTILUS_ROOT_DIR
        )

        # Nautilus có thể in ra một số dòng log, ta chỉ cần phần code JS.
        # Nếu code JS in ra stdout, ta lấy nó.
        js_code = result.stdout

        if not js_code.strip():
            print("[-] Cảnh báo: Nautilus không sinh ra dòng code nào!")
            return None

        print(f"[+] Đã sinh được {len(js_code)} bytes code JS.")
        return js_code

    except subprocess.CalledProcessError as e:
        print(f"[-] Lỗi khi chạy Nautilus:\n{e.stderr}")
        return None
    except Exception as e:
        print(f"[-] Lỗi không xác định: {e}")
        return None

def save_js_log(js_content, filename):
    try:
        # Dùng encoding='utf-8' để không bị lỗi khi ghi các ký tự lạ (Naughty Strings)
        with open(filename, "w", encoding="utf-8") as f:
            f.write(js_content)
        print(f"[+] LOG: Đã lưu code JS gốc vào file '{filename}'")
        return True
    except Exception as e:
        print(f"[-] Lỗi khi lưu file log JS: {e}")
        return False

def create_pdf_with_js(js_content, output_filename):
    """Tạo PDF và nhúng code JS vào"""
    try:
        # Cách 1: Tạo file mới tinh (như script cũ của bạn)
        pdf = pikepdf.new()
        pdf.add_blank_page()

        # Cách 2: Nếu muốn dùng file mẫu có sẵn (Seed) thì dùng dòng này:
        # pdf = pikepdf.open(SEED_PDF)

        # Gắn code vào OpenAction
        pdf.Root.OpenAction = pikepdf.Dictionary(
            S=pikepdf.Name.JavaScript,
            JS=js_content
        )

        pdf.save(output_filename)
        print(f"[+] THÀNH CÔNG: Đã tạo file '{output_filename}' chứa code JS mới.")
        return True

    except Exception as e:
        print(f"[-] Lỗi khi tạo PDF: {e}")
        return False

def main():
    # Bước 1: Sinh code
    js_code = generate_js_from_nautilus()

    if js_code:
        save_js_log(js_code, OUTPUT_JS)
        # Bước 2: Đóng gói vào PDF
        create_pdf_with_js(js_code, OUTPUT_PDF)

    else:
        print("[-] Dừng quy trình do lỗi sinh code.")

if __name__ == "__main__":
    main()