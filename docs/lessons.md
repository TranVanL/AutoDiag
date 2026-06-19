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