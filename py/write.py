import sys
from test_bench_interfaces_pb2 import Command
from cobs import cobs

for line in sys.stdin:
    splits = line.rstrip().split()

    cmd = Command()
    cmd.delay = int(splits[1])
    if splits[0] == 'a':
        for val in splits[2:]:
            cmd.adc.values.append(float(val))
    elif splits[0] == 't':
        cmd.trigger = int(splits[2])


    serialized = cmd.SerializeToString()
    cobsd = cobs.encode(serialized) + b'\0'
    sys.stdout.buffer.write(cobsd)


