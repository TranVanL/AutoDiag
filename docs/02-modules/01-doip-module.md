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
python_simulator/
├── DoIP_Simulator.py    ← threaded TCP listener + DoIP frame parser + UDS handler
├── testDoIP.py          ← socket client integration tests
└── requirements.txt     ← Python dependencies (if any)
```

### DoIP payload types used by VDiag

| Payload Type | Value | Direction | Meaning |
|---|---|---|---|
| Routing activation request | `0x0005` | tester → ECU | Open diagnostic routing on the gateway |
| Routing activation response | `0x0006` | ECU → tester | Accepted / denied |
| Alive check request | `0x0007` | ECU → tester | Keep-alive probe |
| Alive check response | `0x0008` | tester → ECU | Keep-alive reply |
| Diagnostic message | `0x8001` | both | UDS request/response |
| Diagnostic message positive ack | `0x8002` | ECU → tester | Frame accepted |
| Diagnostic message negative ack | `0x8003` | ECU → tester | Frame rejected |

### `DoIP_Simulator.py` — full implementation

The simulator is a multi-threaded TCP server. Each client connection runs on its own thread, so multiple testers can connect concurrently. It handles DoIP framing, routing activation, UDS request/response, and stateful DTC clearing.

```python
#!/usr/bin/env python3
import logging
import socket
import struct
import threading
from dataclasses import dataclass, field
from typing import Dict

HOST = "127.0.0.1"
PORT = 13400

DOIP_VERSION = 0x02
DOIP_INV_VERSION = 0xFD

PTYPE_ROUTING_ACTIVATION_REQ  = 0x0005
PTYPE_ROUTING_ACTIVATION_RES  = 0x0006
PTYPE_ALIVE_CHECK_REQ         = 0x0007
PTYPE_ALIVE_CHECK_RES         = 0x0008
PTYPE_DIAG_MESSAGE            = 0x8001

DOIP_HEADER = struct.Struct(">BBHI")  # ver, inv, payload_type, payload_len
MAX_PAYLOAD_LEN = 64 * 1024

SID_READ_DID         = 0x22
SID_READ_DTC         = 0x19
SID_CLEAR_DTC        = 0x14
SID_TESTER_PRESENT   = 0x3E

NRC_SERVICE_NOT_SUPPORTED = 0x11
NRC_INCORRECT_LENGTH      = 0x13
NRC_REQUEST_OUT_OF_RANGE  = 0x31


def hexs(b: bytes) -> str:
    return " ".join(f"{x:02x}" for x in b)


def recv_exact(conn: socket.socket, size: int) -> bytes:
    """TCP is a stream; read exactly `size` bytes."""
    chunks = []
    total = 0
    while total < size:
        part = conn.recv(size - total)
        if not part:
            raise ConnectionError("peer closed while receiving")
        chunks.append(part)
        total += len(part)
    return b"".join(chunks)


def nrc(original_sid: int, code: int) -> bytes:
    return bytes([0x7F, original_sid & 0xFF, code & 0xFF])


@dataclass
class EcuState:
    did_db: Dict[int, bytes] = field(default_factory=lambda: {
        0xF190: b"VINFAST12345678901",   # VIN
        0xF187: b"SW_V3.2.1_AAOS",       # Software version
        0x0105: bytes([0x4E]),           # SOC = 78%
        0x010C: bytes([0x32, 0x70]),     # RPM = 12912 (demo bytes)
    })
    dtc_list_ascii: bytes = b"P0A00, P0562"
    routing_active: bool = False

    def clear_dtc(self) -> None:
        self.dtc_list_ascii = b""


def handle_uds(req: bytes, st: EcuState) -> bytes:
    if not req:
        return nrc(0x00, NRC_INCORRECT_LENGTH)

    sid = req[0]

    if sid == SID_READ_DID:
        if len(req) < 3:
            return nrc(sid, NRC_INCORRECT_LENGTH)
        did = (req[1] << 8) | req[2]
        data = st.did_db.get(did)
        if data is None:
            return nrc(sid, NRC_REQUEST_OUT_OF_RANGE)
        return bytes([sid + 0x40, req[1], req[2]]) + data

    if sid == SID_READ_DTC:
        sub = req[1] if len(req) >= 2 else 0x02
        if st.dtc_list_ascii:
            return bytes([sid + 0x40, sub]) + st.dtc_list_ascii
        return bytes([sid + 0x40, sub])

    if sid == SID_CLEAR_DTC:
        st.clear_dtc()
        return bytes([sid + 0x40, 0xFF, 0xFF, 0xFF])

    if sid == SID_TESTER_PRESENT:
        sub = req[1] if len(req) >= 2 else 0x00
        return bytes([sid + 0x40, sub & 0x7F])

    return nrc(sid, NRC_SERVICE_NOT_SUPPORTED)


def parse_doip_header(hdr: bytes):
    if len(hdr) != 8:
        raise ValueError("header must be 8 bytes")
    ver, inv, ptype, plen = DOIP_HEADER.unpack(hdr)
    if inv != ((~ver) & 0xFF):
        raise ValueError(f"invalid inverse version: ver=0x{ver:02X}, inv=0x{inv:02X}")
    if plen > MAX_PAYLOAD_LEN:
        raise ValueError(f"payload too large: {plen}")
    return ver, inv, ptype, plen


def build_doip_frame(payload_type: int, payload: bytes) -> bytes:
    return DOIP_HEADER.pack(DOIP_VERSION, DOIP_INV_VERSION,
                            payload_type, len(payload)) + payload


def handle_client(conn: socket.socket, peer):
    logging.info("client connected: %s", peer)
    st = EcuState()

    try:
        while True:
            hdr = recv_exact(conn, 8)
            ver, inv, ptype, plen = parse_doip_header(hdr)
            payload = recv_exact(conn, plen)

            if ptype == PTYPE_ROUTING_ACTIVATION_REQ:
                # Payload: src_addr(2) + activation_type(1) + reserved(4) + optional(1)
                src_addr = struct.unpack(">H", payload[:2])[0]
                res = struct.pack(">HBBI", src_addr, 0x00, 0x10, 0x00000000)
                conn.sendall(build_doip_frame(PTYPE_ROUTING_ACTIVATION_RES, res))
                st.routing_active = True
                logging.info("routing activated for 0x%04X", src_addr)
                continue

            if ptype == PTYPE_ALIVE_CHECK_RES:
                # Tester replied to our keep-alive probe; nothing to do.
                continue

            if ptype != PTYPE_DIAG_MESSAGE:
                logging.warning("unsupported ptype=0x%04X from %s", ptype, peer)
                continue

            if not st.routing_active:
                logging.warning("diagnostic message before routing activation from %s", peer)
                continue

            if len(payload) < 4:
                logging.warning("short diag payload from %s", peer)
                continue

            src_addr, tgt_addr = struct.unpack(">HH", payload[:4])
            uds_req = payload[4:]
            uds_resp = handle_uds(uds_req, st)

            resp = build_doip_frame(
                PTYPE_DIAG_MESSAGE,
                struct.pack(">HH", tgt_addr, src_addr) + uds_resp
            )
            conn.sendall(resp)

            logging.info(
                "src=0x%04X tgt=0x%04X uds_req=[%s] uds_resp=[%s]",
                src_addr, tgt_addr, hexs(uds_req), hexs(uds_resp)
            )

    except (ConnectionError, OSError) as e:
        logging.info("client disconnected %s: %s", peer, e)
    except Exception:
        logging.exception("client error %s", peer)
    finally:
        try:
            conn.close()
        except Exception:
            pass


def serve():
    logging.basicConfig(
        level=logging.INFO,
        format="[%(asctime)s] %(levelname)s %(message)s"
    )
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(16)
    logging.info("DoIP simulator listening on tcp://%s:%d", HOST, PORT)

    while True:
        conn, peer = srv.accept()
        t = threading.Thread(target=handle_client, args=(conn, peer), daemon=True)
        t.start()


if __name__ == "__main__":
    serve()
```

### Routing activation

ISO 13400 requires a tester to activate routing before diagnostic messages are accepted by a DoIP gateway. The simulator:

1. Receives `0x0005` with source address and activation type.
2. Responds with `0x0006` and code `0x10` (routing activated).
3. Sets `routing_active = True` for that connection.
4. Drops any `0x8001` diagnostic messages received before activation.

### Alive check

A real DoIP gateway sends periodic `0x0007` alive-check requests to detect a disconnected tester. The simulator currently replies to `0x0008` responses; a production C++ `DoipDiagnosticHal` would spawn a small reader thread to answer these probes so the gateway does not tear down the TCP session.

### `testDoIP.py` — integration tests

`testDoIP.py` starts the simulator in the background and runs a socket client that:

1. Sends routing activation.
2. Reads VIN (`0x22 F1 90`).
3. Reads SOC (`0x22 01 05`).
4. Reads DTC list (`0x19 02`).
5. Sends TesterPresent (`0x3E 00`).

It asserts positive-response SIDs and expected payload content. This is the same sequence exercised by the CI `python-simulation` job.

---

## 3. C++ `DoipDiagnosticHal`

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
    bool isReady() const override { return sock_ >= 0 && routing_active_; }
    void reset() override;

private:
    bool connect();
    bool sendRoutingActivation();
    bool recvDoipFrame(uint16_t expected_ptype, std::vector<uint8_t>& out_payload);

    std::string host_;
    uint16_t port_;
    int sock_ = -1;
    bool routing_active_ = false;
    static constexpr uint16_t SRC_ADDR = 0x0E00;
    static constexpr uint16_t TGT_ADDR = 0x1234;
};

} // namespace vdiag
```

### Key behaviors

1. **Connect** opens a TCP socket to `host:port` (default 13400).
2. **Routing activation** sends `0x0005` with `SRC_ADDR` and waits for `0x0006` with code `0x10`. Diagnostic messages are not sent until this succeeds.
3. **Diagnostic message** wraps UDS bytes in `0x8001`, sends the 8-byte DoIP header + payload, then reads the response header and payload.
4. **Alive check** is not fully implemented in the reference HAL; a production version would run a small reader thread to reply to `0x0007` probes from the gateway.
5. **Reset** closes the socket, clears `routing_active_`, and reconnects.

### `sendAndReceive` outline

```cpp
IDiagnosticHal::Result DoipDiagnosticHal::sendAndReceive(std::span<const uint8_t> uds) {
    if (sock_ < 0 && !connect())
        return {false, {}, "connect failed"};

    if (!routing_active_ && !sendRoutingActivation())
        return {false, {}, "routing activation failed"};

    // Build 0x8001 diagnostic message
    std::vector<uint8_t> payload;
    payload.push_back((SRC_ADDR >> 8) & 0xFF); payload.push_back(SRC_ADDR & 0xFF);
    payload.push_back((TGT_ADDR >> 8) & 0xFF); payload.push_back(TGT_ADDR & 0xFF);
    payload.insert(payload.end(), uds.begin(), uds.end());

    auto frame = buildDoipFrame(0x8001, payload);
    if (send(sock_, frame.data(), frame.size(), 0) < 0)
        return {false, {}, "send failed"};

    std::vector<uint8_t> resp_payload;
    if (!recvDoipFrame(0x8001, resp_payload))
        return {false, {}, "recv failed"};

    // Strip src(2) + tgt(2) and return UDS bytes
    if (resp_payload.size() < 4)
        return {false, {}, "short diag payload"};

    return {true,
            {resp_payload.begin() + 4, resp_payload.end()},
            ""};
}
```

---

## 4. Wire into Android (adb reverse)

```bash
# 1. Start Python sim on host
python3 python_simulator/DoIP_Simulator.py &

# 2. Forward emulator port → host port
adb reverse tcp:13400 tcp:13400

# 3. Configure HAL spec, e.g. in nativeInit
g_engine = std::make_unique<DiagEngine>(
    HalFactory::create("doip:127.0.0.1:13400"));

# 4. Build + run
./gradlew installDebug
adb shell am start -n com.vdiag/.app.DiagActivity

# 5. Tap "Read VIN" → response from Python sim, latency ~50 ms
```

---

## 5. Wireshark verification

```bash
# Capture loopback DoIP traffic
sudo tcpdump -i lo -w doip.pcap port 13400 &
# Tap "Read VIN" in the app
sudo killall tcpdump

# Open doip.pcap in Wireshark → Decode As → DoIP (built-in dissector since 3.4)
# Screenshot → docs/DoIP/DoIP_Request.png / DoIP_Reply.png
```

---

## 6. Talking points

1. **"Why DoIP?"** — Modern ECUs use Ethernet instead of CAN. ISO 13400 standardizes UDS-over-IP on TCP port 13400.
2. **"Why Python sim?"** — Demo without real ECU hardware; deterministic; CI-runnable.
3. **"adb reverse vs forward?"** — `forward`: host → device. `reverse`: device → host. DoIP needs `reverse` because the app on the emulator calls out to the host.
4. **"Routing activation?"** — A DoIP gateway drops diagnostic traffic until the tester sends `0x0005` and receives `0x0006` with code `0x10`.
5. **"Alive check?"** — Gateways send `0x0007` probes; the tester must reply with `0x0008` or the gateway closes the TCP session.
6. **"Production differences?"** — Real DoIP also has Vehicle Identification Request (UDP broadcast), multiple logical addresses, and gateway routing tables. VDiag implements the subset needed to demonstrate the architecture.
7. **"Throughput?"** — TCP loopback ~50 ms round-trip vs Mock <1 ms. Real ECU over Ethernet ~10–30 ms typical.

---

## 7. Cut criteria

If Phase 9 slips more than three days, **skip the full DoIP integration** and keep only:

- `MockDiagnosticHal` for CI and demos.
- This spec document as the design reference.
- A note in `README.md`: *"DoIP transport is specified and simulator-ready; wire it in by implementing `DoipDiagnosticHal::sendAndReceive` and running `python_simulator/DoIP_Simulator.py`."*

Interview line: *"The HAL abstraction is Open-Closed. Mock runs today; DoIP is designed and the Python ECU simulator passes integration tests — wiring it into the engine is a matter of implementing one HAL class."*
