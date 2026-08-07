#!/usr/bin/env python3
"""Minimal SocketCAN ECU simulator for VDiag integration testing.

Listens on vcan0 for ISO-TP diagnostic requests and replies with hardcoded
UDS responses. Uses Python raw CAN sockets; ISO-TP segmentation/reassembly is
implemented manually to mirror the C++ codec.
"""

import socket
import struct
import time

VCAN0 = "vcan0"
ECU_RX_ID = 0x7DF   # OBD-II functional request address
ECU_TX_ID = 0x7E8   # ECU response address

ISO_TP_SF = 0x00
ISO_TP_FF = 0x10
ISO_TP_CF = 0x20
ISO_TP_FC = 0x30

# Hardcoded UDS responses keyed by first 3 request bytes (ReadDataByIdentifier DID)
RESPONSES = {
    bytes([0x22, 0xF1, 0x90]): bytes([0x62, 0xF1, 0x90]) + b"1VDIAG00000000001",  # VIN
    bytes([0x22, 0xF4, 0x05]): bytes([0x62, 0xF4, 0x05, 0x4E]),                    # SOC 78%
    bytes([0x22, 0x01, 0x0C]): bytes([0x62, 0x01, 0x0C, 0x0B, 0xB8]),              # RPM 3000
}


def open_can_socket(iface: str) -> socket.socket:
    sock = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((iface,))
    return sock


def parse_can_frame(data: bytes):
    can_id, dlc = struct.unpack("=IB", data[:5])[:2]
    payload = data[8:8 + dlc]
    return can_id, dlc, payload


def pack_can_frame(can_id: int, payload: bytes) -> bytes:
    dlc = len(payload)
    frame = struct.pack("=IB", can_id, dlc) + b"\x00" * 3 + payload.ljust(8, b"\x00")
    return frame


def segment_isotp(payload: bytes, tx_id: int):
    """Segment a UDS payload into ISO-TP CAN frames."""
    frames = []
    if len(payload) <= 7:
        frames.append(pack_can_frame(tx_id, bytes([ISO_TP_SF | len(payload)]) + payload))
        return frames

    # First Frame
    length = len(payload)
    ff = pack_can_frame(tx_id, bytes([ISO_TP_FF | ((length >> 8) & 0x0F), length & 0xFF]) + payload[:6])
    frames.append(ff)

    # Consecutive Frames
    offset = 6
    seq = 1
    while offset < len(payload):
        chunk = payload[offset:offset + 7]
        cf = pack_can_frame(tx_id, bytes([ISO_TP_CF | seq]) + chunk)
        frames.append(cf)
        offset += 7
        seq = (seq + 1) & 0x0F
    return frames


def reassemble_isotp(frames: list) -> bytes:
    """Reassemble ISO-TP frames into a UDS payload."""
    first = frames[0]
    first_byte = first[0]
    ptype = first_byte & 0xF0
    if ptype == ISO_TP_SF:
        length = first_byte & 0x0F
        return first[1:1 + length]
    if ptype == ISO_TP_FF:
        length = ((first_byte & 0x0F) << 8) | first[1]
        payload = bytearray(first[2:])
        expected_seq = 1
        for cf in frames[1:]:
            cf_type = cf[0] & 0xF0
            seq = cf[0] & 0x0F
            if cf_type != ISO_TP_CF or seq != expected_seq:
                raise RuntimeError("ISO-TP sequence error")
            payload.extend(cf[1:])
            expected_seq = (expected_seq + 1) & 0x0F
        return bytes(payload[:length])
    raise RuntimeError("Unexpected ISO-TP frame type")


def handle_request(payload: bytes) -> bytes:
    key = payload[:3] if len(payload) >= 3 else payload
    return RESPONSES.get(key, bytes([0x7F, 0x22, 0x11]))  # default: serviceNotSupported


def main():
    sock = open_can_socket(VCAN0)
    print(f"[ecu_can_sim] Listening on {VCAN0} rx=0x{ECU_RX_ID:03X} tx=0x{ECU_TX_ID:03X}")

    rx_buffer = []
    expecting_multiframe = False

    while True:
        data, _ = sock.recvfrom(16)
        can_id, dlc, payload = parse_can_frame(data)

        if can_id != ECU_RX_ID:
            continue

        ptype = payload[0] & 0xF0 if payload else 0

        if ptype == ISO_TP_SF:
            request = payload[1:1 + (payload[0] & 0x0F)]
            response = handle_request(request)
            for frame in segment_isotp(response, ECU_TX_ID):
                sock.send(frame)

        elif ptype == ISO_TP_FF:
            rx_buffer = [payload]
            expecting_multiframe = True
            # Send Flow Control (CTS)
            fc = pack_can_frame(ECU_TX_ID, bytes([ISO_TP_FC | 0x00, 0x00, 0x00]))
            sock.send(fc)

        elif ptype == ISO_TP_CF and expecting_multiframe:
            rx_buffer.append(payload)
            try:
                request = reassemble_isotp(rx_buffer)
                response = handle_request(request)
                for frame in segment_isotp(response, ECU_TX_ID):
                    sock.send(frame)
                rx_buffer = []
                expecting_multiframe = False
            except RuntimeError:
                pass


if __name__ == "__main__":
    main()
