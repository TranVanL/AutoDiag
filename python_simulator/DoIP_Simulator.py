#!/usr/bin/env python3
import logging
import socket
import struct
import threading
from dataclasses import dataclass
from typing import Dict, List 

HOST = "0.0.0.0"
# DoIP is transport frame use TCP on port 13400
PORT = 13400 

# Prerequisite
# First byte : DOIP_Version
DOIP_VERSION = 0x02
# Second byte : Inverse version
DOIP_INV_VERSION = 0xFD
# Byte 3 - 4 : Diagnostic message (Payload type) 
PTYPE_DIAG_MESSAGE = 0x8001

MAX_PAYLOAD_LEN = 64 * 1024
DOIP_HEADER = struct.Struct(">BBHI")  # ver, inv, payload_type, payload_len

# UDS service list 
SID_READ_DID = 0x22 
SID_READ_DTC = 0x19
SID_CLEAR_DTC = 0x14 
SID_TESTER_PRESENT = 0x3E 

# NRC list 
NRC_SERVICE_NOT_SUPPORTED = 0x11 
NRC_INCORRECT_LENGTH = 0x13
NRC_REQUEST_OUT_OF_RANGE = 0x31 

def hexs(b: bytes) -> str:
    return " ".join("%02x" % byte for byte in b)

# Have to handle fragment problem because TCP is stream , not message queue
def recv_exact(conn: socket.socket , size: int) -> bytes:
    chunks = []
    total = 0
    while total < size : 
        part = conn.recv(size - total)
        if not part:
            raise ConnectionError("peer closed while receiving")
        chunks.append(part)
        total += len(part)
    return b"".join(chunks)

# Build NRC payload to back with ID and error code 
def nrc(original_sid: int, code: int) -> bytes:
    return bytes([0x7F, original_sid & 0xFF, code & 0xFF])

@dataclass
class EcuState:
    # 6 "properties" mirror MockHal intent:
    # VIN, SW_VER, SOC, RPM via DID
    # DTC_LIST via 0x19
    # DTC_CLEAR via 0x14
    did_db: Dict[int, bytes]
    dtc_list_ascii: bytes

    # Data base for resposne coressponding 
    @staticmethod
    def create_default() -> "EcuState":
        return EcuState(
            did_db={
                0xF190: b"VINFAST12345678901",   # VIN
                0xF187: b"SW_V3.2.1_AAOS",       # SoftwareVer
                0x0105: bytes([0x4E]),           # SOC = 78
                0x010C: bytes([0x32, 0x70]),     # RPM bytes (mirror current MockHal)
            },
            dtc_list_ascii=b"P0A00, P0562",
        )

    def clear_dtc(self) -> None:
        self.dtc_list_ascii = b""


# Handle UDS after parse header DoIP
def handle_uds(req: bytes, st: EcuState) -> bytes:
    if not req:
        return nrc(0x00, NRC_INCORRECT_LENGTH)
    # Extract the first byte (Service ID )
    sid = req[0]

    if sid == SID_READ_DID:
        # Read DID require extra 2 byte for Data ID
        if len(req) < 3:
            return nrc(sid, NRC_INCORRECT_LENGTH)

        did = (req[1] << 8) | req[2]
        data = st.did_db.get(did)
        if data is None:
            return nrc(sid, NRC_REQUEST_OUT_OF_RANGE)

        # positive response: 0x62 + DID + data
        return bytes([sid + 0x40, req[1], req[2]]) + data

    if sid == SID_READ_DTC:
        # Read DTC usually attach the sub-function -
        # keep simple and deterministic -> Get sub if len >= 2 and other cases , use default 
        sub = req[1] if len(req) >= 2 else 0x02


        # positive response: 0x59 + sub + payload
        # mock style payload as readable ascii for UI demo
        if st.dtc_list_ascii:
            return bytes([sid + 0x40, sub]) + st.dtc_list_ascii
        return bytes([sid + 0x40, sub])

    if sid == SID_CLEAR_DTC:
        st.clear_dtc()
        # mirror MockHal positive clear shape
        return bytes([sid + 0x40, 0xFF, 0xFF, 0xFF])

    if sid == SID_TESTER_PRESENT:
        # mirror MockHal positive response
        sub = req[1] if len(req) >= 2 else 0x00
        return bytes([sid + 0x40, sub & 0x7F])

    # Other cases fail
    return nrc(sid, NRC_SERVICE_NOT_SUPPORTED)

# Parse DoIP header , it is wrapper for the UDS content 
def parse_doip_header(hdr: bytes):

    # DoIP header 8 bytes : Ver | Inv | Payload Type (2 byte) | Payload Length (4 byte)
    if len(hdr) != 8:
        raise ValueError("header must be 8 bytes")
    ver, inv, ptype, plen = DOIP_HEADER.unpack(hdr)

    if inv != ((~ver) & 0xFF):
        raise ValueError(f"invalid inverse version: ver=0x{ver:02X}, inv=0x{inv:02X}")

    if plen > MAX_PAYLOAD_LEN:
        raise ValueError(f"payload too large: {plen}")

    # Return necessary value
    return ver, inv, ptype, plen

# Build DoIP frame : DoIP header + src_addr + target_addr + uds_payload
def build_doip_diag_frame(src_addr: int, tgt_addr: int, uds_payload: bytes) -> bytes:
    payload = struct.pack(">HH", src_addr & 0xFFFF, tgt_addr & 0xFFFF) + uds_payload
    hdr = DOIP_HEADER.pack(
        DOIP_VERSION,
        DOIP_INV_VERSION,
        PTYPE_DIAG_MESSAGE,
        len(payload),
    )
    return hdr + payload

def handle_client(conn: socket.socket, peer):
    logging.info("client connected: %s", peer)
    st = EcuState.create_default()

    try:
        while True:
            # Split DoIP header and Payload
            hdr = recv_exact(conn, 8)
            ver, inv, ptype, plen = parse_doip_header(hdr)
            payload = recv_exact(conn, plen)

            if ptype != PTYPE_DIAG_MESSAGE:
                logging.warning(
                    "unsupported ptype=0x%04X from %s, dropping",
                    ptype, peer
                )
                continue

            if len(payload) < 4:
                logging.warning("short diag payload from %s", peer)
                continue
            # 2 bytes for src_addr and 2 bytes for target_addr
            src_addr, tgt_addr = struct.unpack(">HH", payload[:4])
            uds_req = payload[4:]
            uds_resp = handle_uds(uds_req, st)

            # response swaps addresses: ECU -> tester
            resp = build_doip_diag_frame(tgt_addr, src_addr, uds_resp)
            conn.sendall(resp)

            logging.info(
                "ver=0x%02X inv=0x%02X src=0x%04X tgt=0x%04X uds_req=[%s] uds_resp=[%s]",
                ver, inv, src_addr, tgt_addr, hexs(uds_req), hexs(uds_resp)
            )
    except (ConnectionError, OSError) as e:
        logging.info("client disconnected %s: %s", peer, e)
    except Exception as e:
        logging.exception("client error %s: %s", peer, e)
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