Day1:

1. AAOS layered model có những lớp nào, và mỗi ranh giới lớp tạo ra loại coupling nào?
- Application -> Car API: API coupling (compile-time, SDK contract).
- Car API -> Car Service (system process): IPC coupling qua Binder/AIDL (runtime contract + permission).
- Car Service (Java/Kotlin) -> Native core: JNI coupling (ABI + marshaling dữ liệu).
- Native core -> HAL interface: interface coupling (stable abstraction, giảm phụ thuộc phần cứng cụ thể).
- HAL impl -> Vehicle bus (CAN/LIN/Ethernet): protocol coupling (frame format, timing, bus constraints).

2. Vì sao App không nên gọi thẳng HAL mà đi qua service boundary?
- Security: service là nơi enforce permission/audit.
- Stability: lỗi app không làm hỏng trực tiếp hardware path.
- Governance: arbitration khi nhiều app cùng đòi truy cập một tài nguyên xe.
- Compatibility: app giữ API ổn định, HAL có thể thay đổi bên dưới.

3. Bound service lifecycle gồm những callback nào, callback nào chắc chắn có thể chạy nhiều lần?
Khi nào onCreate của service được gọi so với onBind?
- Callback chính: onCreate(), onBind(), onUnbind(), onDestroy().
- onBind() có thể chạy nhiều lần (mỗi lần có client bind mới).
- onUnbind() có thể chạy nhiều lần theo vòng đời bind/unbind.
- onCreate() chỉ chạy 1 lần cho 1 instance service, và chạy trước onBind() đầu tiên.

4. BIND_AUTO_CREATE giải quyết bài toán gì và tạo side effect gì?
- Giải quyết: nếu service chưa tồn tại thì hệ thống tạo service để bind thành công.
- Side effect: service có thể sống lâu hơn dự kiến nếu client không unbind đúng cách; tăng memory/resource footprint.

5. Nếu client bị kill bất ngờ, service lifecycle bị ảnh hưởng ra sao?
- Client bị kill thường KHONG kịp gọi onDestroy()/unbind thủ công.
- Hệ thống sẽ xử lý disconnect binder; nếu không còn client nào bind thì service có thể bị destroy.
- Vì vậy cần thiết kế cleanup bằng DeathRecipient/registry cleanup, không trông chờ client dọn dẹp đẹp.

6. Vì sao AIDL phù hợp hơn local binder trong use case multi-process?
- AIDL tạo IPC contract rõ ràng, generate Stub/Proxy tự động, chuẩn cho giao tiếp cross-process.
- Local binder phù hợp same-process; qua process khác sẽ không đủ contract và không scale tốt.

7. oneway trong AIDL mang ý nghĩa gì về blocking behavior của caller?
- Caller non-blocking: gửi transaction rồi đi tiếp, không chờ method thực thi xong.
- oneway không có return value đồng bộ; kết quả nên trả qua callback/event khác.

8. Nếu callback là oneway, thứ tự callback có luôn deterministic không?
- Không nên giả định deterministic tuyệt đối giữa nhiều luồng/sender.
- Cần requestId/sequence để correlate và xử lý out-of-order an toàn.

9. Điểm khác nhau giữa interface contract và implementation contract trong Android IPC là gì?
- Interface contract: định nghĩa trong .aidl (method signature, parcelable shape, direction in/out/oneway).
- Implementation contract: cách service hiện thực Stub, validate input, xử lý lỗi, thread-safety, timing.

10. Nếu cần explain cho senior trong 30 giây về process boundary:

"Process boundary là lớp cách ly giữa các tiến trình để bảo vệ memory, quyền truy cập, và độ ổn định. Mọi call qua boundary phải đi bằng IPC contract (AIDL/Binder), nên có thể kiểm soát permission, validate dữ liệu, và giới hạn blast radius khi một process crash."

11. 3 rủi ro lớn khi mới dùng AIDL:
- Quên lifecycle cleanup (không unregister/unlinkToDeath) -> leak callback/resource.
- Bỏ qua concurrency trên binder thread -> race condition, crash khó tái hiện.
- Phá compatibility contract (đổi field/method thiếu chiến lược) -> client cũ lỗi runtime.


-----------------------------------------------------------------------------------------------------------------
Day2:

1. Tại sao minSdk 26 là một lựa chọn hợp lý cho project này?
- API 26 đủ hiện đại cho nền tảng service/binder ổn định, toolchain tốt, và vẫn tương thích phần lớn thiết bị mục tiêu automotive/dev test hiện tại.

2. Bạn sẽ kiểm tra điều gì đầu tiên để biết skeleton project sạch trước khi thêm feature?
- build.gradle (namespace, minSdk/targetSdk, signing, dependencies).
- applicationId và package structure có nhất quán không.
- Build/debug variant chạy được, test task cơ bản chạy được.
- Manifest merge không warning nghiêm trọng.

3. app namespace, applicationId và package khác nhau như thế nào?
- namespace: dùng cho R/BuildConfig và code generation khi build.
- applicationId: định danh app khi cài đặt lên thiết bị (duy nhất trên device).
- package (source): package khai báo trong file Kotlin/Java/AIDL; là cấu trúc mã nguồn.

4. Vì sao từ đầu phải xác định rõ package com.vdiag cho toàn bộ AIDL và Java?
Nếu app chạy Hello World nhưng Gradle setup sai, vấn đề gì sẽ nổ ở tuần 2?
- Cần thống nhất package để tránh mismatch classpath, import và Binder descriptor giữa client/service.
- Lỗi thường nổ ở tuần 2: AIDL không resolve đúng type, bind thất bại runtime, manifest/service name mismatch, instrumentation test không tìm đúng appId/runner, refactor rất tốn công.


-----------------------------------------------------------------------------------------------------------------
Day3:

1. Vì sao tách HAL thành standalone CMake project chạy trên Linux host là quyết định tốt?
- Tách domain risk: verify core logic/toolchain sớm mà chưa phụ thuộc Android integration.
- Build/test nhanh hơn, feedback loop ngắn hơn.

2. HAL standalone giúp giảm rủi ro gì so với build tất cả qua Android NDK ngay từ đầu?
- Giảm rủi ro trộn lỗi logic với lỗi tích hợp NDK/Gradle/JNI.
- Khoanh vùng lỗi rõ hơn: fail do HAL hay fail do integration layer.

3. CMakeLists tối thiểu cần những phần nào để scale từ dummy lên library thật?
- cmake_minimum_required + project.
- add_library cho core.
- target_sources, target_include_directories.
- target_compile_features/definitions/options cơ bản.
- add_executable test harness + target_link_libraries.

4. Include path management sai sẽ gây bug kiểu gì khi project lớn dần?
- Include nhầm header cùng tên, phụ thuộc ẩn, ODR/link lỗi, và bug môi trường (máy A build được, máy B fail).

5. Mục tiêu thực sự của dummy.cpp trong Day3 là gì ngoài return 42?
- Smoke test cho build graph, include path, link path, và artifact output.

6. Vì sao build green ở Day3 là checkpoint kiến trúc chứ không chỉ "biên dịch được"?
- Vì nó chứng minh skeleton có thể mở rộng: thêm module mới mà không phá cấu trúc build.

7. Nếu viết test harness sớm cho HAL, bạn đặt boundary test ở đâu?
- Test ở public HAL interface: input hợp lệ/không hợp lệ, error code, timing cơ bản.
- Không test sâu implementation private ở giai đoạn skeleton.

8. Khi nào nên split thành static lib và executable test targets trong CMake?
- Khi core logic cần tái sử dụng bởi nhiều binary/test.
- Khi muốn unit test nhanh, độc lập với app chính.


-----------------------------------------------------------------------------------------------------------------
Day4:

1. IDiagCarService nên expose method nào và vì sao?
- getProperty(request, callback) hoặc submitRequest(request).
- registerCallback(callback) và unregisterCallback(callback).
- Bộ API này đủ cho request-response async + quản lý lifecycle callback.

2. Vì sao registerCallback và unregisterCallback phải là API first-class ngay từ đầu?
- Callback lifecycle là invariant quan trọng của hệ async.
- Thiếu unregister sẽ dễ leak binder/death recipient và gây callback vào client đã chết.

3. DiagRequest nên giữ field nào ở giai đoạn này để không over-design?
- requestId (bắt buộc).
- propertyId hoặc commandId (bắt buộc).
- payload tối thiểu (vd: String/byte[]), tránh nhồi nhiều field chưa dùng.

4. Vì sao dùng Parcelable object thay vì truyền từng primitive params?
- Gom dữ liệu thành 1 contract versionable.
- Dễ mở rộng field mới hơn, giảm vỡ chữ ký method AIDL.

5. Callback contract ở IDiagCallback.aidl cần giữ backward-compatible điểm nào?
- Tên method và chữ ký method không đổi tùy tiện.
- Ý nghĩa field hiện có không đổi.
- Nếu thêm field parcelable thì giữ default/optional để client cũ vẫn đọc được.

6. Ý nghĩa của requestId trong async system là gì?
- Correlate request-response khi callback đến chậm, song song, hoặc đảo thứ tự.

7. Nếu callback về chậm hoặc out-of-order, requestId cứu bạn như thế nào?
- Map response về đúng request đang chờ, tránh cập nhật nhầm UI/state.

8. Phân biệt request semantic và transport payload semantic trong DiagRequest?
- Request semantic: "muốn làm gì" (vd: đọc tốc độ xe).
- Transport payload semantic: "đóng gói truyền như thế nào" (field, format, direction).

9. Vì sao AIDL package naming sai có thể build pass nhưng runtime fail?
- Có thể vẫn compile ở một module, nhưng lúc bind runtime descriptor/interface token không khớp giữa client-service -> transact fail hoặc cast fail.

10. Nếu senior hỏi "why this IPC API is minimal but sufficient", bạn defend ra sao?
- API chỉ giữ 3 trục bắt buộc: gửi request, nhận async callback, và quản lý đăng ký callback.
- Ít surface area hơn nên dễ secure, dễ test, và chưa khóa cứng thiết kế quá sớm.


-----------------------------------------------------------------------------------------------------------------
Day5:

1. Vì sao tách android, hal, docs ở repo root là kiến trúc tốt?
- Tách concern rõ ràng: app layer, native/hal layer, tài liệu.
- Dễ ownership theo nhóm, dễ CI theo module, giảm xung đột khi phát triển song song.

2. CI Day5 nên kiểm tra tối thiểu những gì để có giá trị thật?
- Android build (debug) pass.
- HAL CMake build pass.
- AIDL compile/generate pass.
- Lint/static check cơ bản và shell script CI fail-fast.

3. README skeleton nên chứa 3 phần bắt buộc nào để người mới vào hiểu ngay project?
- Mục tiêu hệ thống + scope tuần hiện tại.
- Kiến trúc tầng (android/service/jni/hal) và boundary chính.
- Quick start: cách build/run/verify tối thiểu.

-----------------------------------------------------------------------------------------------------------------