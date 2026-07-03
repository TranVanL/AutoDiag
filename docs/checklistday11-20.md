Day11: JNI setup và baseline native bridge

Vì sao phải đọc perf JNI trước khi code native bridge?
JNI_OnLoad nên cache những gì và vì sao không nên FindClass lặp lại ở runtime?
3 lỗi JNI kinh điển dễ gặp nhất ở giai đoạn đầu là gì?
CMake/NDK config tối thiểu cần gì để build shared library JNI?
Vì sao nên bắt đầu bằng nativeInit dummy trước native business logic?
JNI local ref và global ref khác nhau ở điểm cốt lõi nào?
Vì sao FindClass trên worker thread có thể fail?
Nếu nativeInit fail, service hoặc app nên phản ứng thế nào?
Checkpoint Day11 cần verify những gì để xem JNI skeleton đã sạch?
Nếu interview hỏi “why cache in JNI_OnLoad”, bạn trả lời sao?

Day12: JNI_OnLoad cache class và method ID

JNI_OnLoad nên lấy và cache những class/method nào?
Vì sao phải DeleteGlobalRef trong JNI_OnUnload?
Method signature trong GetMethodID sai sẽ fail theo kiểu nào?
Làm sao verify class descriptor com/vdiag/IDiagCallback là đúng?
Vì sao cache method ID giúp ổn định và nhanh hơn?
Nếu callback interface đổi chữ ký, phần nào sẽ vỡ trước?
JNI bridge cần giữ reference nào qua nhiều request?
Nếu loadLibrary thành công nhưng callback không gọi được, bạn debug lớp nào trước?
Vì sao nên log JNI_OnLoad OK với đủ descriptor đã cache?
Nếu interview hỏi “why global ref lifetime matters”, bạn nói thế nào?

Day13: DiagHalBridge và nativeInit sống

DiagHalBridge.java nên expose những native method nào ở giai đoạn này?
Vì sao loadLibrary nên nằm ở static block hay init path rõ ràng?
nativeInit nên nhận tham số gì để chuẩn bị cho tương lai?
Làm sao verify luồng Java -> nativeInit -> log OK hoạt động thật?
Vì sao chưa nên nhảy ngay sang native async worker ở Day13?
Service onCreate nên gọi nativeInit ở đâu và vì sao?
Nếu nativeInit fail, cleanup cần làm gì để tránh trạng thái nửa sống?
Điểm khác nhau giữa verify build pass và verify runtime pass ở JNI là gì?
Làm sao biết Logcat đang đi qua đúng lớp bridge?
Nếu interview hỏi “what did Day13 de-risk”, bạn trả lời sao?

Day14: nativeGetProperty sync path

nativeGetProperty sync stage đang de-risk phần nào?
Vì sao callback được gọi trực tiếp trên Binder thread chỉ nên là tạm thời?
Input tối thiểu của nativeGetProperty gồm những gì?
Vì sao phải delete local ref sau khi CallVoidMethod?
Nếu propertyId không support, nativeGetProperty nên trả gì?
Điểm yếu lớn nhất của sync callback path là gì?
Làm sao chứng minh Java -> C++ -> Java round-trip đã work?
Nếu callback throw trong native path, service nên xử lý thế nào?
Vì sao nên giữ đường sync đơn giản trước khi chuyển sang worker thread?
Nếu interview hỏi “why sync first, async later”, bạn defend sao?

Day15: CheckJNI, cleanup và edge cases JNI

CheckJNI phát hiện những lỗi gì mà build thường không thấy?
Kill app giữa request có thể làm lộ bug JNI nào?
GlobalRef leak biểu hiện ra sao khi chạy nhiều lần?
Vì sao nên test 5 requests liên tiếp trước khi mở rộng thêm logic?
JNI cleanup cần đảm bảo idempotent như thế nào?
Nếu app force-stop, native state nào cần được reset?
Làm sao biết không còn local ref overflow hoặc global ref table overflow?
Thread detach trong JNI có thể gây lỗi kiểu nào nếu quên xử lý?
Nếu interview hỏi “what are the top JNI pitfalls”, bạn nêu gì?
Checkpoint Day15 cần pass những test nào để coi JNI ổn?

Day16: UDS codec và diag types

diag_types.h tối thiểu cần những enum/struct nào?
Vì sao nên model UDS service, property và NRC rõ ràng từ đầu?
encode(UdsService::ReadDataByIdentifier) phải tạo frame gì?
Vì sao ClearDTC hoặc TesterPresent cần test riêng?
Diagnostic request và diagnostic response nên tách ra sao?
Mục tiêu của Day16 là codec đúng hay business logic đúng?
Làm sao biết encode/decode coverage đã đủ để đi tiếp?
Nếu byte payload sai định dạng, codec nên trả lỗi gì?
Vì sao nên có helper nrcToString ngay từ đầu?
Nếu senior hỏi “why UDS enum first”, bạn defend thế nào?

Day17: UDS decode và negative response

Decode positive response và negative response khác nhau ra sao?
NRC byte nằm ở vị trí nào trong negative response?
Vì sao phải test empty response và truncated response?
Làm sao xử lý payload nhiều byte cho VIN, SOC, RPM?
Vì sao decode nên giữ semantics rõ giữa transport và business?
Nếu serviceId không khớp, decode nên trả gì?
Khi nào nên coi response là protocol error thay vì business error?
Nếu decode hỏng nhưng transport OK, tầng nào cần debug trước?
Vì sao 20 tests là checkpoint quan trọng cho UDS layer?
Nếu interview hỏi “how do you know decoder is production-ready”, bạn nói gì?

Day18: MockDiagnosticHal và DID database

MockDiagnosticHal nên mô phỏng những DID nào trước?
Vì sao DID database trong mock HAL hữu ích cho test và demo?
sendAndReceive nên xử lý ReadDID, ReadDTC, ClearDTC, TesterPresent thế nào?
Một mock HAL tốt cần return deterministic hay random?
Vì sao nên thêm tests cho state transitions và invalid DID?
Nếu isReady false thì behavior mong đợi là gì?
Làm sao đảm bảo mock HAL không che mất bug thật của engine?
Vì sao nên tách mock dữ liệu và codec dữ liệu?
Nếu interview hỏi “why mock HAL now”, bạn defend sao?
Checkpoint Day18 cần chứng minh điều gì về HAL abstraction?

Day19: SessionStateMachine và DiagEngine worker queue

SessionStateMachine cần bảo vệ những transition nào?
Vì sao worker thread + condition_variable là mô hình hợp lý ở giai đoạn này?
submit request cần lock ở đâu và vì sao?
Queue depth và stop flag nên được bảo vệ như thế nào?
Vì sao shutdown phải join worker thread?
Nếu hai request vào cùng lúc, engine phải giữ invariant gì?
Worker loop cần làm gì khi stop_ được set?
Làm sao đo được request-to-callback latency trong engine?
Nếu callback throw trong worker thread, engine nên làm gì?
Nếu interview hỏi “why separate session state from engine queue”, bạn nói sao?

Day20: TSAN, ASAN và milestone engine standalone

TSAN giúp phát hiện lỗi gì ở DiagEngine?
ASAN và Valgrind mỗi tool bắt loại bug nào?
Vì sao cần test shutdown clean dưới sanitizer?
Nếu engine chạy 5 requests burst, cần verify những gì?
Queue implementation có nguy cơ race ở đâu nhất?
Làm sao chứng minh không còn use-after-free trong callback path?
Checkpoint Day20 cần bao nhiêu tests pass để coi engine ổn?
Tại sao đây là checkpoint kiến trúc chứ không chỉ build xanh?
Nếu interview hỏi “what did you de-risk by Day20”, bạn trả lời sao?

Checklist cross-cutting cấp senior Day11-Day20

Đâu là boundary rõ nhất mới được thêm ở Day11-Day20?
Đâu là chỗ có nguy cơ deadlock hoặc race cao nhất khi thêm worker thread?
Đâu là điểm dễ leak nhất giữa Java ref, native ref và registry state?
Đâu là invariant quan trọng nhất của async request-response khi có callback?
Nếu phải viết threat model mini cho JNI + engine + HAL, bạn nêu 5 threat nào?
Nếu phải thêm observability ngay lúc này, bạn thêm metric nào cho JNI và engine?
Nếu teammate mới clone repo ở mốc Day20, 5 phút đầu họ cần đọc file nào?
Điều gì là just enough và điều gì chưa đủ tốt trước khi lên subscription/watchdog?
Một thay đổi nhỏ nào có thể phá vỡ compatibility của codec hoặc JNI contract?
Nếu rollback về Day20, file nào là critical path cần giữ nguyên?