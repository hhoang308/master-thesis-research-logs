# Nautilus
- Mục tiêu là tạo seed đúng cả về mặt ngữ pháp và ngữ nghĩa bằng cách sử dụng ngữ pháp phi ngữ cảnh (context-free grammar).
- Đầu vào cần có là source code của target và ngữ pháp để tạo input.
- Sử dụng feedback để nâng cao hiệu quả.

### Context-Free Grammar
- Một vài khái niệm có liên quan:
    - `variable` hoặc `non-terminal symbol` là những "chỗ trống" cần điền giá trị vào.
    - `terminal symbol` là những từ hoặc chữ cái thực sự và không thể thay thế
    - `production rule` là quy tắc quy định xem một biến có thể được thay thế bằng cái gì
    - `start non-terminal` là điểm xuất phát
    - `derivation tree` là một dạng biểu diễn khác của kết quả sau khi được tạo ra bởi văn phạm phi ngữ cảnh, với `root` là kí hiệu bắt đầu và các cạnh là các `production rule`
- Một Context-Free Grammar là một tập các quy tắc theo dạng "một biến X có thể được thay thế bởi một hoặc nhiều giá trị từ một mảng các biến hoặc chuỗi" miễn biến X này không phải kí tự kết thúc. Ngoài ra, có một kí tự khởi tạo đặc biệt chỉ thị nơi bắt đầu áp dụng các quy luật này.
- Gọi đây là văn phạm phi ngữ cảnh vì trong các quy tắc của văn phạm, phía bên trái mũi tên chỉ được phép có duy nhất một non-terminal symbol, ví dụ: `A` $\to$ `B + C`, nghĩa là bất kì chỗ nào có A thì luôn có thể thay bằng B + C. Còn văn phạm có ngữ cảnh là `xAy` $\to$ `xBy`, nghĩa là chỉ được thay A bằng B khi phía trước và phía sau của A là x và y.
- Quy trình dẫn xuất, hay cách sinh ra chuỗi được thực hiện như sau:
    - Khởi động: Bắt đầu từ kí hiệu gốc `S`
    - Vòng lặp thay thế: Quét chuỗi hiện tại, tìm một rule bất kì rồi thay thế vế trái bằng vế phải, và chỉ dừng khi nào chuỗi chỉ chứa toàn là non-terminal symbol.
    Ví dụ: N là tập hữu hạn các non-terminal symbols, T là tập hữu hạn các terminal symbol (T và N không giao nhau), R là tâp hợp các production rule, S là ký tự bắt đầu. Một trong số những kết quả đầu ra có thể tạo được từ các tập này là: `a = 1`. Cụ thể, `PROG` $\to$ `STMT` $\to$ `VAR = EXPR` $\to$ `a = EXPR` $\to$ `a = NUMBER` $\to$ `a = 1`
    ![n, t, r](image-27.png)
    ![s](image-28.png)


