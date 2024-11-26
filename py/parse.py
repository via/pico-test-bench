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

read_until_zero()

while True:
    buffer = read_until_zero()
    print(buffer)
    decoded = cobs.decode(buffer)
    msg = Status()
    msg.ParseFromString(decoded)
    print(msg)

