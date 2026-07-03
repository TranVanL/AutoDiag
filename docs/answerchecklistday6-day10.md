Day 6:

1. Tại sao service nên chạy process riêng thông qua AndroidManifest?
- Tách crash domain giữa UI và service.
- Giảm tác động của ANR, memory leak, hoặc native crash lên app client.
- Cho phép service có lifecycle và resource budget riêng, dễ kiểm soát hơn khi sau này nhiều client cùng dùng.

2. Ý nghĩa bảo mật của exported false với service này là gì?
- Chỉ thành phần trong cùng application/UID mới có thể bind vào service.
- Giảm attack surface, tránh app ngoài gọi trực tiếp vào IPC entrypoint.
- Kết hợp với permission gate sẽ an toàn hơn cho service nội bộ.

3. onCreate của service nên khởi tạo những thành phần nào và chưa nên khởi tạo gì?
- Nên khởi tạo: binder stub, permission gate, registry/collections, executor hoặc handler cần cho IPC, các state tối thiểu để bind được ngay.
- Chưa nên khởi tạo: kết nối JNI nặng, HAL session lớn, resource tốn thời gian nếu chưa thật sự cần.

4. onDestroy phải cleanup gì để tránh leak binder/native resources?
- Unregister callback, unlinkToDeath, clear registry.
- Shutdown executor/thread, đóng native handle/JNI context nếu có.
- Đảm bảo cleanup idempotent để gọi nhiều lần vẫn an toàn.

5. Nếu service init JNI fail, service nên fail fast hay degraded mode?
- Nếu JNI là đường đi chính của business logic thì nên fail fast.
- Chỉ dùng degraded mode khi còn một luồng chức năng đủ nghĩa mà không cần JNI.
- Không nên giữ service sống nhưng ở trạng thái nửa hỏng cho logic cốt lõi.

6. DiagCarService và DiagCarServiceBinder nên tách trách nhiệm như thế nào?
- DiagCarService: lifecycle, process boundary, tạo binder, giữ state cấp service, cleanup.
- DiagCarServiceBinder: implement từng method AIDL, validate input, quản lý callback, gọi xuống core/JNI.

7. Vì sao service class không nên nhồi logic nghiệp vụ nặng?
- Service nên mỏng để dễ đọc, dễ kiểm soát lifecycle, dễ test.
- Logic nặng đặt ở manager/core để tách trách nhiệm rõ và giảm rủi ro deadlock/ANR trong entrypoint IPC.

8. Nếu bị kill bởi hệ thống, service state nào cần recover?
- Registry callback, pending request, session context, native/JNI handles, worker threads nếu cần.
- State chỉ nên recover nếu nó thực sự cần cho continuity; còn lại nên dựng lại sạch.

9. Bạn verify đúng multi-process setup bằng command nào và kỳ vọng output gì?
- Dùng adb shell ps -A | grep <package> hoặc adb shell pidof <package>.
- Kỳ vọng thấy ít nhất 2 process/PID khác nhau: process app chính và process service riêng, thường có suffix như :diag.

10. Nếu interviewer hỏi “why process split now, not later”, bạn trả lời sao?
- Vì tách process sớm làm rõ boundary trước khi logic phình to.
- Nó giúp phát hiện sớm lỗi IPC, permission, lifecycle, và resource ownership trước khi tích hợp JNI/HAL.


Day 7:

1. Trong Binder method getProperty, bước validate tối thiểu là gì?
- Enforce permission/caller identity.
- Kiểm tra request không null, requestId hợp lệ, propertyId/commandId hợp lệ.
- Kiểm tra callback/registry state nếu flow là async.

2. Bạn log callingPid và callingUid để làm gì, và khi nào log này không đáng tin?
- Dùng để audit, debug, và trace ai đang gọi IPC.
- Không nên coi đó là business identity duy nhất, vì một UID có thể đại diện cho nhiều thành phần và PID thay đổi theo process lifecycle.

3. Vì sao response path phải đi qua callback thay vì return trực tiếp?
- Vì kết quả có thể đến sau, hoặc cần tách khỏi binder thread để tránh block.
- Callback cho phép async request-response rõ ràng và mở rộng tốt hơn.

4. Khi callback null hoặc không đăng ký, behavior đúng là gì?
- Trả lỗi ngay, không âm thầm drop request.
- Đây là lỗi contract hoặc state lỗi của client, cần fail rõ ràng để debug được.

5. Dummy response stage giúp de-risk phần nào trước JNI integration?
- De-risk binder chain, AIDL contract, callback flow, threading, và end-to-end plumbing.
- Khi dummy đã chạy đúng thì lúc gắn JNI chỉ còn khoanh vùng lỗi ở lớp native/hal.

6. Nếu request malformed, bạn trả error thế nào để không crash service?
- Validate sớm, log ngắn gọn, trả mã lỗi/exception phù hợp.
- Không gọi xuống JNI/HAL khi request đã sai contract.

7. Sự khác nhau giữa lỗi transport và lỗi business trong binder layer?
- Transport lỗi là lỗi IPC hạ tầng: bind fail, transact fail, RemoteException, chết process, marshalling lỗi.
- Business lỗi là request hợp lệ về IPC nhưng sai nghiệp vụ: property không hỗ trợ, state không cho phép, dữ liệu không hợp lệ.

8. Nếu hai client gọi cùng lúc, binder implementation cần thread-safe ở đâu?
- Ở registry, pending-request map, shared session/state, và mọi native shared resource.
- Biến local trong method thường không cần lock, nhưng mọi shared mutable state phải được bảo vệ.

9. Vì sao không được block lâu trong binder thread?
- Binder thread pool hữu hạn, block lâu sẽ nghẽn các transaction khác.
- Có thể gây back-pressure, timeout, hoặc ANR ở client.

10. Nếu callback throw RemoteException, service nên xử lý như thế nào?
- Catch exception, coi callback đó là dead/unreachable nếu cần.
- Cleanup registry, unlinkToDeath, và không để exception làm crash service.


Day 8:

1. Signature permission khác dangerous permission ở điểm cốt lõi nào?
- Signature permission chỉ cấp cho app ký cùng cert với app/service định nghĩa permission.
- Dangerous permission là permission người dùng phải cấp runtime, không phù hợp để làm gate chính cho IPC nội bộ.

2. Vì sao cần cả permission declaration và uses-permission trong manifest?
- Service phải declare permission để chặn caller không hợp lệ.
- Client phải uses-permission để xin quyền đó và làm rõ intent sử dụng.

3. PermissionGate nên enforce ở entrypoint nào để không bypass?
- Ngay tại đầu mỗi method AIDL/binder entrypoint, trước khi đụng vào state hay logic nghiệp vụ.
- Nếu có nhiều method, nên gom vào helper chung để tránh quên một method.

4. Tại sao fail fast bằng SecurityException là hành vi đúng ở IPC boundary?
- Vì đây là boundary bảo mật, caller không hợp lệ phải bị chặn ngay.
- Fail fast giúp rõ lỗi, tránh chạy nửa vời rồi chạm vào resource nhạy cảm.

5. Nếu app cùng package nhưng ký key khác, behavior mong đợi là gì?
- Nếu dùng signature permission thì bị từ chối vì khác cert.
- Nếu service exported=false thì chỉ same app/UID mới vào được; khác UID vẫn không truy cập được.

6. Nếu quên enforce ở một method binder, attack surface cụ thể là gì?
- Method đó trở thành cửa mở cho app ngoài gọi vào IPC không kiểm soát.
- Có thể dẫn tới đọc/sửa state trái phép, lộ dữ liệu, hoặc kích hoạt hành vi nguy hiểm.

7. Bạn viết test scenario nào để chứng minh permission đang thực thi thật?
- Tạo app client không có permission rồi bind/call service, kỳ vọng SecurityException hoặc bind fail.
- Tạo app cùng cert nhưng khác client flow hợp lệ để xác nhận permission cho phép đúng đối tượng.

8. Với process riêng, permission model bổ sung thêm bảo vệ gì so với same-process?
- Có thêm boundary IPC để enforce UID/PID và permission rõ ràng.
- Lỗi client không còn truy cập trực tiếp vào object nội bộ của service như cùng process.

9. Nếu interview hỏi “defense-in-depth ở service này là gì”, bạn kể đủ lớp nào?
- exported=false, signature permission, kiểm tra caller ở entrypoint, validate request, registry/death cleanup, và hạn chế shared mutable state.

10. Nếu quên enforce ở một method binder, attack surface cụ thể là gì?
- Lặp lại: method đó có thể bị gọi trái phép, nên phải rà từng entrypoint để không có lỗ hổng lệch nhau giữa các API.


Day 9:

1. Vì sao phải có ClientRegistry thay vì giữ một callback variable duy nhất?
- Có thể có nhiều client/callback đồng thời.
- Registry giúp quản lý lifecycle riêng từng client, tránh callback của client này đè client kia.

2. Key trong map nên là gì và vì sao?
- Nên key bằng binder token của callback, tức IBinder của callback, hoặc một client token ổn định.
- UID/PID chỉ nên dùng làm metadata, vì PID có thể tái sử dụng và không đại diện duy nhất cho callback object.

3. linkToDeath giải quyết đúng bài toán gì?
- Phát hiện client process chết để service cleanup chủ động.
- Tránh giữ callback sống giả và gửi response vào binder đã chết.

4. binderDied callback phải làm tối thiểu 3 việc nào?
- Remove client khỏi registry.
- Cleanup pending state/death recipient liên quan.
- Báo cho business layer hoặc metric/log rằng client đã chết.

5. unlinkToDeath cần gọi ở đâu để tránh rò tài nguyên?
- Khi unregister chủ động và khi cleanup do binder death/onDestroy.
- Mục tiêu là đảm bảo không còn death recipient treo trên binder đã hết hạn.

6. Vì sao ConcurrentHashMap là chọn phù hợp ở đây?
- Registry bị truy cập từ nhiều binder threads và death recipient callbacks.
- ConcurrentHashMap giảm nhu cầu lock lớn cho thao tác đọc/ghi phổ biến và dễ scale hơn map thường.

7. Nếu một client register callback hai lần, policy đúng nên là gì?
- Nên reject hoặc replace có kiểm soát, nhưng phải nhất quán một policy duy nhất.
- Với service async, reject duplicate thường an toàn hơn vì tránh trạng thái mơ hồ.

8. Nếu client process chết giữa request đang xử lý, service phải đảm bảo điều gì?
- Không được callback vào binder đã chết.
- Phải cleanup pending request và không giữ tài nguyên vô thời hạn.

9. cleanup toàn bộ registry trong onDestroy có cần idempotent không?
- Có, vì onDestroy/cleanup có thể đến từ nhiều nhánh, và cleanup an toàn khi gọi lặp lại sẽ giảm bug race.

10. Điểm khác nhau giữa client unregister chủ động và binder death bị động?
- Unregister chủ động là client tự nói đã xong, service cleanup có kiểm soát.
- Binder death là service phát hiện client chết bất ngờ và cleanup theo sự kiện hệ thống.

11. Nếu callback object bị stale, làm sao tránh gửi response vào “xác chết” binder?
- Check alive trước khi callback, catch RemoteException, và xóa entry stale khỏi registry.

12. Race condition nào có thể xảy ra giữa unregister và binderDied?
- Double remove, remove trước rồi binderDied chạy sau, hoặc callback đang được lấy ra để gửi trong lúc entry bị xóa.
- Cần atomic cleanup và policy rõ cho registry operations.

13. Bạn sẽ thêm metric/log gì để quan sát health của registry trong runtime?
- Số client đang active, số register/unregister, số binderDied, số callback fail, và latency request-to-callback.

14. Cách verify DeathRecipient bằng force-stop như thế nào để chắc chắn đúng?
- Force-stop client bằng adb shell am force-stop <package>.
- Kỳ vọng service log binderDied, xóa registry entry, và không còn callback tới client đó.

15. Nếu mở rộng nhiều callback type trong tương lai, registry design cần đổi gì?
- Dùng một ClientEntry rõ ràng chứa callback binder, deathRecipient, metadata, và loại callback.
- Tránh nhiều map rời rạc nếu chúng luôn phải thay đổi cùng nhau.


Day 10:

1. E2E path Day10 từ MainActivity tới callback gồm những bước nào?
- MainActivity bind service.
- onServiceConnected nhận binder.
- Client register callback.
- Client gửi request.
- Service validate, xử lý, rồi gọi callback trả kết quả về UI.

2. Dấu hiệu nào trong log chứng minh request đi qua đúng binder chain?
- Có log ở client bind/connect, log service nhận request, log service gửi callback, và log client nhận callback.
- Các mốc này phải đi theo đúng thứ tự request lifecycle.

3. Làm sao phân biệt service connect thành công với callback register thành công?
- Service connect thành công chỉ nói binder đã nối được.
- Callback register thành công là state riêng sau đó, chứng minh service đã lưu callback và có thể dùng để trả kết quả.

4. Bạn nên disable action buttons khi disconnected để tránh bug gì?
- Tránh gọi request vào binder null hoặc binder đã chết.
- Giảm race giữa UI action và lifecycle connect/disconnect.

5. Nếu bind fail, UI nên phản ứng ra sao cho đúng semantics?
- Hiển thị trạng thái disconnected hoặc error rõ ràng.
- Không cho user bấm các action phụ thuộc service cho đến khi bind lại thành công.

6. Khi app bị force-stop, kỳ vọng ở service side logs là gì?
- Service nhận binder death, cleanup registry, và không còn callback tới client đó.
- Nếu service ở process riêng, service vẫn sống nếu còn client khác hoặc vẫn còn logic hợp lệ để giữ.

7. 2 process verify bằng ps giúp phát hiện lỗi cấu hình nào?
- Phát hiện service chưa chạy process riêng thật sự.
- Phát hiện manifest process name sai hoặc service vẫn bị gom chung process với app chính.

8. Nếu callback không về nhưng service nhận request, bạn debug theo thứ tự lớp nào?
- Kiểm tra registry/callback đã đăng ký chưa.
- Kiểm tra service có gọi callback không và có RemoteException không.
- Kiểm tra client process còn sống và callback object còn valid không.

9. RequestId có vai trò gì ngay cả khi Day10 mới có request đơn?
- Nó giúp log/trace request-response rõ ràng.
- Dù chỉ có một request, requestId vẫn chuẩn bị cho async, retry, và out-of-order trong tương lai.

10. Với Day10, checklist pass thực chiến tối thiểu gồm những test case nào?
- Bind thành công, register callback thành công, request thành công, callback về UI.
- Disconnect/reconnect hoạt động đúng.
- Force-stop client thì service cleanup đúng.
- Bind fail hoặc permission fail thì UI và log phản ứng đúng.

11. Nếu senior hỏi “what can still fail after Day10”, bạn liệt kê gì?
- Permission/manifest mismatch.
- Callback death/registry cleanup sai.
- Threading issue trên binder thread.
- RequestId/correlation chưa đủ cho async nhiều client.
- JNI/HAL vẫn chưa được gắn vào flow thật.

12. Bạn tóm tắt technical debt còn lại từ Day10 trước khi lên JNI như thế nào?
- Contract đã có nhưng chưa có native business thật.
- Cần thêm observability, error mapping, concurrency hardening, và compatibility strategy trước khi chạm vào HAL/JNI.


Checklist cross-cutting cấp senior cho Day1-Day10:

1. Đâu là API boundary rõ nhất của hệ thống ở tuần 1-2?
- AIDL service interface và callback interface.

2. Đâu là trust boundary rõ nhất của hệ thống ở tuần 1-2?
- IPC boundary giữa app client và service process.

3. Đâu là chỗ có khả năng tạo deadlock hoặc ANR trong thiết kế hiện tại?
- Binder thread bị block lâu, hoặc lock chéo giữa registry, callback, và worker thread.

4. Đâu là điểm có nguy cơ memory leak cao nhất trước Day11?
- Registry callback, DeathRecipient, và native/JNI resources không được cleanup đúng.

5. Đâu là invariant quan trọng nhất của callback lifecycle?
- Chỉ callback cho client còn sống và đã đăng ký hợp lệ; unregister/death phải xóa sạch state liên quan.

6. Nếu phải viết threat model mini cho Day1-Day10, bạn viết 5 threat nào?
- Unauthorized bind/call.
- Stale callback sau client death.
- Malformed request gây crash/UB.
- Binder thread exhaustion/ANR.
- Race condition giữa unregister và binderDied.

7. Nếu phải thêm observability ngay bây giờ, bạn thêm metric nào ở service?
- Active clients, request count, callback success/fail, binder death count, latency, permission reject count.

8. Nếu phải onboard intern vào code này, 3 file nào bắt họ đọc đầu tiên?
- README.
- AIDL interface files.
- Service/binder implementation và architecture docs liên quan.

9. Điều gì đang "just enough" và điều gì đang "under-engineered"?
- Just enough: requestId, callback registry, permission gate, basic E2E bind flow.
- Under-engineered: observability, retry policy, versioning/compatibility, và concurrency hardening chi tiết.

10. Một thay đổi nhỏ nào có thể phá vỡ compatibility AIDL và cách phòng tránh?
- Đổi chữ ký method hoặc thay đổi Parcelable field không có chiến lược versioning.
- Phòng tránh bằng append-only field, giữ tên/method cũ, và thêm version nếu cần.

11. Bạn sẽ commit message chuẩn hóa thế nào để trace từ requirement tới code?
- Theo format rõ mục tiêu, ví dụ: Day9: add ClientRegistry and DeathRecipient cleanup for callback lifecycle.

12. Nếu rollback nhanh về mốc Day10, file nào là critical path cần giữ nguyên?
- AIDL contracts, service/binder implementation, manifest/process config, registry/death handling, và client bind flow.


