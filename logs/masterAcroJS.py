import sys

# Skeleton
ctx.rule(u'START', u'var myAnn = null;\n{PROGRAM}')

ctx.rule(u'PROGRAM', u'{STATEMENT};\n{PROGRAM}')
ctx.rule(u'PROGRAM', u'{STATEMENT};')

# API List
ctx.rule(u'STATEMENT', u'{STMT_ANNOT_CREATE}')   # Tạo annotation
ctx.rule(u'STATEMENT', u'{STMT_ANNOT_MODIFY}')   # Sửa thuộc tính (Cái bạn vừa hỏi)
ctx.rule(u'STATEMENT', u'{STMT_ANNOT_DESTROY}')  # Xóa annotation

# ctx.rule(u'STATEMENT', u'{STMT_PAGE}')        # Module Page
# ctx.rule(u'STATEMENT', u'{STMT_APP}')         # Module App
# ctx.rule(u'STATEMENT', u'{STMT_UTIL}')        # Module Util

# Module Annotation
# 1. Create

ctx.rule(u'STMT_ANNOT_CREATE', u'myAnn = this.addAnnot(\\{ {ANNOT_BODY} \\})')

ctx.rule(u'ANNOT_BODY', u'type: "{TYPE}", page: {PAGE}, {OPT_PROPS}')

ctx.rule(u'TYPE', u'Text')
ctx.rule(u'TYPE', u'FreeText')
ctx.rule(u'TYPE', u'Line')
ctx.rule(u'TYPE', u'Square')
ctx.rule(u'TYPE', u'Circle')
ctx.rule(u'TYPE', u'Stamp')
ctx.rule(u'TYPE', u'FileAttachment')

ctx.rule(u'PAGE', u'0')
ctx.rule(u'PAGE', u'1')

ctx.rule(u'OPT_PROPS', u'{PROP}, {OPT_PROPS}')
ctx.rule(u'OPT_PROPS', u'{PROP}')


ctx.rule(u'PROP', u'rect: [{COORD}, {COORD}, {COORD}, {COORD}]')
ctx.rule(u'PROP', u'point: [{COORD}, {COORD}]')
ctx.rule(u'COORD', u'100')
ctx.rule(u'COORD', u'200')
ctx.rule(u'COORD', u'300')
ctx.rule(u'COORD', u'400')

ctx.rule(u'PROP', u'strokeColor: color.red')
ctx.rule(u'PROP', u'strokeColor: color.yellow')
ctx.rule(u'PROP', u'strokeColor: ["RGB", {FLOAT}, {FLOAT}, {FLOAT}]')
ctx.rule(u'PROP', u'fillColor: ["CMYK", {FLOAT}, {FLOAT}, {FLOAT}, {FLOAT}]')
ctx.rule(u'FLOAT', u'0.5')
ctx.rule(u'FLOAT', u'1.0')
ctx.rule(u'FLOAT', u'0.0')

ctx.rule(u'PROP', u'contents: "{STRING}"')
ctx.rule(u'PROP', u'author: "{STRING}"')

ctx.rule(u'STRING', u'{NAUGHTY_CONTENT}')

ctx.rule(u'NAUGHTY_CONTENT', u'A' * 128)
ctx.rule(u'NAUGHTY_CONTENT', u'B' * 1024)
ctx.rule(u'NAUGHTY_CONTENT', u'C' * 4096)

ctx.rule(u'NAUGHTY_CONTENT', u'%s%s%s%s%s')
ctx.rule(u'NAUGHTY_CONTENT', u'%n%n%n%n')
ctx.rule(u'NAUGHTY_CONTENT', u'%x%x%x%x')

ctx.rule(u'NAUGHTY_CONTENT', u'../../../../windows/system32/cmd.exe')
ctx.rule(u'NAUGHTY_CONTENT', u'\\\\\\\\127.0.0.1\\\\C$\\\\Windows') # SMB path
ctx.rule(u'NAUGHTY_CONTENT', u'/etc/passwd')
ctx.rule(u'NAUGHTY_CONTENT', u'COM1')

ctx.rule(u'NAUGHTY_CONTENT', u'Text\\0NullByte')
ctx.rule(u'NAUGHTY_CONTENT', u'\\0')
ctx.rule(u'NAUGHTY_CONTENT', u'\\n\\r\\t' * 10)

ctx.rule(u'NAUGHTY_CONTENT', u'\ufeff') # BOM
ctx.rule(u'NAUGHTY_CONTENT', u'\ufffe')
ctx.rule(u'NAUGHTY_CONTENT', u'\u4141\u4141') # Wide characters

ctx.rule(u'NAUGHTY_CONTENT', u"' OR '1'='1")
ctx.rule(u'NAUGHTY_CONTENT', u'"; DROP TABLE annotations; --')

ctx.rule(u'NAUGHTY_CONTENT', u'<script>app.alert(1)</script>')

ctx.rule(u'NAUGHTY_CONTENT', u'undefined')
ctx.rule(u'NAUGHTY_CONTENT', u'null')
ctx.rule(u'NAUGHTY_CONTENT', u'NaN')

ctx.rule(u'PROP', u'AP: "{STAMP_NAME}"')
ctx.rule(u'STAMP_NAME', u'Confidential')
ctx.rule(u'STAMP_NAME', u'TopSecret')
ctx.rule(u'STAMP_NAME', u'NotApproved')
ctx.rule(u'STAMP_NAME', u'Draft')

ctx.rule(u'PROP', u'noteIcon: "{ICON}"')
ctx.rule(u'ICON', u'Help')
ctx.rule(u'ICON', u'Key')
ctx.rule(u'ICON', u'NewParagraph')
ctx.rule(u'ICON', u'InvalidIconName')

ctx.rule(u'PROP', u'cAttachmentPath: "{PATH}"')
ctx.rule(u'PATH', u'/d/a.pdf')
ctx.rule(u'PATH', u'C:\\\\Windows\\\\System32\\\\cmd.exe')

ctx.rule(u'PROP', u'richContents: [{SPAN_OBJ}]')
ctx.rule(u'SPAN_OBJ', u'\\{ text: "{STRING}", textSize: {NUM}, textColor: color.blue \\}')
ctx.rule(u'NUM', u'12')
ctx.rule(u'NUM', u'24')
ctx.rule(u'NUM', u'-1')

ctx.rule(u'PROP', u'width: {NUM}')
ctx.rule(u'PROP', u'opacity: {FLOAT}')
ctx.rule(u'PROP', u'rotate: {ANGLE}')
ctx.rule(u'ANGLE', u'0')
ctx.rule(u'ANGLE', u'90')
ctx.rule(u'ANGLE', u'45') # Góc lạ (Document chỉ cho phép 0, 90, 180, 270)

# 2. Modify

ctx.rule(u'STMT_ANNOT_MODIFY', u'myAnn.richContents = {SPAN_ARRAY}')
ctx.rule(u'STMT_ANNOT_MODIFY', u'myAnn.strokeColor = {COLOR}')
ctx.rule(u'STMT_ANNOT_MODIFY', u'myAnn.point = [{COORD}, {COORD}]')

ctx.rule(u'SPAN_ARRAY', u'new Array({SPAN_OBJ})')
ctx.rule(u'SPAN_ARRAY', u'new Array({SPAN_OBJ}, {SPAN_OBJ})')

ctx.rule(u'SPAN_OBJ', u'\\{ text: "{STRING}", textColor: {COLOR}, textSize: {NUM} \\}')

ctx.rule(u'STMT_ANNOT_DESTROY', u'myAnn.destroy()')

ctx.rule(u'COLOR', u'color.red')
ctx.rule(u'COLOR', u'color.blue')

# Module Page


# Shared Definitions
# ctx.rule(u'NUM', u'0')
# ctx.rule(u'NUM', u'1')
# ctx.rule(u'NUM', u'-1')
# ctx.rule(u'STRING', u'A' * 128)