# 🌐 Module — DoIP / ISO 13400 + Python ECU Simulator

> **Phase 9** (Tuần 9, NICE — có thể cut nếu trễ). Mục tiêu: app đọc VIN qua **TCP socket thật** từ Python ECU simulator chạy trên host.

---

## 1. ISO 13400 (DoIP) tóm tắt

DoIP = **Diagnostic over IP** — wrap UDS payload trong TCP/UDP frame để chạy trên Ethernet (thay vì CAN). Default port: **TCP 13400** cho diagnostic message.

### DoIP header (8 bytes)

| Offset | Field | Size | Value |
|---|---|---|---|
| 0 | Protocol Version | 1 | `0x02` (ISO 13400-2:2012) |
| 1 | Inverse Protocol Version | 1 | `0xFD` (= ~0x02) |
| 2-3 | Payload Type | 2 | `0x8001` = Diagnostic Message |
| 4-7 | Payload Length | 4 | length of payload (big-endian) |

### Diagnostic Message payload

| Offset | Field | Size |
|---|---|---|
| 0-1 | Source Address (tester) | 2 (e.g. `0x0E00`) |
| 2-3 | Target Address (ECU) | 2 (e.g. `0x1234`) |
| 4+ | UDS payload | N |

### Ví dụ: Read VIN

**Request bytes:**
```
02 FD 80 01  00 00 00 07   0E 00  12 34   22 F1 90
└──────┬──────────┬─────┘  └─src─┘└─tgt─┘└─UDS─┘
   header     length=7
```

**Response bytes:**
```
02 FD 80 01  00 00 00 18   12 34  0E 00   62 F1 90  56 49 4E 46 41 53 54 ...
                                          └─UDS positive: 0x62 + DID + "VINFAST..."─┘
```

---

## 2. Python ECU simulator design

### File layout

```
tools/ecu_simulator/
├── doip_server.py       ← TCP listener + DoIP frame parser
├── ecu_db.py            ← DID database (mirror MockHal)
└── test_sim.py          ← unit test bằng socket client
```

### `ecu_db.py` (~50 dòng)

```python
DID_DATABASE = {
    0xF190: b"VINFAST12345678901",
    0xF195: b"SW_V3.2.1_AAOS",
    0xFD01: bytes([78]),               # battery SOC %
    0xFE01: bytes([0x0C, 0x80]),       # 3200 RPM
}

DTC_LIST = [b"\x00\x0A\x00", b"\x05\x62\x00"]   # P0A00, P0562

class EcuState:
    def __init__(self):
        self.dtc_list = list(DTC_LIST)
    def clear_dtc(self):
        self.dtc_list = []
```

### `doip_server.py` (~150 dòng) — skeleton

```python
import socket, struct, logging
from ecu_db import DID_DATABASE, EcuState

PORT = 13400
DOIP_HEADER = struct.Struct(">BBHI")  # ver, inv_ver, payload_type, length

def handle_uds(payload: bytes, state: EcuState) -> bytes:
    """Parse UDS request, return UDS response bytes."""
    sid = payload[0]
    if sid == 0x22:  # ReadDID
        did = (payload[1] << 8) | payload[2]
        if did in DID_DATABASE:
            return bytes([0x62, payload[1], payload[2]]) + DID_DATABASE[did]
        return bytes([0x7F, 0x22, 0x31])  # NRC: Request Out Of Range
    elif sid == 0x14:  # ClearDTC
        state.clear_dtc()
        return bytes([0x54])
    elif sid == 0x19:  # ReadDTC
        if payload[1] == 0x02:  # reportDTCByStatusMask
            dtcs = b"".join(d + b"\x08" for d in state.dtc_list)
            return bytes([0x59, 0x02, 0xFF]) + dtcs
    elif sid == 0x3E:  # TesterPresent
        return bytes([0x7E, payload[1] & 0x7F])
    return bytes([0x7F, sid, 0x11])  # NRC: ServiceNotSupported

def handle_client(conn, addr):
    state = EcuState()
    logging.info(f"Client connected: {addr}")
    while True:
        # Read DoIP header
        header = conn.recv(8)
        if len(header) < 8: break
        ver, inv_ver, ptype, length = DOIP_HEADER.unpack(header)
        if ver != 0x02 or inv_ver != 0xFD:
            logging.error(f"Invalid DoIP header: {header.hex()}")
            break
        # Read payload
        payload = conn.recv(length)
        if ptype == 0x8001:  # Diagnostic Message
            src = payload[:2]; tgt = payload[2:4]; uds = payload[4:]
            uds_resp = handle_uds(uds, state)
            # Response: swap src/tgt, wrap with header
            resp_payload = tgt + src + uds_resp
            resp_header = DOIP_HEADER.pack(0x02, 0xFD, 0x8001, len(resp_payload))
            conn.sendall(resp_header + resp_payload)
            logging.info(f"UDS req={uds.hex()} resp={uds_resp.hex()}")
    conn.close()

def main():
    logging.basicConfig(level=logging.INFO, format="[%(asctime)s] %(message)s")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", PORT))
    sock.listen(5)
    logging.info(f"DoIP simulator listening on tcp://0.0.0.0:{PORT}")
    while True:
        conn, addr = sock.accept()
        handle_client(conn, addr)  # synchronous, simple

if __name__ == "__main__":
    main()
```

### `test_sim.py` (~50 dòng)

```python
import socket, struct, subprocess, time, unittest

class TestDoipSim(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proc = subprocess.Popen(["python3", "doip_server.py"])
        time.sleep(0.5)

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate(); cls.proc.wait()

    def test_read_vin(self):
        s = socket.socket(); s.connect(("127.0.0.1", 13400))
        # ReadDID 0xF190
        uds = bytes([0x22, 0xF1, 0x90])
        payload = b"\x0E\x00" + b"\x12\x34" + uds
        header = struct.pack(">BBHI", 0x02, 0xFD, 0x8001, len(payload))
        s.sendall(header + payload)
        resp = s.recv(1024)
        self.assertIn(b"VINFAST", resp)
        s.close()
```

---

## 3. C++ DoipDiagnosticHal

### `hal/include/doip_diag_hal.h`

```cpp
#pragma once
#include "idiag_hal.h"
#include <string>

namespace vdiag {

class DoipDiagnosticHal : public IDiagnosticHal {
public:
    DoipDiagnosticHal(std::string host, uint16_t port);
    ~DoipDiagnosticHal() override;

    Result sendAndReceive(std::span<const uint8_t> req) override;
    bool isReady() const override { return sock_ >= 0; }
    void reset() override;

private:
    bool connect();
    std::string host_;
    uint16_t port_;
    int sock_ = -1;
};

} // namespace vdiag
```

### `hal/src/doip_diag_hal.cpp` (~200 dòng) — key parts

```cpp
#include "doip_diag_hal.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

namespace vdiag {

constexpr uint16_t DOIP_PORT = 13400;
constexpr uint8_t  DOIP_VER  = 0x02;
constexpr uint16_t PAYLOAD_DIAG_MSG = 0x8001;
constexpr uint16_t SRC_ADDR  = 0x0E00;
constexpr uint16_t TGT_ADDR  = 0x1234;

DoipDiagnosticHal::DoipDiagnosticHal(std::string h, uint16_t p)
    : host_(std::move(h)), port_(p) { connect(); }

DoipDiagnosticHal::~DoipDiagnosticHal() { if (sock_ >= 0) close(sock_); }

bool DoipDiagnosticHal::connect() {
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    if (::connect(sock_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock_); sock_ = -1; return false;
    }
    return true;
}

IDiagnosticHal::Result DoipDiagnosticHal::sendAndReceive(std::span<const uint8_t> uds) {
    if (sock_ < 0 && !connect()) return {false, {}, "connect failed"};

    // Build DoIP frame: header(8) + src(2) + tgt(2) + uds
    uint32_t payload_len = 4 + uds.size();
    std::vector<uint8_t> frame;
    frame.reserve(8 + payload_len);
    frame.push_back(DOIP_VER);
    frame.push_back(~DOIP_VER & 0xFF);
    frame.push_back((PAYLOAD_DIAG_MSG >> 8) & 0xFF);
    frame.push_back(PAYLOAD_DIAG_MSG & 0xFF);
    for (int i = 3; i >= 0; --i) frame.push_back((payload_len >> (i*8)) & 0xFF);
    frame.push_back((SRC_ADDR >> 8) & 0xFF); frame.push_back(SRC_ADDR & 0xFF);
    frame.push_back((TGT_ADDR >> 8) & 0xFF); frame.push_back(TGT_ADDR & 0xFF);
    frame.insert(frame.end(), uds.begin(), uds.end());

    if (send(sock_, frame.data(), frame.size(), 0) < 0)
        return {false, {}, "send failed"};

    // Recv response
    uint8_t hdr[8];
    if (recv(sock_, hdr, 8, MSG_WAITALL) < 8) return {false, {}, "recv header failed"};
    uint32_t resp_len = (hdr[4]<<24) | (hdr[5]<<16) | (hdr[6]<<8) | hdr[7];
    std::vector<uint8_t> resp_payload(resp_len);
    if (recv(sock_, resp_payload.data(), resp_len, MSG_WAITALL) < (ssize_t)resp_len)
        return {false, {}, "recv payload failed"};

    // Strip src(2)+tgt(2) → return UDS only
    return {true, {resp_payload.begin() + 4, resp_payload.end()}, ""};
}

void DoipDiagnosticHal::reset() {
    if (sock_ >= 0) { close(sock_); sock_ = -1; }
    connect();
}

} // namespace vdiag
```

---

## 4. Wire vào Android (adb reverse)

```bash
# 1. Start Python sim trên host
python3 tools/ecu_simulator/doip_server.py &

# 2. Forward port emulator → host
adb reverse tcp:13400 tcp:13400

# 3. Update jni_bridge.cpp nativeInit
# g_engine = make_unique<DiagEngine>(HalFactory::create("doip:127.0.0.1:13400"));

# 4. Install + run
./gradlew installDebug
adb shell am start -n com.vdiag/.app.DiagActivity

# 5. Tap "Read VIN" → response từ Python sim, latency ~50ms
```

---

## 5. Wireshark verification

```bash
# Capture loopback
sudo tcpdump -i lo -w doip.pcap port 13400 &
# Tap nút trên app
sudo killall tcpdump
# Open in Wireshark → Decode As → DoIP (built-in dissector từ Wireshark 3.4+)
# Screenshot → docs/doip_wireshark.png
```

---

## 6. Talking points (cho interview)

1. **"Why DoIP?"** — Modern ECUs dùng Ethernet thay CAN, ISO 13400 chuẩn hóa UDS-over-IP, port 13400. Tôi implement subset (Diagnostic Message only, không discover).
2. **"Why Python sim?"** — Demo không cần ECU thật, deterministic, CI runnable.
3. **"adb reverse vs forward?"** — `forward`: host:port → device:port. `reverse`: device:port → host:port. DoIP cần `reverse` vì app trên emulator gọi ra host.
4. **"Production differences?"** — Real DoIP có Vehicle Identification Request (UDP broadcast), Routing Activation, Alive Check. Tôi skip để focus architecture.
5. **"Throughput?"** — TCP loopback ~50ms round-trip vs Mock < 1ms. Real ECU qua Ethernet ~10-30ms typical.

---

## 7. Cut criteria (nếu W9 trễ > 3 ngày)

→ **SKIP toàn bộ Phase 9**. Vẫn giữ MockHal + ghi vào README "DoIP roadmap": *"DoIP transport designed but not yet implemented — see `docs/04_MODULE_DOIP.md` for spec."*

Interview: "Pattern open-closed cho phép swap HAL. Mock đang chạy, DoIP đã design — chỉ cần implement IDiagnosticHal là wire vào engine."
