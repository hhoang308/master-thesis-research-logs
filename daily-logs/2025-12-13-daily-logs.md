- Định dạng PDF sử dụng graph-based object model và cơ chế truy cập ngẫu nhiên, do đó nếu sử dụng biểu diễn trung gian dạng cây sẽ bỏ qua `attack surface` quan trọng của pdf là `cross-reference table`, `stream objects` và `incremental updates mechanism.` Các đối tượng trong PDF có thể tham chiếu đến các đối tượng khác thông qua `object number` và `generation number`, do đó một đối tượng có thể sử dụng được nhiều lần mà không cần sao chép dữ liệu. Do đó để quá trình fuzzing thực sự hiệu quả, cần mô hình hoá các mối quan hệ phức tạp này. Ngoài ra, PDF cho phép hiển thị nhanh bất kì trang tài liệu nào thông qua cơ chế `cross-reference table` nằm ở cuối tệp. Bảng này lưu trữ `offset` (vị trí byte) của từng object do đó có thể truy cập chính xác từng object, và do có sự phụ thuộc về `offset` này mà một sự thay đổi nhỏ có thể ảnh hưởng đến tính toàn vẹn của file nếu `cross-reference table` không được cập nhật.
- PDF đóng gói dữ liệu lớn như ảnh, font vào các stream object và được nén hoặc mã hoá thông qua các filter. Đồng thời, mỗi stream sẽ đi kèm với stream dictionary, trong đó quan trọng nhất là `\Length` quy định độ dài thực tế của dữ liệu.
- Định dạng HTML và XML được thiết kế  trên cấu trúc cây phân cấp, bắt đầu từ các thẻ gốc, rồi phân nhánh thành các thẻ con lồng nhau. Parser của HTML xử lý tài liệu theo tuần tự từ đầu đến cuối, khi gặp một thẻ mới, nó tạo thành một nút mới trong DOM (Document Object Model) và duy trì trạng thái ngữ cảnh cho đến khi gặp thẻ đóng tương ứng. Do đó cơ chế fuzzing cho HTML thường tập trung vào việc tạo các cây DOM phức tạp, lồng ghép thẻ hoặc sai lệch cặp thẻ.
### Phân vùng các lỗ hổng đặc thù
1. Xref Table
- Offset Mismatch: Sửa offset của Xref để trỏ đến đối tượng khác hoặc trỏ ra ngoài vùng nhớ được cấp phát
- Xref Cycles: Trong PDF 1.5+, bảng xref có thể được lưu trữ dưới dạng một stream nén (xref stream) và nó cũng là một object, do đó nó cần được tham chiếu, có thể sửa để xref tự tham chiếu chính nó gây tràn bộ nhớ.
- Hybrid Xref: Tồn tài cả xref và xref stream trong cùng một file
2. Stream and Filters
- Heap Overflow: Đặt `/Length` nhỏ hơn thực tế để ghi đè lên các dữ liệu liền kề
- Filter Chains: PDF cho phép áp dụng nhiều bộ lọc liên tiếp, do đó có thể can thiệp vào sự tương tác giữa các bộ lọc: thay đổi thứ tự, tham số,...
- Object Streams: PDF cho phép nhúng các đối tượng gián tiếp bên trong một stream nén.
3. Objects and Graph-based Object Model
- Recursion Bombs: Một đối tượng tham chiếu chính nó để tạo ra đệ quy vô tận
- Type Confusion: Tham chiếu đến đối tượng khác với mong đợi của parser
4. Incremental Updates and Shadow Attacks
- Shadow Attacks: Thay đổi nội dung hiển thị sau khi tài liệu đã được kí số bằng cách thao túng Xref trong phần cập nhật.
- Kiểm tra tính hợp lệ nếu có nhiều trailer và xref trong cùng một văn 
### Đánh giá hạn chế của biểu diễn trung gian đa năng đối với PDF
1. SDK Sanitization
- Việc sử dụng SDK để sinh mã PDF được thiết kế để tạo ra các tệp PDF hợp lệ. Khi DIR yêu cầu tạo một đối tượng, SDK sẽ tự động tính toán offset, cập nhật bẳng xref và thiết lập độ dài stream chính xác. Do đó vô tình loại bỏ khả năng tạo ra các tệp pdf bán hợp lệ và biến dạng cấu trúc.
2. Loss of Structural Semantics
- Các object không cần thiết phải nằm trong page mà có thể được tham chiếu bởi page, nhưng DIR dạng cây không thể biểu diễn sự khác biệt giữa đối tượng trực tiếp và đối tượng gián tiếp. Do đó không thể tạo ra các tệp có các tham chiếu lơ lửng hoặc tham chiếu vòng.
3. File Metadata Ignorance
- Trong DIR không có các thành phẩn như xref table, trailer. DIR tập trung vào nội dung hiển thị mà bỏ qua cấu trúc vật lý (physical layout), do đó các lỗ hổng liên quan đến offset, versioning, hay chữ ký số không thể được phát hiện thông qua phương pháp này.
### Đề xuất PDF-IR
Đồ thị đối tượng có tuần tự hoá (Serializable Object Graph) với quyền kiểm soát chi tiết đối với cấu trúc vật lý của tệp
1. Kiến trúc
```
// Root
PDF-IR = {
    // Phiên bản
    Header: String,             // "%PDF-1.7" hoặc "%PDF-2.0"

    // Danh sách đối tượng
    Body: List<IndirectObject>, 

    // Bảng tham chiếu
    XRefTable: XRefStructure,   

    // Từ điển chứa thông tin khởi tạo
    Trailer: Dictionary,        // Chứa /Root, /Info, /Prev...

    // Danh sách các bản cập nhật nối đuôi
    IncrementalUpdates: List<UpdateBlock> 
}

// ---------------------------------------------------------
// CHI TIẾT CÁC THÀNH PHẦN CON (SUB-STRUCTURES)
// ---------------------------------------------------------

// A. CẤU TRÚC ĐỐI TƯỢNG GIÁN TIẾP (INDIRECT OBJECT) - [2]
// Đây là đơn vị cơ bản trong Body.
IndirectObject = {
    // Định danh đối tượng (theo chuẩn PDF)
    ID: Integer,                // Mã số đối tượng (`Object Number`)
    Generation: Integer,        // Số thế hệ (`Generation Number`)
    
    // Nội dung thực tế
    Type: String,               // Ví dụ: "Dictionary", "Stream", "Array", "Integer"
    Content: Data,              // Dữ liệu bên trong (Ví dụ: nội dung từ điển << /Key /Value >>)

    // --- CÁC TRƯỜNG DÀNH RIÊNG CHO FUZZING (MUTATION METADATA) ---
    // Cờ báo hiệu cho Serializer viết sai cú pháp cố ý
    IsMalformed: Boolean,       // Nếu True: Viết thiếu dấu đóng ">>" hoặc "endobj"
    
    // Cờ gây nhầm lẫn kiểu (Type Confusion)
    ForceTypeConfusion: Type    // Ví dụ: Content là Dictionary, nhưng ép Serializer ghi là Stream
}

// B. CẤU TRÚC ĐỐI TƯỢNG LUỒNG (STREAM) - [3]
// Dùng để chứa dữ liệu lớn (ảnh, font). Đây là nơi gây lỗi bộ nhớ (Heap Overflow).
StreamObject = {
    // Phần từ điển mô tả luồng
    Dictionary: Dictionary,     // Chứa khóa /Length, /Filter, /DecodeParms

    // Dữ liệu nhị phân thực tế
    RawData: BinaryBlob,        

    // --- CÁC TRƯỜNG DÀNH RIÊNG CHO FUZZING ---
    // Độ dài thực tế của RawData (Máy tính tự động)
    CalculatedLength: Integer,  
    
    // Độ dài SẼ ĐƯỢC GHI vào file (Người dùng/Fuzzer chỉnh sửa)
    // Nếu SpecifiedLength < CalculatedLength -> Gây lỗi cắt cụt hoặc buffer under-read
    // Nếu SpecifiedLength > CalculatedLength -> Gây lỗi đọc trộm bộ nhớ (buffer over-read)
    SpecifiedLength: Integer,   

    // Danh sách bộ lọc nén
    FilterPipeline: List        // Ví dụ: ["ASCII85Decode", "FlateDecode"]
}

// C. CẤU TRÚC BẢNG THAM CHIẾU (XREF STRUCTURE) - [4]
// Bản đồ vị trí byte. Được phơi bày để tấn công cơ chế truy cập ngẫu nhiên.
XRefStructure = {
    Type: String,               // "Table" (cổ điển) hoặc "Stream" (nén - PDF 1.5+)
    Entries: List<XRefEntry>    // Danh sách các dòng tham chiếu
}

XRefEntry = {
    ObjectNumber: Integer,
    
    // Vị trí byte trong file
    // Fuzzer có thể set số âm, hoặc trỏ ra ngoài file để gây lỗi Parser
    Offset: Integer,            

    Generation: Integer,
    
    // Trạng thái đối tượng
    // Fuzzer có thể đổi 'n' thành 'f' khi đối tượng vẫn đang dùng -> Lỗi Use-After-Free
    State: Enum [InUse ('n'), Free ('f')], 

    // Dùng cho danh sách liên kết các đối tượng đã xóa
    // Fuzzer có thể tạo vòng lặp vô hạn tại đây
    NextFreeObject: Integer     
}

// D. CẤU TRÚC CẬP NHẬT TĂNG DẦN (INCREMENTAL UPDATE) - [1], [5]
// Mô phỏng việc sửa file PDF nhiều lần (Versioning)
UpdateBlock = {
    Body: List<IndirectObject>, // Các đối tượng mới thêm vào hoặc bị sửa đổi
    XRefTable: XRefStructure,   // Bảng XRef chỉ chứa các mục cập nhật
    Trailer: Dictionary         // Trailer mới trỏ về Trailer cũ qua khóa /Prev
}
```
2. Đột biến
2.1. Đột biến Topo Đồ thị (Topological Mutations)

Chiến lược này thay đổi cấu trúc kết nối của đồ thị đối tượng nhằm tấn công logic duyệt cây và quản lý bộ nhớ của trình phân tích.

Tiêm Chu trình (Cycle Injection): Chọn một đối tượng container (Dictionary hoặc Array) và thêm một tham chiếu trỏ ngược về tổ tiên của nó. Mục tiêu là gây ra lỗi Stack Overflow trong các hàm đệ quy của trình phân tích (như ScanField trong Xpdf).   

Hoán đổi Tham chiếu (Reference Swapping): Thay thế tham chiếu đến một đối tượng an toàn (ví dụ: chuỗi văn bản) bằng tham chiếu đến một đối tượng phức tạp hoặc khác kiểu (ví dụ: một Stream hình ảnh hoặc một Dictionary đệ quy). Điều này kiểm tra tính chặt chẽ trong kiểm tra kiểu dữ liệu (Type Checking) của parser.  

Cô lập Đối tượng (Orphaning): Loại bỏ tham chiếu đến một đối tượng khỏi cây Root nhưng vẫn giữ đối tượng đó trong Body và XRef. Điều này kiểm tra cơ chế dọn dẹp bộ nhớ (Garbage Collection) hoặc xử lý lỗi của parser khi gặp đối tượng "ma".

2.2. Đột biến Cấu trúc Vật lý (Layout & XRef Fuzzing)

Nhắm vào cơ chế truy cập ngẫu nhiên và bảng tham chiếu.

Dịch chuyển Offset (Offset Shift): Tăng hoặc giảm giá trị offset trong bảng XRef một lượng nhỏ (1-4 bytes). Điều này buộc trình phân tích đọc các từ khóa cấu trúc (obj, stream) như dữ liệu, hoặc ngược lại, gây ra sự nhầm lẫn trong luồng phân tích (Parsing Confusion).   

Đảo trạng thái (State Flipping): Chuyển đổi cờ n (in-use) thành f (free) trong bảng XRef cho một đối tượng đang được sử dụng. Điều này nhằm kích hoạt lỗi Use-After-Free (UAF) khi parser cố gắng truy cập đối tượng mà nó cho là đã được giải phóng.  

Phân mảnh Bảng (Table Fragmentation): Chia nhỏ bảng XRef thành hàng trăm subsection nhỏ với các khoảng trống (gaps) lớn về số thứ tự đối tượng. Mục tiêu là kiểm tra khả năng quản lý bộ nhớ của parser đối với các mảng thưa (sparse arrays).

2.3. Đột biến Container Dữ liệu (Stream & Filter Fuzzing)

Tập trung vào bộ lọc giải nén và siêu dữ liệu độ dài.

Bất đồng bộ Độ dài (Length Desynchronization): Thiết lập SpecifiedLength trong PDF-IR thành các giá trị biên:

    ActualLength - 1: Gây lỗi Buffer Under-read hoặc cắt cụt dữ liệu.

    ActualLength + 1: Gây lỗi Buffer Over-read (đọc trộm bộ nhớ).

    MAX_INT hoặc số âm: Gây lỗi Integer Overflow/Underflow khi tính toán kích thước bộ đệm.   

Xáo trộn Chuỗi Bộ lọc (Filter Chain Fuzzing): Tạo ra các chuỗi bộ lọc hợp lệ nhưng phức tạp (ví dụ: nén Flate 10 lần) hoặc không hợp lệ (ví dụ: giải mã JPEG trước khi giải nén Flate). Mục tiêu là làm quá tải bộ nhớ hoặc gây lỗi logic trong pipeline xử lý luồng.  

2.4. Đột biến Lịch sử và Cập nhật (Incremental Update Fuzzing)

Khai thác tính năng versioning của PDF.

Giả mạo Lịch sử (History Forgery): Thêm một phần Body, XRef và Trailer mới vào cuối tệp (giả lập Incremental Update) định nghĩa lại đối tượng Root (/Catalog) để trỏ đến một trang độc hại, mô phỏng kỹ thuật tấn công Shadow.

Cập nhật "Ma" (Ghost Updates): Tạo phần cập nhật tăng dần tuyên bố cập nhật các đối tượng không hề tồn tại trong phiên bản gốc.

Phá vỡ Chuỗi Trailer: Làm hỏng offset /Prev trong từ điển Trailer để tạo ra chuỗi lịch sử vòng tròn hoặc trỏ vào vùng dữ liệu rác, buộc parser phải kích hoạt các quy trình khôi phục lỗi (thường kém an toàn hơn quy trình chuẩn).
### Danh sách các công việc cần làm
1. Sử dụng bộ PDF SDK hoặc PDF library, cụ thể: Foxit SDK, PyPDF2, PDFBox,...để tạo một file PDF nhưng có các ràng buộc không khớp nhau, bao gồm:
    - `/Length` = 100 nhưng độ dài stream thực tế khác 100 bytes (nhỏ hơn 100 hoặc lớn hơn 100 byte).
        $\to$ Dự đoán kết quả: Thư viện hoặc SDK sẽ tự động sửa `/Length` thành độ dài stream thực tế hoặc báo lỗi

2. Kiểm tra tính khả thi bằng việc sửa một file PDF hợp lệ và kiểm tra phản ứng của chương trình đọc PDF.

3. Xây dựng một bộ Serializer cho PDF với mục đích phiên dịch từ biểu diễn trung gian sang dạng byte một cách tuần tự nếu PDF SDK hoặc PDF library không cho phép SDK và thư viện tạo ra các kết quả đặc biệt phía trên.

