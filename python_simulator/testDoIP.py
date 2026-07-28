#!/usr/bin/env python3
"""
DoIP Integration Test Client
- Build DoIP frame properly
- Send multiple UDS requests
- Receive and verify responses
- Full round-trip validation
"""

import socket
import struct
import sys
import time

# DoIP constants
DOIP_VERSION = 0x02
DOIP_INV_VERSION = 0xFD
PTYPE_DIAG_MESSAGE = 0x8001

# UDS Service IDs
SID_READ_DID = 0x22
SID_READ_DTC = 0x19
SID_TESTER_PRESENT = 0x3E

# Test constants
HOST = "127.0.0.1"
PORT = 13400
TESTER_ADDR = 0x0E00  # Tester source address
ECU_ADDR = 0x1234     # ECU target address

def build_doip_frame(src_addr: int, tgt_addr: int, uds_payload: bytes) -> bytes:
    """
    Build complete DoIP diagnostic frame:
    
    Header (8 bytes):
      - Version: 0x02
      - Inverse Version: 0xFD
      - Payload Type: 0x8001 (DiagMessage)
      - Payload Length: 4-byte value
    
    Payload:
      - Source Address: 2 bytes
      - Target Address: 2 bytes
      - UDS Data: variable length
    """
    payload = struct.pack(">HH", src_addr & 0xFFFF, tgt_addr & 0xFFFF) + uds_payload
    
    header = struct.pack(
        ">BBHI",
        DOIP_VERSION,
        DOIP_INV_VERSION,
        PTYPE_DIAG_MESSAGE,
        len(payload)
    )
    
    return header + payload


def recv_exact(sock: socket.socket, size: int) -> bytes:
    """
    Receive exact number of bytes from socket.
    Handle TCP stream fragmentation properly.
    """
    chunks = []
    total = 0
    while total < size:
        part = sock.recv(size - total)
        if not part:
            raise ConnectionError("Peer closed connection while receiving")
        chunks.append(part)
        total += len(part)
    return b"".join(chunks)


def parse_doip_response(data: bytes) -> tuple:
    """
    Parse DoIP response frame.
    Returns: (src_addr, tgt_addr, uds_response)
    """
    if len(data) < 8:
        raise ValueError(f"DoIP frame too short: {len(data)} bytes")
    
    # Parse header
    ver, inv, ptype, plen = struct.unpack(">BBHI", data[:8])
    
    if plen > 0x10000:
        raise ValueError(f"Payload size unrealistic: {plen}")
    
    # Extract payload
    payload = data[8:8+plen]
    if len(payload) < 4:
        raise ValueError("Payload too short for addresses")
    
    src_addr = struct.unpack(">H", payload[:2])[0]
    tgt_addr = struct.unpack(">H", payload[2:4])[0]
    uds_data = payload[4:]
    
    return src_addr, tgt_addr, uds_data


def test_read_vin():
    """Test: Read VIN (DID 0xF190) from ECU"""
    print("\n[TEST 1] Read VIN (DID 0xF190)")
    
    # Build UDS request: Service 0x22 + DID 0xF190
    uds_req = bytes([SID_READ_DID, 0xF1, 0x90])
    
    # Build DoIP frame
    doip_frame = build_doip_frame(TESTER_ADDR, ECU_ADDR, uds_req)
    
    print(f"  → Sending DoIP frame ({len(doip_frame)} bytes)")
    print(f"    UDS request: {' '.join(f'{b:02X}' for b in uds_req)}")
    
    # Connect and send
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    sock.sendall(doip_frame)
    
    # Receive response header (8 bytes)
    resp_hdr = recv_exact(sock, 8)
    _, _, _, plen = struct.unpack(">BBHI", resp_hdr)
    
    # Receive payload
    resp_payload = recv_exact(sock, plen)
    sock.close()
    
    # Parse response
    src, tgt, uds_resp = parse_doip_response(resp_hdr + resp_payload)
    
    print(f"  ← Received response ({len(uds_resp)} bytes)")
    print(f"    UDS response: {' '.join(f'{b:02X}' for b in uds_resp)}")
    
    # Verify: positive response starts with 0x62 (0x22 + 0x40)
    if uds_resp[0] != 0x62:
        print(f"  ❌ FAIL: Expected SID 0x62, got 0x{uds_resp[0]:02X}")
        return False
    
    # Extract value (skip: SID + DID_H + DID_L)
    vin_value = uds_resp[3:].decode('utf-8', errors='ignore')
    
    print(f"  VIN value: {vin_value}")
    
    # Verify contains VINFAST
    if "VINFAST" in vin_value:
        print(f"  ✅ PASS: VIN contains 'VINFAST'")
        return True
    else:
        print(f"  ❌ FAIL: VIN doesn't contain 'VINFAST'")
        return False


def test_read_soc():
    """Test: Read SOC (DID 0x0105) from ECU"""
    print("\n[TEST 2] Read SOC (DID 0x0105)")
    
    uds_req = bytes([SID_READ_DID, 0x01, 0x05])
    doip_frame = build_doip_frame(TESTER_ADDR, ECU_ADDR, uds_req)
    
    print(f"  → Sending UDS request: {' '.join(f'{b:02X}' for b in uds_req)}")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    sock.sendall(doip_frame)
    
    resp_hdr = recv_exact(sock, 8)
    _, _, _, plen = struct.unpack(">BBHI", resp_hdr)
    resp_payload = recv_exact(sock, plen)
    sock.close()
    
    _, _, uds_resp = parse_doip_response(resp_hdr + resp_payload)
    
    print(f"  ← Received: {' '.join(f'{b:02X}' for b in uds_resp)}")
    
    # Parse SOC (byte value)
    if uds_resp[0] == 0x62 and len(uds_resp) >= 4:
        soc_value = uds_resp[3]
        print(f"  SOC value: {soc_value}%")
        
        if 70 <= soc_value <= 85:  # MockHal returns 77-82
            print(f"  ✅ PASS: SOC in expected range [70, 85]")
            return True
    
    print(f"  ❌ FAIL: Invalid SOC response")
    return False


def test_read_dtc():
    """Test: Read DTC list (Service 0x19)"""
    print("\n[TEST 3] Read DTC List (Service 0x19)")
    
    uds_req = bytes([SID_READ_DTC, 0x02])  # Sub-function 0x02
    doip_frame = build_doip_frame(TESTER_ADDR, ECU_ADDR, uds_req)
    
    print(f"  → Sending UDS request: {' '.join(f'{b:02X}' for b in uds_req)}")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    sock.sendall(doip_frame)
    
    resp_hdr = recv_exact(sock, 8)
    _, _, _, plen = struct.unpack(">BBHI", resp_hdr)
    resp_payload = recv_exact(sock, plen)
    sock.close()
    
    _, _, uds_resp = parse_doip_response(resp_hdr + resp_payload)
    
    print(f"  ← Received: {' '.join(f'{b:02X}' for b in uds_resp)}")
    
    if uds_resp[0] == 0x59:  # 0x19 + 0x40
        dtc_data = uds_resp[2:].decode('utf-8', errors='ignore')
        print(f"  DTC list: {dtc_data}")
        
        if "P0A00" in dtc_data or "P0562" in dtc_data:
            print(f"  ✅ PASS: DTC list contains expected codes")
            return True
    
    print(f"  ❌ FAIL: Invalid DTC response")
    return False


def test_tester_present():
    """Test: Tester Present (Keep-alive Service 0x3E)"""
    print("\n[TEST 4] Tester Present (Service 0x3E)")
    
    uds_req = bytes([SID_TESTER_PRESENT, 0x00])
    doip_frame = build_doip_frame(TESTER_ADDR, ECU_ADDR, uds_req)
    
    print(f"  → Sending UDS request: {' '.join(f'{b:02X}' for b in uds_req)}")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    sock.sendall(doip_frame)
    
    resp_hdr = recv_exact(sock, 8)
    _, _, _, plen = struct.unpack(">BBHI", resp_hdr)
    resp_payload = recv_exact(sock, plen)
    sock.close()
    
    _, _, uds_resp = parse_doip_response(resp_hdr + resp_payload)
    
    print(f"  ← Received: {' '.join(f'{b:02X}' for b in uds_resp)}")
    
    if uds_resp[0] == 0x7E:  # 0x3E + 0x40
        print(f"  ✅ PASS: Tester Present acknowledged")
        return True
    
    print(f"  ❌ FAIL: Invalid Tester Present response")
    return False


def main():
    """Run all DoIP integration tests"""
    print("=" * 60)
    print("DoIP Integration Test Suite")
    print("=" * 60)
    
    results = []
    
    try:
        # Ping server first
        print("\n[SETUP] Connecting to DoIP server...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        sock.connect((HOST, PORT))
        sock.close()
        print(f"  ✅ Server reachable at {HOST}:{PORT}")
    except Exception as e:
        print(f"  ❌ Cannot reach server: {e}")
        print("  Make sure DoIP simulator is running:")
        print(f"    cd tools/ecu_simulator && python3 doip_server.py")
        return False
    
    # Run tests
    results.append(("Read VIN", test_read_vin()))
    time.sleep(0.1)
    
    results.append(("Read SOC", test_read_soc()))
    time.sleep(0.1)
    
    results.append(("Read DTC", test_read_dtc()))
    time.sleep(0.1)
    
    results.append(("Tester Present", test_tester_present()))
    
    # Summary
    print("\n" + "=" * 60)
    print("Test Summary:")
    print("=" * 60)
    
    passed = sum(1 for _, result in results if result)
    total = len(results)
    
    for test_name, result in results:
        status = "✅ PASS" if result else "❌ FAIL"
        print(f"{test_name:30s} {status}")
    
    print(f"\nTotal: {passed}/{total} tests passed")
    
    if passed == total:
        print("🎉 All tests PASSED!")
        return True
    else:
        print(f"⚠️ {total - passed} test(s) failed")
        return False


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)