import sys
from test_bench_interfaces_pb2 import Status
from cobs import cobs

last = 0

def read_until_zero():
    buffer = b""
    while True:
        n = sys.stdin.buffer.read(1)
        if n == b'\0':
            break
        buffer += n
    return buffer

while True:
    buffer = read_until_zero()
    decoded = cobs.decode(buffer)
    msg = Status()
    msg.ParseFromString(decoded)
    print(msg)

