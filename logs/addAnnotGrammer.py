# Phần này Nautilus sẽ tự inject biến ctx vào khi chạy
# ctx.rule(NON_TERMINAL, EXPANSION)

# 1. START & Cấu trúc chương trình
# Tạo vòng lặp để sinh ra nhiều lệnh addAnnot liên tiếp
ctx.rule(u'START', u'{PROGRAM}')

ctx.rule(u'PROGRAM', u'{STATEMENT};\n{PROGRAM}')
ctx.rule(u'PROGRAM', u'{STATEMENT};')

# 2. Câu lệnh addAnnot (STATEMENT)
# Lưu ý: \\{ và \\} để tạo ra dấu ngoặc nhọn thật cho JS Object
ctx.rule(u'STATEMENT', u'var ann = this.addAnnot(\\{ {ANNOT_BODY} \\})')

# 3. Thân Annotation (ANNOT_BODY)
# Bắt buộc có type và page, còn lại là các thuộc tính tùy chọn (OPT_PROPS)
ctx.rule(u'ANNOT_BODY', u'type: "{TYPE}", page: {PAGE}, {OPT_PROPS}')

# 4. Định nghĩa TYPE (Dựa trên tài liệu bạn gửi)
ctx.rule(u'TYPE', u'Text')
ctx.rule(u'TYPE', u'FreeText')
ctx.rule(u'TYPE', u'Line')
ctx.rule(u'TYPE', u'Square')
ctx.rule(u'TYPE', u'Circle')
ctx.rule(u'TYPE', u'Stamp')          # Quan trọng cho AP
ctx.rule(u'TYPE', u'FileAttachment') # Quan trọng cho attachment

# 5. Định nghĩa PAGE
ctx.rule(u'PAGE', u'0')
ctx.rule(u'PAGE', u'1')

# 6. Các thuộc tính tùy chọn (Đệ quy để sinh ra danh sách dài)
ctx.rule(u'OPT_PROPS', u'{PROP}, {OPT_PROPS}')
ctx.rule(u'OPT_PROPS', u'{PROP}')

# --- 7. CHI TIẾT CÁC THUỘC TÍNH (PROP) ---

# Tọa độ: rect và point
ctx.rule(u'PROP', u'rect: [{COORD}, {COORD}, {COORD}, {COORD}]')
ctx.rule(u'PROP', u'point: [{COORD}, {COORD}]')
ctx.rule(u'COORD', u'100')
ctx.rule(u'COORD', u'200')
ctx.rule(u'COORD', u'300')
ctx.rule(u'COORD', u'400')

# Màu sắc: strokeColor và fillColor
ctx.rule(u'PROP', u'strokeColor: color.red')
ctx.rule(u'PROP', u'strokeColor: color.yellow')
ctx.rule(u'PROP', u'strokeColor: ["RGB", {FLOAT}, {FLOAT}, {FLOAT}]')
ctx.rule(u'PROP', u'fillColor: ["CMYK", {FLOAT}, {FLOAT}, {FLOAT}, {FLOAT}]')
ctx.rule(u'FLOAT', u'0.5')
ctx.rule(u'FLOAT', u'1.0')
ctx.rule(u'FLOAT', u'0.0')

# Nội dung text (Test giới hạn 127 ký tự và buffer overflow)
ctx.rule(u'PROP', u'contents: "{STRING}"')
ctx.rule(u'PROP', u'author: "{STRING}"')
ctx.rule(u'STRING', u'Fuzzing Test')
ctx.rule(u'STRING', u'A' * 128)  # Chính xác 128 ký tự để test boundary
ctx.rule(u'STRING', u'B' * 1000) # Overflow nặng

# AP (Appearance) - Chỉ có ý nghĩa với Stamp
ctx.rule(u'PROP', u'AP: "{STAMP_NAME}"')
ctx.rule(u'STAMP_NAME', u'Confidential')
ctx.rule(u'STAMP_NAME', u'TopSecret')
ctx.rule(u'STAMP_NAME', u'NotApproved')
ctx.rule(u'STAMP_NAME', u'Draft')

# Note Icon (Biểu tượng ghi chú)
ctx.rule(u'PROP', u'noteIcon: "{ICON}"')
ctx.rule(u'ICON', u'Help')
ctx.rule(u'ICON', u'Key')
ctx.rule(u'ICON', u'NewParagraph')
ctx.rule(u'ICON', u'InvalidIconName') # Test lỗi

# Attachment Path (Đường dẫn file đính kèm)
ctx.rule(u'PROP', u'cAttachmentPath: "{PATH}"')
ctx.rule(u'PATH', u'/d/a.pdf')
ctx.rule(u'PATH', u'C:\\\\Windows\\\\System32\\\\cmd.exe') # Test đường dẫn hệ thống

# Rich Contents (Nội dung giàu - Cấu trúc phức tạp)
# Span object cũng là một object JS nên cần escape \\{ \\}
ctx.rule(u'PROP', u'richContents: [{SPAN_OBJ}]')
ctx.rule(u'SPAN_OBJ', u'\\{ text: "{STRING}", textSize: {NUM}, textColor: color.blue \\}')
ctx.rule(u'NUM', u'12')
ctx.rule(u'NUM', u'24')
ctx.rule(u'NUM', u'-1') # Số âm

# Các thuộc tính số khác
ctx.rule(u'PROP', u'width: {NUM}')
ctx.rule(u'PROP', u'opacity: {FLOAT}')
ctx.rule(u'PROP', u'rotate: {ANGLE}')
ctx.rule(u'ANGLE', u'0')
ctx.rule(u'ANGLE', u'90')
ctx.rule(u'ANGLE', u'45') # Góc lạ (Document chỉ cho phép 0, 90, 180, 270)