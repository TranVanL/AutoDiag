import socket
import struct

uds = bytes([0x22, 0xF1, 0x90])

payload = b"\x0E\x00" + b"\x12\x34" + uds
hdr = struct.pack(">BBHI", 0x02, 0xFD, 0x8001, len(payload))

s = socket.socket()
s.connect(("127.0.0.1", 13400))
s.sendall(hdr + payload)

resp = s.recv(4096)

print("resp:", resp.hex(" "))
print("contains VINFAST:", b"VINFAST" in resp)

s.close()