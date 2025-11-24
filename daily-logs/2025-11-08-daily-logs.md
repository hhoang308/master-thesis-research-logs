# AFLSmart++
Các bài báo tham khảo được: LibProtobuf-Mutator, Nautilus, Superion.

Nhược điểm của AFLSmart:
1. Chỉ thực hiện higher-order mutations ở mức chunk level, bao gồm: chunk delete, chunk splicing mà không thực hiện lower-level mutations bên trong các chunk structures.
2. Chỉ hỗ trợ những chương trình nhận vào một định dạng cụ thể mà người thực hiện fuzzing chỉ định trước, ví dụ thư viện đọc file png chỉ nhận đầu vào là file png, chứ không tự động chọn định dạng hoặc fuzzing cho chương trình nhận vào nhiều định dạng file hoặc các định dạng file đặc biệt không được public ra ngoài. Theo cách thông thường, nếu fuzzing file có thể nhận nhiều định dạng file đầu vào, thì mỗi lần thử với một định dạng sẽ phải chủ động chọn định dạng để xây dựng virtual structure, do đó sẽ phải dừng lại và nhập tay.

Cải tiến của AFLSmart++:
1. Structure-aware Low-level Mutation Operators
Dựa trên các nghiên cứu của tác giả về các định dạng file khác nhau và các thư viện xử lý định dạng file đấy, thì có vài loại chunk kiểm soát nhiều nhánh (tức được xét điều kiện tại nhiều nơi?) hơn so với các loại chunk khác. Sau khi đã chọn các được chunk này, sẽ thực hiện low-level mutation operator bên trong chunk này và từ đó giảm thiểu khả năng phá vỡ định dạng file.
Để triển khai tính năng này, tác giả thêm vào 2 hashset, một "current" hashset cho các loại chunk trong input vừa được parse và một "global" hashset cho tất cả các loại chunk phân tích được từ input queue (hay seed corpus, hay tất cả các đầu vào từ trước đến giờ). Dựa trên 2 hashset và các dữ liệu khác, tính toán được điểm số cho từng loại chunk, và tại thời điểm hiện tại thì tấc giả sử dụng thuật toán lựa chọn tên là FAVOR: ưu tiên những loại chunk hiếm thấy và những loại chunk giúp phát hiện ra paths mới. Ngoài ra tác giả còn thực hiện 2 thuật toán lựa chọn khác là RANDOM và ROUND_ROBIN, RANDOM sẽ lựa chọn chunk types ngẫu nhiên còn ROUND_ROBIN sẽ lựa chọn chunk types theo lượt (về lâu dài, theo lý thuyết, thì tất cả các chunk types đều sẽ được lựa chọn)
2. Composite Input Model
`composite input model` là một file chứa file `.xml` của các định dạng file khác như PNG, PDF, WAV,...Khi một `composite model` được đưa vào AFLSmart++, File Crackers sẽ chọn tối đa 1 input model khớp với seed input được cho ban đầu trong danh sách các định dạng được lưu tại `composite input model`. Trong trường hợp không khớp với file nào 