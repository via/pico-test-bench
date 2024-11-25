import sys
import time

import usb.core
import usb.util
import usb.control

import array

# Look for a specific device and open it
#
dev = usb.core.find(idVendor=0x2e8a, idProduct=0x000a)
if dev is None:
    raise ValueError('Device not found')

# Detach interfaces if Linux already attached a driver on it.
#
for itf_num in [0, 1, 2]:
    itf = usb.util.find_descriptor(dev.get_active_configuration(),
                                   bInterfaceNumber=itf_num)
    if dev.is_kernel_driver_active(itf_num):
        dev.detach_kernel_driver(itf_num)
    usb.util.claim_interface(dev, itf)


data = sys.stdin.buffer.read(1)
while len(data) != 0:
    dev.write(0x02, data)
    print('.')
    data = sys.stdin.buffer.read(1)
    print(len(dev.read(0x81, size_or_buffer=1)))
