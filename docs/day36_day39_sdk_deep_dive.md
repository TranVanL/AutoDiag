# VDiag Day36-Day39 Deep Dive

## Mục tiêu của tài liệu này

Tài liệu này tổng hợp phần kiến thức cốt lõi từ Day36 đến Day39 theo góc nhìn kỹ thuật sâu, để dùng cho 3 việc:

1. Hiểu bản chất thiết kế SDK Android trong project VDiag.
2. Hiểu flow code thực tế từ app xuống Binder, JNI, engine và callback quay lại.
3. Có thể defend trước senior interviewer về các quyết định kiến trúc, tradeoff và hạn chế hiện tại.

Phần này cố ý bỏ qua UI styling và chỉ tập trung vào kiến thức Android SDK layer, AIDL contract, request correlation, callback threading và integration logic.

## Bức tranh lớn: Day36-Day39 thực chất là gì

Giai đoạn Day36-Day39 là giai đoạn chuyển project từ kiểu app gọi service trực tiếp sang kiểu app gọi qua một SDK facade nội bộ.

Trước giai đoạn này:

1. MainActivity biết quá nhiều về Binder và AIDL.
2. MainActivity tự bind service, tự giữ service reference, tự tạo request và tự xử lý callback.
3. Logic IPC bị trộn vào logic màn hình.

Sau giai đoạn này:

1. App chỉ biết DiagProperty là gì.
2. App chỉ gọi DiagClient.getProperty.
3. App chỉ implement DiagListener để nhận response.
4. Binder, callback, requestId, thread dispatch và lifecycle được gom vào SDK layer.

Đây là bước tiến từ demo code sang architecture có thể maintain và defend được.

## Các file quan trọng

1. android/app/src/main/java/com/vdiag/sdk/DiagProperty.java
2. android/app/src/main/java/com/vdiag/sdk/DiagListener.java
3. android/app/src/main/java/com/vdiag/sdk/DiagClient.java
4. android/app/src/main/java/com/vdiag/MainActivity.java
5. android/app/src/main/aidl/com/vdiag/IDiagCarService.aidl
6. android/app/src/main/aidl/com/vdiag/IDiagCallback.aidl
7. android/app/src/main/aidl/com/vdiag/DiagRequest.aidl

## Day36: Định nghĩa domain contract

Day36 không phải chỉ là tạo hai file cho đủ milestone. Đây là ngày đóng đinh vocabulary của SDK.

### DiagProperty là gì

DiagProperty là domain model đại diện cho một yêu cầu chẩn đoán mà app được phép gọi.

Nó không phải raw UDS frame, cũng không phải transport DTO AIDL. Nó đứng ở tầng business-facing SDK.

Trong project hiện tại, mỗi DiagProperty có 3 trường:

1. propId: ID ổn định ở tầng SDK/app contract.
2. udsId: metadata dùng để trace xuống tầng UDS hoặc log/debug.
3. displayName: tên dễ đọc cho app, log và UI.

Các property hiện có:

1. VIN
2. SOC
3. RPM
4. SW_VERSION
5. DTC_LIST
6. DTC_CLEAR

### Tại sao dùng enum thay vì int constants

Lý do kỹ thuật:

1. Type-safe hơn int constants.
2. IDE autocomplete tốt hơn.
3. Tất cả metadata nằm ở một nơi.
4. Dễ mở rộng thêm field trong tương lai.

Lý do kiến trúc:

1. App gọi SDK bằng semantic name thay vì magic number.
2. MainActivity không cần biết propId cụ thể là bao nhiêu.
3. Code review dễ đọc hơn vì lời gọi thể hiện intent rõ ràng.

### Vì sao có cả propId và udsId

Đây là điểm senior rất hay hỏi.

propId và udsId có thể nhìn giống nhau ở vài case, nhưng vai trò kiến trúc của chúng khác nhau:

1. propId là contract giữa app và SDK.
2. udsId là protocol metadata để map hoặc trace xuống tầng chẩn đoán.

Tư duy đúng là tách domain identity khỏi protocol identity.

Điều này quan trọng vì trong tương lai:

1. Một property có thể cần nhiều bước UDS khác nhau.
2. Một variant sản phẩm có thể đổi mapping protocol nhưng không muốn đổi API app.
3. Bạn vẫn muốn log và test được protocol-level intent mà không lộ Binder/JNI details lên UI layer.

### Hàm fromPropId dùng để làm gì

DiagProperty.fromPropId là lookup ngược từ int sang enum.

Ý nghĩa kỹ thuật:

1. Gom logic parse về một nơi.
2. Tránh viết loop enum ở nhiều chỗ.
3. Chuẩn bị cho case callback hoặc storage chỉ còn giữ propId.

### DiagListener là gì

DiagListener là app-facing async callback contract.

Nó có 2 nhánh rõ ràng:

1. onPropertyReceived cho success.
2. onError cho failure.

Điểm quan trọng nhất không phải chữ ký method, mà là semantics:

1. Callback là bất đồng bộ.
2. requestId được giữ lại để correlate request và response.
3. property được trả lại để caller không phải tự nhớ mapping.
4. latencyUs được expose vì nó là dữ liệu quan trọng để debug pipeline.

## Day37: Xây SDK facade với DiagClient

Day37 là phần trung tâm nhất về software design.

DiagClient không phải là wrapper mỏng cho AIDL. Nó là một facade hoàn chỉnh cho SDK layer.

### DiagClient giải quyết bài toán gì

1. Ẩn Binder/AIDL khỏi Activity.
2. Quản lý bind và unbind service.
3. Quản lý register và unregister callback.
4. Tạo và correlate requestId.
5. Chuẩn hóa callback về main thread.
6. Gom error handling vào một nơi.

### Vai trò của DiagClient theo pattern

1. Facade: che giấu chi tiết Binder/AIDL.
2. Adapter: chuyển callback thô từ IPC thành callback typed ở tầng app.
3. Lifecycle owner: quản lý service connection và callback registration.
4. Correlation manager: map requestId với property.

## DiagClient được thiết kế như thế nào

### 1. Service connection management

DiagClient.create tạo client và bind service ngay.

Flow:

1. Nhận Context.
2. Lấy application context để tránh leak Activity.
3. Tạo explicit intent tới DiagCarService.
4. Gọi bindService với BIND_AUTO_CREATE.

Tại sao để logic này trong SDK thay vì MainActivity:

1. Activity không nên biết chi tiết IPC.
2. Mọi màn hình về sau có thể reuse cùng một contract.
3. Lifecycle service được quản lý tập trung.

### 2. Callback registration lifecycle

Khi onServiceConnected chạy:

1. SDK convert IBinder sang IDiagCarService.
2. SDK gọi registerCallback với sdkCallback nội bộ.
3. Nếu RemoteException xảy ra, SDK clear service reference và dispatch error.

Điểm thiết kế quan trọng là sdkCallback nội bộ.

Tại sao không đưa listener app-level trực tiếp xuống Binder:

1. App-level listener không nên phụ thuộc vào AIDL types.
2. SDK cần chặn callback để correlate requestId với property.
3. SDK cần có chỗ để marshal thread.
4. SDK cần có chỗ để chuẩn hóa lỗi.

### 3. Request dispatch

Khi caller gọi getProperty:

1. Validate property khác null.
2. Tăng requestId từ AtomicInteger.
3. Check client đã close chưa.
4. Check service đã connected chưa.
5. Tạo DiagRequest AIDL object.
6. Ghi requestId -> DiagProperty vào inFlight map.
7. Gọi service.getProperty.
8. Nếu RemoteException xảy ra, remove entry khỏi map và dispatch error.

Đây là nơi quan trọng nhất của Day37.

### 4. Request correlation bằng inFlight map

Map inFlight là lời giải cho bài toán concurrency.

Vấn đề:

1. Nhiều request có thể được gửi song song.
2. Callback AIDL chỉ trả requestId và payload response.
3. Nếu không lưu mapping lúc gửi, khi callback về sẽ không biết thuộc property nào.

Lời giải:

1. Khi gửi request, lưu requestId -> property.
2. Khi callback về, lookup map bằng requestId.
3. Dispatch đúng property ra ngoài.
4. Remove entry ngay khi callback terminal về.

Tại sao phải remove ngay:

1. Tránh memory leak.
2. Tránh stale mapping.
3. Đúng semantics một request chỉ có một terminal callback.

### 5. Threading contract

Đây là phần cực kỳ quan trọng để defend.

IDiagCallback là callback Binder-side, không đảm bảo chạy trên main thread.

Điều đó có nghĩa:

1. Callback có thể đến từ Binder thread pool.
2. Nếu update UI trực tiếp từ callback đó, app có thể crash.
3. Nếu expose callback không rõ thread contract, caller sẽ dùng sai.

Thiết kế đúng trong DiagClient:

1. SDK nhận callback ở thread Binder.
2. SDK dùng Handler với Looper.getMainLooper.
3. SDK post callback ra main thread.
4. Public listener contract trở thành main-thread-safe.

Đây là điểm rất mạnh trong thiết kế, vì bạn biến một API async khó dùng thành API predictable hơn.

### 6. Error normalization

DiagClient hiện có các mã lỗi nội bộ:

1. ERR_NOT_CONNECTED
2. ERR_BIND_FAILED
3. ERR_REMOTE_EXCEPTION
4. ERR_UNKNOWN_REQUEST

Ý nghĩa:

1. ERR_NOT_CONNECTED là lỗi state ở client/service lifecycle.
2. ERR_BIND_FAILED là lỗi môi trường hoặc startup.
3. ERR_REMOTE_EXCEPTION là Binder transport failure.
4. ERR_UNKNOWN_REQUEST là inconsistency nội bộ hoặc stale callback.

Tại sao việc chuẩn hóa lỗi quan trọng:

1. App-level code không cần hiểu mọi chi tiết Binder.
2. Analytics/logging đọc lỗi rõ nghĩa hơn.
3. Test case dễ viết hơn.
4. Giảm coupling giữa UI và transport layer.

## Vai trò của AIDL trong kiến trúc này

### IDiagCarService.aidl

IDiagCarService là service contract giữa app process và car_service process.

Nó định nghĩa 3 operation:

1. registerCallback
2. unregisterCallback
3. getProperty

Điểm quan trọng là app không còn dùng interface này trực tiếp ở MainActivity nữa. Nó được bọc bởi DiagClient.

### IDiagCallback.aidl

IDiagCallback là contract để service callback ngược về app.

Đây là transport callback thô, không phải app-facing callback cuối cùng.

SDK nhận callback này trước, sau đó transform thành DiagListener.

### DiagRequest.aidl

DiagRequest là transport DTO giữa app và service.

Nó không phải domain object.

Phân biệt đúng ba tầng là rất quan trọng:

1. DiagProperty là domain model ở SDK/app.
2. DiagRequest là transport DTO qua Binder.
3. UDS request ở native side là protocol-level object.

Nếu trộn ba tầng này vào nhau, code sẽ rất khó scale.

## Day39: MainActivity trở thành consumer của SDK

Day39 chứng minh SDK Day37 usable thật sự.

MainActivity sau refactor không còn là AIDL client trực tiếp nữa.

### Trước refactor

MainActivity tự làm mọi thứ:

1. bindService
2. giữ IDiagCarService reference
3. register/unregister callback
4. build DiagRequest
5. xử lý RemoteException
6. update UI theo callback Binder thô

### Sau refactor

MainActivity chỉ còn 4 trách nhiệm chính:

1. tạo DiagClient
2. setListener
3. gọi getProperty bằng DiagProperty
4. react với callback ở mức domain

Đây là cải thiện rất lớn về separation of concerns.

### MainActivity giờ đóng vai trò gì

Nó là consumer của SDK chứ không phải participant của Binder layer.

Điều này có nghĩa:

1. Activity không cần biết AIDL callback shape ra sao.
2. Activity không cần biết request AIDL object được build thế nào.
3. Activity không cần nhớ map requestId với property.
4. Activity chỉ làm việc với abstraction có nghĩa business.

Đây là mục tiêu đúng của SDK.

## End-to-end flow hoàn chỉnh từ Day36 đến Day39

### Bước 1: App gọi SDK

MainActivity gọi DiagClient.getProperty với một DiagProperty.

### Bước 2: SDK tạo request transport

DiagClient sinh requestId mới và tạo DiagRequest AIDL object.

### Bước 3: SDK ghi correlation state

DiagClient lưu requestId -> property vào inFlight map.

### Bước 4: Binder call sang service

DiagClient gọi IDiagCarService.getProperty.

### Bước 5: Service xử lý request

Service binder tìm callback đã đăng ký và gọi JNI bridge.

### Bước 6: Native side xử lý

JNI bridge build native request, đẩy vào DiagEngine, engine encode UDS, gọi Mock HAL, decode kết quả rồi callback ngược về Java callback bridge.

### Bước 7: Binder callback quay lại app

IDiagCallback.onResult hoặc onError được gọi về app process.

### Bước 8: SDK transform callback

DiagClient nhận callback Binder-side, lookup requestId trong inFlight map, remove entry, rồi dispatch ra DiagListener trên main thread.

### Bước 9: Activity nhận callback domain-level

MainActivity chỉ nhận property, value, latency, requestId hoặc error đã được chuẩn hóa.

Đây là điểm đẹp nhất của cả flow:

1. Ở dưới là IPC callback thô.
2. Ở trên là callback typed và app-friendly.
3. DiagClient là adapter giữa hai thế giới.

## Tư duy Android quan trọng rút ra từ phần này

### 1. Activity không nên giữ transport complexity

Khi một Activity vừa là UI layer, vừa là Binder client, vừa là callback dispatcher, code sẽ rất dễ rối.

Tách SDK layer ra giúp:

1. code dễ đọc hơn
2. test dễ hơn
3. dễ reuse ở màn hình khác
4. ít phụ thuộc vào lifecycle UI hơn

### 2. Application context là quyết định đúng cho SDK client

DiagClient dùng application context để bind service.

Tại sao:

1. tránh leak Activity
2. lifecycle của service binding không gắn cứng vào một view object
3. dễ reuse nếu sau này có manager sống lâu hơn activity

### 3. Public API phải có thread contract rõ ràng

Một API callback mà không nói rõ callback chạy ở thread nào là API nguy hiểm.

Ở đây, DiagClient chốt luôn rằng listener callback được dispatch về main thread. Đây là một decision rất đúng cho Android app-facing SDK.

### 4. Async API luôn cần correlation key

Nếu response quay về bất đồng bộ, property name một mình không đủ để identify request instance.

requestId là correlation key thật sự.

Điều này đặc biệt quan trọng khi:

1. gọi cùng một property nhiều lần liên tiếp
2. bắn nhiều property song song
3. debug log multi-threaded flow

## Những điểm mạnh của thiết kế hiện tại

### 1. Layering sạch hơn rõ rệt

1. Activity ở UI/app layer.
2. DiagClient ở SDK layer.
3. AIDL ở IPC contract layer.
4. JNI/Engine/HAL ở native/backend layer.

### 2. Domain model rõ

DiagProperty và DiagListener tạo language chung cho app và SDK.

### 3. Correlation logic đúng chuẩn

inFlight map giải đúng bài toán multi-request async.

### 4. Thread-safe ở public callback boundary

Main thread dispatch giảm rủi ro UI crash và race.

### 5. Error handling có cấu trúc cơ bản

Không còn kiểu mọi lỗi đều chỉ là string vô nghĩa.

### 6. Activity giảm coupling mạnh với Binder

Đây là improvement kiến trúc lớn nhất của Day39.

## Senior Review

Phần này là review theo tư duy senior engineer: cái gì tốt, cái gì ổn cho scope hiện tại, và cái gì nếu làm production thì nên nâng cấp.

### Điểm làm tốt

1. Đã tách abstraction đúng chỗ.
MainActivity không còn nói chuyện trực tiếp với AIDL nữa.

2. Đã có domain contract rõ.
DiagProperty và DiagListener là bước rất đúng để ổn định API surface.

3. Đã giải được bài toán async correlation.
requestId + inFlight là thiết kế cần có, không phải optional.

4. Đã chuẩn hóa callback thread.
Đây là điểm trưởng thành hơn hẳn mức demo code thông thường.

5. Đã gom lifecycle service vào SDK.
registerCallback, unregisterCallback, bind và unbind được quản lý tập trung.

### Điểm còn non hoặc còn thiếu nếu xét production-grade

1. Chưa có timeout cho in-flight request.
Nếu native side treo hoặc callback không bao giờ quay về, map có thể giữ request lâu hơn mong muốn.

2. Chưa có retry policy hoặc reconnect policy rõ ràng.
Hiện tại nếu service rớt, SDK báo lỗi là đúng cho scope nhỏ, nhưng production thường cần policy rõ hơn.

3. Chưa có cancellation model.
Hiện không có API cancel request hoặc clear pending request theo lifecycle caller.

4. Error taxonomy còn khá thô.
Hiện đang dùng int code và string message. Production có thể cần object lỗi typed hơn.

5. DTC_CLEAR đang được model như property.
Điều này ổn cho scope project nhỏ, nhưng nếu domain lớn hơn thì command có side effect nên được tách API riêng để semantic chuẩn hơn.

6. Chưa có abstraction riêng cho connection state.
Hiện tại connection callback mới ở mức cơ bản. Nếu scale, nên có state model rõ như Connecting, Connected, Disconnected, Error.

7. Chưa có unit test cho Android-side SDK layer.
Phần native và HAL test đã khá ổn, nhưng DiagClient, request correlation và callback dispatch vẫn nên có unit tests hoặc instrumentation tests riêng.

### Nếu là production, tôi sẽ nâng cấp gì đầu tiên

1. Thêm timeout scheduler cho inFlight request.
2. Đổi error từ int/string sang typed error object.
3. Tách connection state thành model rõ ràng.
4. Thêm test cho DiagClient.
5. Cân nhắc tách command API khỏi property API cho các action có side effect như Clear DTC.

## Câu hỏi senior interviewer rất hay hỏi và cách defend

### Tại sao không để Activity gọi AIDL trực tiếp

Vì Activity là UI layer, không nên gánh transport detail. Khi tách ra thành DiagClient:

1. code dễ maintain hơn
2. tái sử dụng được
3. test dễ hơn
4. tách lifecycle UI khỏi lifecycle IPC tốt hơn

### Tại sao callback cần requestId trong khi đã có property

Vì cùng một property có thể được gọi nhiều lần song song. property chỉ nói request thuộc loại gì, còn requestId mới xác định được instance cụ thể của request.

### Tại sao phải marshal callback về main thread

Vì AIDL callback không đảm bảo chạy ở main thread. Android UI chỉ an toàn khi update trên main thread. SDK phải làm điều này để public API predictable và safe hơn.

### Tại sao cần inFlight map

Vì callback Binder-side không mang đầy đủ domain context. SDK phải lưu context ở thời điểm gửi để reconstruct domain-level callback khi response về.

### Tại sao dùng enum thay vì int constants

Vì enum cho type safety, readability, metadata attachment và API surface rõ ràng hơn.

### Tại sao DiagRequest không phải domain object

Vì nó chỉ là transport DTO qua Binder. Domain object của SDK là DiagProperty. Native/protocol layer lại có object riêng. Tách 3 tầng này ra giúp code scale tốt hơn.

## Cách tự kể lại phần này ngắn gọn khi phỏng vấn

Bạn có thể tóm tắt như sau:

1. Day36 tôi định nghĩa domain contract cho SDK bằng DiagProperty và DiagListener.
2. Day37 tôi xây DiagClient như một facade cho Binder/AIDL, chịu trách nhiệm bind service, register callback, gửi request và correlate response bằng requestId map.
3. Tôi chuẩn hóa public callback về main thread bằng Handler main looper để UI-safe.
4. Tôi chuẩn hóa lỗi ở tầng SDK bằng internal error code thay vì để Activity xử lý transport detail.
5. Day39 tôi chuyển MainActivity thành consumer của SDK, bỏ hoàn toàn coupling trực tiếp với AIDL.

## Checklist tự đánh giá là đã hiểu sâu hay chưa

Nếu bạn trả lời chắc các câu dưới đây mà không nhìn code, nghĩa là bạn đã hiểu khá sâu:

1. DiagProperty khác gì với DiagRequest.
2. Vì sao cần requestId dù đã có property.
3. Vì sao callback phải đi qua DiagClient thay vì đưa thẳng ra Activity.
4. Vì sao phải dispatch callback về main thread.
5. Vì sao inFlight map là cần thiết.
6. Error code nội bộ ở DiagClient dùng để giải quyết vấn đề gì.
7. Điểm mạnh và hạn chế của việc model DTC_CLEAR như property.
8. Nếu production hóa phần này, bạn sẽ nâng cấp gì đầu tiên.

## Kết luận

Day36-Day39 là phần chuyển project từ một app demo gọi Binder trực tiếp thành một app có SDK layer nội bộ tương đối đúng chuẩn.

Giá trị lớn nhất không phải ở số dòng code, mà ở chỗ bạn đã bắt đầu làm đúng những việc sau:

1. Tách domain khỏi transport.
2. Tách UI khỏi IPC.
3. Tách callback thô khỏi callback app-facing.
4. Quản lý async request bằng correlation key.
5. Định nghĩa thread contract rõ ràng cho public API.

Đây là nền tảng rất tốt để về sau mở rộng sang manager thật, multiple screens, reconnection policy hoặc production-level Android service SDK.