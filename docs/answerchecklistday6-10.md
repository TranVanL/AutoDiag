Day 6 : 
1. Tại sao service nên chạy process riêng thông qua AndroidManifest? 
Service run a invidual process because we split app logic and service logic , service can be used for many apps and may potential control itself lifecycle
 
2. Ý nghĩa bảo mật của exported false với service này là gì? 
- Only internal client can bind and start service , no any app external can do it 
-> Binder only receive request from process in the same package -> Ensure for AIDL contract work properly and avoid call wrong from external
 
3. onCreate của service nên khởi tạo những thành phần nào và chưa nên khởi tạo gì? 
- Only core components that necessary for init , Binder stub (really important cause it have to ready when client bind it immediately ) 
Other components just init when we use it , It can weight and heavy , spend a lot of times and resources .
 
4. onDestroy phải cleanup gì để tránh leak binder/native resources? 
Ondestroy() naturally clean up binder stup  , unbid callback , release resource  ,... to avoid leak resource
 
5. Nếu service init JNI fail, service nên fail fast hay degraded mode? 
- From my perspective , service should fail fast because jni is the bridge to core logic , it play crucial role for handle logic  business , If it fail , service clearly can't request or receive , interact with HAL -> Ko còn tác dụng gì  , still run may make a huge issue -> Should fail fast and retry to look for the next one . 
Only degraded mode if JNI is optional feature , don't responsible for main logic .
 
6. DiagCarService và DiagCarServiceBinder nên tách trách nhiệm như thế nào? 
- DiagCarService should be extend Service and init component essential like Binder stub and registry 
- DiagCarServiceBinder should implement how it run when receive request from Client , handle register , unregister , get the right callback , call down to JNI Bridge .
 
7. Vì sao service class không nên nhồi logic nghiệp vụ nặng?
Cause it should be entry point for IPC and lifecycle management 
heavy logic only stuff in DiagCarServiceBinder , it implement logic business to meet IPC communication 
-> Only heavy when  has something run require IPC , service always stable smoothly
 
8. Nếu bị kill bởi hệ thống, service state nào cần recover? 
- Binder stub , registration state , session state , critical resource and pending task
 
9 .Bạn verify đúng multi-process setup bằng command nào và kỳ vọng output gì? 
adb shell get ps 
-> expectation : two process at least
 
------------------------------------------------------------------------------------------------------------------
 
Day 7 : 
1. Trong Binder method getProperty, bước validate tối thiểu là gì?
It is check bind connection and service survive , check parameter , ....
 
2. Bạn log callingPid và callingUid để làm gì, và khi nào log này không đáng tin? 
Log show what app has bind and call this function and check permission to bind 
-> Log isn't reliable when request across multi apps , a lot of apps share the same UID
 
3. Khi callback null hoặc không đăng ký, behavior đúng là gì? 
-> Should fail fast , show error right away , notify issue because without callback , getProperty successful still doesn't make any sense .
 
4. Dummy response stage giúp de-risk phần nào trước JNI integration? 
Ensure and de-risk that HAL has already work well
 
5. Nếu request malformed, bạn trả error thế nào để không crash service? 
-> Fast fail , early validate , return error , banned to call down JNI to avoid crash or UB
 
6. Nếu hai client gọi cùng lúc, binder implementation cần thread-safe ở đâu? 
In place shared common like map , registry , JNI call (lock mutex ,.... ) 
Variable through method isn't necessary because it place in invidual thread and safety
 
7 . Vì sao không được block lâu trong binder thread?
Avoid stuck flow because binder thread is limited and responsible for a lot of request -> Avoid app wait long time
 
8. Nếu callback throw RemoteException, service nên xử lý như thế nào? 
Wrap try catch , it call back fail -> remove out of list regis , avoid crash , call client has died .