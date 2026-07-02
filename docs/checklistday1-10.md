Day1: AAOS và Bound Service nền tảng

AAOS layered model có những lớp nào, và mỗi ranh giới lớp tạo ra loại coupling nào?
Vì sao App không nên gọi thẳng HAL mà đi qua service boundary?
Bound service lifecycle gồm những callback nào, callback nào chắc chắn có thể chạy nhiều lần?
Khi nào onCreate của service được gọi so với onBind?
BIND_AUTO_CREATE giải quyết bài toán gì và tạo side effect gì?
Nếu client bị kill bất ngờ, service lifecycle bị ảnh hưởng ra sao?
Vì sao AIDL phù hợp hơn local binder trong use case multi-process?
oneway trong AIDL mang ý nghĩa gì về blocking behavior của caller?
Nếu callback là oneway, thứ tự callback có luôn deterministic không?
Điểm khác nhau giữa interface contract và implementation contract trong Android IPC là gì?
Nếu cần explain cho senior “vì sao cần process boundary” trong 30 giây, bạn nói gì?
3 rủi ro lớn nhất khi mới học AIDL mà dev hay bỏ qua là gì?
Day2: Android skeleton và project foundation

Tại sao minSdk 26 là một lựa chọn hợp lý cho project này?
Bạn sẽ kiểm tra điều gì đầu tiên để biết skeleton project sạch trước khi thêm feature?
app namespace, applicationId và package khác nhau như thế nào?
Vì sao từ đầu phải xác định rõ package com.vdiag cho toàn bộ AIDL và Java?
Nếu app chạy Hello World nhưng Gradle setup sai, các vấn đề nào sẽ phát nổ ở tuần 2?
3 chỉ dấu cho thấy skeleton đã sẵn sàng cho AIDL và NDK extension?
Vì sao commit Day2 nên nhỏ và “boring” nhưng vẫn quan trọng?
Nếu interview hỏi “what did you de-risk at Day2”, bạn trả lời thế nào?
Day3: HAL CMake skeleton

Vì sao tách hal thành standalone CMake project chạy trên Linux host là quyết định tốt?
hal standalone giúp giảm rủi ro gì so với build tất cả qua Android NDK ngay từ đầu?
CMakeLists tối thiểu cần những phần nào để scale từ dummy lên library thật?
Include path management sai sẽ gây bug kiểu gì khi project lớn dần?
Mục tiêu thực sự của dummy.cpp trong Day3 là gì ngoài việc return 42?
Vì sao build green ở Day3 là checkpoint kiến trúc chứ không chỉ “biên dịch được”?
Nếu phải viết test harness sớm cho hal, bạn đặt boundary test ở đâu?
Khi nào nên split thành static lib và executable test targets trong CMake?

Day4: AIDL contracts

IDiagCarService nên expose những method nào và vì sao?
Vì sao registerCallback và unregisterCallback phải là API first-class ngay từ đầu?
DiagRequest nên giữ field nào ở giai đoạn này để không over-design?
Vì sao dùng Parcelable object thay vì truyền từng primitive params?
Callback contract ở IDiagCallback.aidl có điểm nào cần giữ backward-compatible?
Ý nghĩa của requestId trong async system là gì?
Nếu callback về chậm hoặc out-of-order, requestId cứu bạn như thế nào?
Điểm khác nhau giữa request semantic và transport payload semantic trong DiagRequest?
Nếu phải thêm field mới vào DiagRequest, bạn giữ compatibility thế nào?
Vì sao AIDL package naming sai có thể làm build pass nhưng runtime fail?
Cách verify generated Stub và Proxy đúng mà không chỉ nhìn folder build?
Nếu senior hỏi “why this IPC API is minimal but sufficient”, bạn defend ra sao?
Day5: Repo structure và CI shell

Vì sao tách android, hal, docs ở repo root là kiến trúc repo tốt cho project này?
CI Day5 nên kiểm tra tối thiểu những gì để có giá trị thật?
Vì sao ngay tuần 1 đã cần lessons.md thay vì đợi cuối phase?
Tag weekly milestone giải quyết vấn đề quản trị kỹ thuật gì?
Nếu CI chỉ build mà chưa test, bạn chấp nhận rủi ro nào và vì sao vẫn hợp lý ở Day5?
README skeleton nên chứa 3 phần bắt buộc nào để người mới vào hiểu ngay project?
Nếu teammate clone repo, 5 phút đầu họ cần thấy điều gì?
Cách bạn chứng minh Day5 “infra readiness” với senior reviewer?
Day6: DiagCarService skeleton

Tại sao service nên chạy process riêng thông qua AndroidManifest?
Ý nghĩa bảo mật của exported false với service này là gì?
onCreate của service nên khởi tạo những thành phần nào và chưa nên khởi tạo gì?
onDestroy phải cleanup gì để tránh leak binder/native resources?
Nếu service init JNI fail, service nên fail fast hay degraded mode?
DiagCarService và DiagCarServiceBinder nên tách trách nhiệm như thế nào?
Vì sao service class không nên nhồi logic nghiệp vụ nặng?
Nếu bị kill bởi hệ thống, service state nào cần recover?
Bạn verify đúng multi-process setup bằng command nào và kỳ vọng output gì?
Nếu interviewer hỏi “why process split now, not later”, bạn trả lời sao?
Day7: Binder implementation và dummy response

Trong Binder method getProperty, bước validate tối thiểu là gì?
Bạn log callingPid và callingUid để làm gì, và khi nào log này không đáng tin?
Vì sao response path phải đi qua callback thay vì return trực tiếp?
Khi callback null hoặc không đăng ký, behavior đúng là gì?
Dummy response stage giúp de-risk phần nào trước JNI integration?
Nếu request malformed, bạn trả error thế nào để không crash service?
Sự khác nhau giữa lỗi transport và lỗi business trong binder layer?
Nếu hai client gọi cùng lúc, binder implementation cần thread-safe ở đâu?
Vì sao không được block lâu trong binder thread?
Nếu callback throw RemoteException, service nên xử lý như thế nào?
Day8: PermissionGate và security model

Signature permission khác dangerous permission ở điểm cốt lõi nào?
Vì sao cần cả permission declaration và uses-permission trong manifest?
PermissionGate nên enforce ở entrypoint nào để không bypass?
enforceCallingOrSelfPermission và checkCallingPermission khác nhau ra sao?
Tại sao fail fast bằng SecurityException là hành vi đúng ở IPC boundary?
Nếu app cùng package nhưng ký key khác, behavior mong đợi là gì?
Nếu quên enforce ở một method binder, attack surface cụ thể là gì?
Bạn viết test scenario nào để chứng minh permission đang thực thi thật?
Nếu interview hỏi “defense-in-depth ở service này là gì”, bạn kể đủ lớp nào?
Với process riêng, permission model bổ sung thêm bảo vệ gì so với same-process?
Day9: ClientRegistry và DeathRecipient

Vì sao phải có ClientRegistry thay vì giữ một callback variable duy nhất?
Key trong map nên là gì và vì sao?
linkToDeath giải quyết đúng bài toán gì?
binderDied callback phải làm tối thiểu 3 việc nào?
unlinkToDeath cần gọi ở đâu để tránh rò tài nguyên?
Vì sao ConcurrentHashMap là chọn phù hợp ở đây?
Nếu một client register callback hai lần, policy đúng nên là gì?
Nếu client process chết giữa request đang xử lý, service phải đảm bảo điều gì?
cleanup toàn bộ registry trong onDestroy có cần idempotent không?
Điểm khác nhau giữa client unregister chủ động và binder death bị động?
Nếu callback object bị stale, làm sao tránh gửi response vào “xác chết” binder?
Race condition nào có thể xảy ra giữa unregister và binderDied?
Bạn sẽ thêm metric/log gì để quan sát health của registry trong runtime?
Cách verify DeathRecipient bằng force-stop như thế nào để chắc chắn đúng?
Nếu interviewer hỏi “why this is robust against client crashes”, bạn defend bằng flow nào?
Nếu mở rộng nhiều callback type trong tương lai, registry design cần đổi gì?
Day10: End-to-end bind test và verify

E2E path Day10 từ MainActivity tới callback gồm các bước nào?
Dấu hiệu nào trong log chứng minh request đi qua đúng binder chain?
Làm sao phân biệt service connect thành công với callback register thành công?
Bạn nên disable action buttons khi disconnected để tránh bug gì?
Nếu bind fail, UI nên phản ứng ra sao cho đúng semantics?
Khi app bị force-stop, kỳ vọng ở service side logs là gì?
2 process verify bằng ps giúp phát hiện lỗi cấu hình nào?
Nếu callback không về nhưng service nhận request, bạn debug theo thứ tự lớp nào?
RequestId có vai trò gì ngay cả khi Day10 mới có request đơn?
Với Day10, checklist pass thực chiến tối thiểu gồm những test case nào?
Nếu senior hỏi “what can still fail after Day10”, bạn liệt kê gì?
Bạn tóm tắt technical debt còn lại từ Day10 trước khi lên JNI như thế nào?
Checklist cross-cutting cấp senior cho Day1-Day10

Đâu là API boundary rõ nhất của hệ thống ở tuần 1-2?
Đâu là trust boundary rõ nhất của hệ thống ở tuần 1-2?
Đâu là chỗ có khả năng tạo deadlock hoặc ANR trong thiết kế hiện tại?
Đâu là điểm có nguy cơ memory leak cao nhất trước Day11?
Đâu là invariant quan trọng nhất của callback lifecycle?
Nếu phải viết threat model mini cho Day1-Day10, bạn viết 5 threat nào?
Nếu phải thêm observability ngay bây giờ, bạn thêm metric nào ở service?
Nếu phải onboard intern vào code này, 3 file nào bắt họ đọc đầu tiên?
Điều gì đang “just enough” và điều gì đang “under-engineered”?
Một thay đổi nhỏ nào có thể phá vỡ compatibility AIDL và cách phòng tránh?
Bạn sẽ commit message chuẩn hóa thế nào để trace từ requirement tới code?
Nếu rollback nhanh về mốc Day10, file nào là critical path cần giữ nguyên?