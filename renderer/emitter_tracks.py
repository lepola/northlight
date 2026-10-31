"""Bounded native M2 light tracks; fire animation is evaluated by the client."""
import math
import struct


def _pulse(value):
    period, times, factors = value['period_ms'], value['times_ms'], value['factors']
    if type(period) is not int or not 80 <= period <= 10000:
        raise ValueError('Invalid emitter pulse period')
    if not 3 <= len(times) == len(factors) <= 65 or times[0] != 0 or times[-1] != period:
        raise ValueError('Invalid emitter pulse keys')
    if any(type(t) is not int for t in times) or any(a >= b for a, b in zip(times, times[1:])):
        raise ValueError('Invalid emitter pulse timestamps')
    if any(not math.isfinite(v) or not .5 <= v <= 1.5 for v in factors) or factors[0] != factors[-1]:
        raise ValueError('Invalid emitter pulse factors')
    mean = sum((b-a)*(x+y)*.5 for a,b,x,y in zip(times,times[1:],factors,factors[1:])) / period
    if not math.isclose(mean, 1., rel_tol=1e-6, abs_tol=1e-6) or max(factors)-min(factors) < .01:
        raise ValueError('Pulse must preserve mean energy and vary visibly')
    return period, list(times), list(factors)


def pulse_for(source, seed=0):
    if source.get('pulse') is not None:
        _pulse(source['pulse'])
        return source['pulse']
    if source.get('emission') not in ('fire', 'candle'):
        return None
    # Stable per-source phase/frequency; no renderer timers, allocations or RNG.
    seed = int(seed) & 0xffffffff
    period = 1240 + seed % 503
    factors = [.94, 1.08, .98, 1.04, .90, 1.02, .96, 1.06]
    shift = (seed >> 9) % len(factors)
    factors = factors[shift:] + factors[:shift]
    factors.append(factors[0])
    times = [period*i//8 for i in range(9)]
    mean = sum((b-a)*(x+y)*.5 for a,b,x,y in zip(times,times[1:],factors,factors[1:])) / period
    result = {'period_ms': period, 'times_ms': times, 'factors': [x/mean for x in factors]}
    _pulse(result)
    return result


class NativeTracks:
    def __init__(self, out):
        if len(out) < 304 or out[:4] != b'MD20' or struct.unpack_from('<I', out, 4)[0] != 264:
            raise ValueError('Requires MD20/v264')
        count, offset = struct.unpack_from('<II', out, 20)
        if count >= 32767 or count and (offset < 304 or offset+count*4 > len(out)):
            raise ValueError('Invalid original global sequences')
        self.out, self.first = out, count
        self.durations = list(struct.unpack_from('<%dI' % count, out, offset)) if count else []
        self.original = bytes(out[offset:offset+count*4]) if count else b''
        self.durations.append(1)
        self.initial = self.add(self.original+struct.pack('<I', 1))
        struct.pack_into('<II', out, 20, count+1, self.initial)

    def add(self, data):
        self.out.extend(b'\0'*(-len(self.out)%4))
        offset = len(self.out)
        self.out.extend(data)
        return offset

    def track(self, fmt, values, pulse=None):
        if pulse is None:
            sequence, interpolation, times, keys = self.first, 0, [0], [values]
        else:
            period, times, factors = _pulse(pulse)
            if fmt not in ('<f', '<3f') or any(not math.isfinite(v) for v in values):
                raise ValueError('Only finite float light tracks can pulse')
            sequence = next((i for i in range(self.first+1,len(self.durations)) if self.durations[i] == period), len(self.durations))
            if sequence == len(self.durations):
                if sequence >= 32767:
                    raise ValueError('Too many global sequences')
                self.durations.append(period)
            interpolation = 1
            keys = [tuple(v*factor for v in values) for factor in factors]
        timestamp = self.add(struct.pack('<%dI' % len(times), *times))
        key = self.add(b''.join(struct.pack(fmt, *value) for value in keys))
        times_array = self.add(struct.pack('<II', len(times), timestamp))
        keys_array = self.add(struct.pack('<II', len(keys), key))
        return struct.pack('<hh4I', interpolation, sequence, 1, times_array, 1, keys_array)

    def finish(self):
        if len(self.durations) > self.first+1:
            offset = self.add(struct.pack('<%dI' % len(self.durations), *self.durations))
            struct.pack_into('<II', self.out, 20, len(self.durations), offset)


def validate_track(blob, offset, fmt, values, pulse=None):
    """Read back the bounded native track, including every timestamp and key."""
    def read(fmt, at):
        if at < 0 or at+struct.calcsize(fmt) > len(blob):
            raise ValueError('Light track exceeds model')
        return struct.unpack_from(fmt, blob, at)
    mode, sequence, nt, ot, nk, ok = read('<hh4I', offset)
    ng, og = read('<II', 20)
    if not 0 <= sequence < ng or nt != 1 or nk != 1:
        raise ValueError('Unexpected native light track layout')
    period = read('<I', og+sequence*4)[0]
    if pulse is None:
        wanted_period, times, factors = 1, [0], [1.]
    else:
        wanted_period, times, factors = _pulse(pulse)
    if mode != int(pulse is not None) or period != wanted_period:
        raise ValueError('Native light interpolation or period mismatch')
    tc, to = read('<II', ot)
    kc, ko = read('<II', ok)
    if tc != len(times) or kc != tc or list(read('<%dI' % tc, to)) != times:
        raise ValueError('Native light timestamps mismatch')
    for i, factor in enumerate(factors):
        actual = read(fmt, ko+i*struct.calcsize(fmt))
        if len(actual) != len(values) or not all(math.isclose(a, b*factor, rel_tol=2e-6, abs_tol=1e-7) for a,b in zip(actual,values)):
            raise ValueError('Native light keys mismatch')
