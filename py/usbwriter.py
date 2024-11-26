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
dev = usb.core.find(idVendor=0xcafe, idProduct=0x4001)
if dev is None:
    raise ValueError('Device not found')

# Detach interfaces if Linux already attached a driver on it.
#
for itf_num in [0, 1]:
    itf = usb.util.find_descriptor(dev.get_active_configuration(),
                                   bInterfaceNumber=itf_num)
    if dev.is_kernel_driver_active(itf_num):
        dev.detach_kernel_driver(itf_num)
    usb.util.claim_interface(dev, itf)


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

        finally:
            break

data = sys.stdin.buffer.read(64)
storage = b""
while len(data) != 0:
    dev.write(0x02, data)
    #rx = dev.read(0x82, size_or_buffer=4096)
    #process(storage, rx)
    data = sys.stdin.buffer.read(64)

