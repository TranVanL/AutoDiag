# VDiag — Lessons Learned

## Week 1 (W1) — AAOS Study + Skeleton

- AAOS layer: `App → Car API → CarService → Vehicle HAL → ECU` — mỗi layer đều là IPC boundary
- Bound Service lifecycle: `onCreate → onBind → [active] → onUnbind → onDestroy`; dùng `BIND_AUTO_CREATE` để auto-start
- AIDL tool generate `Stub` (server, extends Binder) + `Proxy` (client, wraps IBinder); `oneway` = fire-and-forget, không block caller thread
- `hal/` CMake build standalone trên Linux cần `include_directories(include)` để tìm header đúng chỗ
- `buildFeatures { aidl true }` trong `app/build.gradle` là bắt buộc — không có thì AIDL file không được compile

## Week 2 (W2) — AIDL Service + Permission + DeathRecipient

- `android:process=":car_service"` → Android fork process mới, kiểm tra bằng `adb shell ps -A | grep vdiag`
- `android:exported="false"` trên Service — không được quên: ngăn app ngoài bind trực tiếp, bảo mật lớp 1
- `protectionLevel="signature"` cho custom permission → chỉ app cùng signing key mới được grant; khác với `dangerous` (user grant)
- Phải khai báo cả `<permission>` lẫn `<uses-permission>` trong cùng manifest nếu self-signed
- `Binder.getCallingPid/Uid()` chỉ có ý nghĩa khi gọi từ trong Binder transaction (onTransact thread); gọi ngoài trả về PID/UID của chính process
- `enforceCallingOrSelfPermission()` throw SecurityException ngay; dùng khi muốn fail fast

## Day 35 (W7D35) — Full Pipeline Audit + Lessons

- Đã verify phần wiring chính đã chạy đúng hướng: Android CMake link `hal/`, JNI tạo `DiagEngine`, và request đi theo chuỗi App -> Binder -> JNI -> Engine -> MockHal -> callback.
- Baseline HAL test đang xanh: tổng 35 test pass (`uds_encode_decode_20_tests`, `mock_hal_5_tests`, `session_state_5_tests`, `diag_engine_4_tests`).
- Đã chuẩn hóa dữ liệu mock theo expected Day34: VIN=`VINFAST12345678901`, SW=`SW_V3.2.1_AAOS`, DTC list=`P0A00, P0562`.
- Đã bổ sung thao tác thứ 6 từ UI: `Clear DTC (0x14)` đi full pipeline và decoder trả value `OK`.
- Bài học chốt tuần: milestone chỉ được coi là done khi đủ cả 3 lớp: wiring đúng, dữ liệu đúng contract, và test regression vẫn xanh sau khi sửa.