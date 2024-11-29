import sys
import time

import usb.core
import usb.util
import usb.control

import array

from test_bench_interfaces_pb2 import Status
from cobs import cobs

# Look for a specific device and open it
#
dev = usb.core.find(idVendor=0xcafe, idProduct=0x4010)
if dev is None:
    raise ValueError('Device not found')


def process(storage, new):
    storage += new

    while True:
        try:
            idx = storage.index(0)
            cobs_encoded = storage[0:idx]
            storage = storage[idx:]
            decoded = cobs.decode(cobs_encoded)
            msg = Status()
            msg.ParseFromString(decoded)
            print(msg)
        except e:
            print(e)
        finally:
            break

data = sys.stdin.buffer.read(4096)
storage = b""
while True:
    dev.write(0x02, data)
    rx = dev.read(0x82, size_or_buffer=4096)
    process(storage, rx)
#    sys.stdout.buffer.write(rx)
    data = sys.stdin.buffer.read(4096)

