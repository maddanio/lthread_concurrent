from io import BufferedReader
import sys
import glob
import struct
from typing import BinaryIO, Optional, Tuple, Iterable, List
import matplotlib.pyplot as plt

traces = sorted(glob.glob("*.lrt"))
print(traces)

trace_event_t = List[int]

def read_event(f : BinaryIO) -> Optional[trace_event_t]:
    lthread = struct.unpack('Q', f.read(8))[0]
    if lthread == 0:
        return None
    timestamp = struct.unpack('Q', f.read(8))[0]
    event = struct.unpack('B', f.read(1))[0]
    fd = struct.unpack('h', f.read(2))[0]
    result = [timestamp, lthread, event, fd]
    return result


def get_next_event(
    events: List[Optional[trace_event_t]],
    files: List[BufferedReader]
) -> Optional[trace_event_t]:
    result = None
    i_result: Optional[int] = None
    for i, event in enumerate(events):
        if event is not None:
            if result is None or event[0] < result[0]:
                result = event
                i_result = i
    if i_result is not None:
        events[i_result] = read_event(files[i_result])
    return result;


def read_events(traces: List[str]) -> Iterable[trace_event_t]:
    files = list(open(trace, "rb") for trace in traces)
    current_events: List[Optional[trace_event_t]] = list(read_event(f) for f in files)
    t0 : Optional[int] = None
    while True:
        result = get_next_event(current_events, files)
        if result is not None:
            if t0 is None:
                t0 = result[0]
            result[0] -= t0
            yield result
        else:
            break


class events_t:
    def __init__(self):
        self.times = list()
        self.values = list()

    def add(self, time, value):
        self.times.append(time)
        self.values.append(value)


num_lthreads = 0
num_waiting = 0
num_running = 0
num_lthreads_events = events_t()
num_waiting_events = events_t()
num_running_events = events_t()

for event in read_events(traces):
    if event[2] == 0:
        num_lthreads += 1
        num_lthreads_events.add(event[0], num_lthreads)
    elif event[2] == 1:
        num_lthreads -= 1
        num_lthreads_events.add(event[0], num_lthreads)
    elif event[2] == 4:
        num_waiting += 1
        num_waiting_events.add(event[0], num_waiting)
    elif event[2] == 5:
        num_waiting -= 1
        num_waiting_events.add(event[0], num_waiting)
    elif event[2] == 3:
        num_running += 1
        num_running_events.add(event[0], num_running)
    elif event[2] == 2:
        num_running -= 1
        num_running_events.add(event[0], num_running)
plt.figure()
plt.title("alive")
plt.plot(num_lthreads_events.times, num_lthreads_events.values)
plt.figure()
plt.title("waiting")
plt.plot(num_waiting_events.times, num_waiting_events.values)
plt.figure()
plt.title("running")
plt.plot(num_running_events.times, num_running_events.values)
plt.show()


