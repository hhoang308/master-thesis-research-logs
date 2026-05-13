khảo sát CVE (adobe reader, foxit reader, pdf reader khác,...) có payload -> tìm pattern trong payload của PDF (tập trung vào các pattern liên quan đến JS Engine) -> xây dựng văn phạm để reproduce lại được các payload này + các payload khác

xây dựng grammar để lai ghép các pattern

mục tiêu:
- tối thiểu chạy lại được các cve cũ
- tìm được các cve mới