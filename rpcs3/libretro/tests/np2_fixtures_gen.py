#!/usr/bin/env python3
"""Fixtures for test_np2: RPCN's messages as the protobuf library reads and
writes them, which the core's own reader and writer
(rpcs3/Emu/NP/generated/np2_structs_gen.py) must match.

For each message type in np2_structs.proto: random messages written by
protobuf, then the same read back in ways protobuf accepts but does not
write (fields out of order, numbers not packed, unknown fields and groups,
fields given twice, known fields of the wrong wire type), and broken ones
(cut short, bytes changed, strings that are not UTF-8).

Each record: the message type, the bytes, whether protobuf reads them, and
if it does, a hash of the canonical text of what it read and a hash of
what it writes again (unknown fields left out).

When made, they were read with RPCS3's own protobuf (the C++ library at
the commit its submodule pinned, edaa823d, with the code it generated):
it read and refused the same records, and wrote the same bytes.

Usage: np2_fixtures_gen.py PROTO OUT.bin.gz [seed]
Needs the protobuf package and grpcio-tools (for protoc).
"""

import gzip
import importlib
import os
import random
import struct
import subprocess
import sys
import tempfile

from google.protobuf.descriptor import FieldDescriptor as FD

PER_TYPE = 16
VARIANTS = 5
BROKEN = 5


def fnv(b):
    h = 1469598103934665603
    for c in b:
        h ^= c
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def load(proto):
    out = tempfile.mkdtemp()
    d, f = os.path.split(os.path.abspath(proto))
    subprocess.check_call([sys.executable, "-m", "grpc_tools.protoc", "-I" + d, "--python_out=" + out, f])
    sys.path.insert(0, out)
    return importlib.import_module(f[:-6] + "_pb2")


# ---- canonical text, as np2_structs_gen.py --dump writes it ----

def text(m):
    o = ["{"]
    for f in sorted(m.DESCRIPTOR.fields, key=lambda f: f.number):
        o.append("%d=" % f.number)
        v = getattr(m, f.name)
        if f.is_repeated:
            o.append("[")
            for e in v:
                o.append(value(f, e))
                o.append(",")
            o.append("]")
        elif f.type == FD.TYPE_MESSAGE:
            o.append(text(v) if m.HasField(f.name) else "-")
        else:
            o.append(value(f, v))
        o.append(";")
    o.append("}")
    return "".join(o)


def value(f, v):
    if f.type == FD.TYPE_MESSAGE:
        return text(v)
    if f.type == FD.TYPE_STRING:
        return "x" + v.encode("utf-8").hex()
    if f.type == FD.TYPE_BYTES:
        return "x" + v.hex()
    if f.type == FD.TYPE_BOOL:
        return "1" if v else "0"
    return str(int(v))


# ---- random messages ----

STR_PARTS = ["", "a", "npid_01", "Ünïcödé", "日本語の部屋", "🎮", "ελληνικά", "\u0000", "x" * 40, "tab\tnl\n"]


def rand_scalar(r, f):
    t = f.type
    if t == FD.TYPE_BOOL:
        return r.random() < 0.5
    if t == FD.TYPE_UINT32:
        return r.choice([0, 1, 127, 128, 255, 300, 16383, 16384, 65535, 2**31, 2**32 - 1, r.getrandbits(32)])
    if t == FD.TYPE_UINT64:
        return r.choice([0, 1, 128, 2**32, 2**63, 2**64 - 1, r.getrandbits(64), r.getrandbits(20)])
    if t == FD.TYPE_INT32:
        return r.choice([0, 1, -1, 127, -128, 2**31 - 1, -2**31, r.randint(-2**31, 2**31 - 1)])
    if t == FD.TYPE_INT64:
        return r.choice([0, 1, -1, 2**63 - 1, -2**63, r.randint(-2**63, 2**63 - 1), r.randint(-1000, 1000)])
    if t == FD.TYPE_STRING:
        return "".join(r.choice(STR_PARTS) for _ in range(r.randint(0, 3)))
    if t == FD.TYPE_BYTES:
        return bytes(r.getrandbits(8) for _ in range(r.choice([0, 1, 2, 8, 16, 33, r.randint(0, 64)])))
    raise ValueError(t)


def fill(r, m, depth=0):
    for f in m.DESCRIPTOR.fields:
        if r.random() < 0.25:
            continue
        if f.is_repeated:
            n = r.randint(0, 3 if depth < 3 else 1)
            for _ in range(n):
                if f.type == FD.TYPE_MESSAGE:
                    fill(r, getattr(m, f.name).add(), depth + 1)
                else:
                    getattr(m, f.name).append(rand_scalar(r, f))
        elif f.type == FD.TYPE_MESSAGE:
            sub = getattr(m, f.name)
            sub.SetInParent()
            if r.random() < 0.85:
                fill(r, sub, depth + 1)
        else:
            setattr(m, f.name, rand_scalar(r, f))


# ---- the wire, for variants ----

def varint(v):
    v &= 0xFFFFFFFFFFFFFFFF
    o = bytearray()
    while v >= 0x80:
        o.append((v & 0x7F) | 0x80)
        v >>= 7
    o.append(v)
    return bytes(o)


def read_varint(b, i):
    v, s = 0, 0
    while True:
        c = b[i]
        i += 1
        v |= (c & 0x7F) << s
        s += 7
        if not c & 0x80:
            return v, i


def fields_of(b):
    """(number, wire type, payload as written after the tag)"""
    out, i = [], 0
    while i < len(b):
        t, j = read_varint(b, i)
        num, wt = t >> 3, t & 7
        if wt == 0:
            _, k = read_varint(b, j)
        elif wt == 1:
            k = j + 8
        elif wt == 5:
            k = j + 4
        elif wt == 2:
            n, k = read_varint(b, j)
            k += n
        else:
            raise ValueError(wt)
        out.append((num, wt, b[j:k]))
        i = k
    return out


def tag(num, wt):
    return varint((num << 3) | wt)


def unknowns(r, known):
    num = r.choice([n for n in (15, 31, 99, 1000, 20000, 536870900) if n not in known])
    wt = r.choice([0, 1, 2, 3, 5])
    if wt == 0:
        return tag(num, 0) + varint(r.getrandbits(64))
    if wt == 1:
        return tag(num, 1) + bytes(r.getrandbits(8) for _ in range(8))
    if wt == 5:
        return tag(num, 5) + bytes(r.getrandbits(8) for _ in range(4))
    if wt == 2:
        p = bytes(r.getrandbits(8) for _ in range(r.randint(0, 12)))
        return tag(num, 2) + varint(len(p)) + p
    inner = tag(num + 1, 0) + varint(5) + tag(num + 2, 3) + tag(num + 2, 4)
    return tag(num, 3) + inner + tag(num, 4)


def variant(r, desc, b):
    fs = fields_of(b)
    by_num = {f.number: f for f in desc.fields}
    out = []
    for num, wt, payload in fs:
        f = by_num.get(num)
        packed = f is not None and f.is_repeated and wt == 2 and f.type not in (FD.TYPE_MESSAGE, FD.TYPE_STRING, FD.TYPE_BYTES)
        if packed and r.random() < 0.6:
            # numbers one by one instead of packed
            n, j = read_varint(payload, 0)
            body = payload[j:]
            k = 0
            while k < len(body):
                v, k2 = read_varint(body, k)
                out.append(tag(num, 0) + body[k:k2])
                k = k2
            continue
        out.append(tag(num, wt) + payload)
        if r.random() < 0.15:
            out.append(tag(num, wt) + payload)  # given twice
    if out and r.random() < 0.7:
        r.shuffle(out)
    for _ in range(r.randint(0, 3)):
        out.insert(r.randint(0, len(out)), unknowns(r, by_num))
    if r.random() < 0.3 and desc.fields:
        # a known field number with another wire type: skipped as unknown
        f = r.choice(desc.fields)
        wrong = 0 if f.type in (FD.TYPE_MESSAGE, FD.TYPE_STRING, FD.TYPE_BYTES) else 5
        out.insert(r.randint(0, len(out)), tag(f.number, wrong) + (varint(3) if wrong == 0 else b"\1\2\3\4"))
    return b"".join(out)


def bad_utf8(r, m):
    """The message with a string somewhere in it made not UTF-8, or None"""
    strings = []

    def walk(m):
        for f in m.DESCRIPTOR.fields:
            v = getattr(m, f.name)
            if f.type == FD.TYPE_STRING and not f.is_repeated and v:
                strings.append((m, f))
            elif f.type == FD.TYPE_MESSAGE:
                if f.is_repeated:
                    for e in v:
                        walk(e)
                elif m.HasField(f.name):
                    walk(v)

    walk(m)
    if not strings:
        return None
    holder, f = r.choice(strings)
    old = getattr(holder, f.name)
    marker = "☃" * 3
    setattr(holder, f.name, marker)
    b = m.SerializeToString()
    setattr(holder, f.name, old)
    bad = r.choice([b"\xff\xfe\xfd", b"\xc0\x80\x80", b"\xed\xa0\x80", b"\xf4\x90\x80"])
    return b.replace(marker.encode("utf-8"), bad + b"\x80" * (9 - len(bad)), 1)


def read(cls, b):
    m = cls()
    try:
        m.ParseFromString(b)
    except Exception:
        return None
    return m


def record(out, name, kind, b, m):
    nb = name.encode()
    out.append(struct.pack("<BB", kind, len(nb)) + nb + struct.pack("<I", len(b)) + b)
    if m is None:
        out.append(b"\0")
    else:
        m.DiscardUnknownFields()
        out.append(b"\1" + struct.pack("<QQ", fnv(text(m).encode()), fnv(m.SerializeToString())))


def main():
    pb2 = load(sys.argv[1])
    r = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 357)
    out = []
    counts = [0, 0, 0]
    for name, desc in pb2.DESCRIPTOR.message_types_by_name.items():
        cls = getattr(pb2, name)
        for i in range(PER_TYPE):
            m = cls()
            if i:  # the first is the empty message
                fill(r, m)
            b = m.SerializeToString()
            record(out, name, 0, b, read(cls, b))
            counts[0] += 1
            for _ in range(VARIANTS):
                v = variant(r, desc, b)
                record(out, name, 1, v, read(cls, v))
                counts[1] += 1
            for _ in range(BROKEN):
                c = r.random()
                if c < 0.3 and b:
                    v = b[:r.randrange(len(b))]
                elif c < 0.6 and b:
                    v = bytearray(b)
                    v[r.randrange(len(v))] = r.getrandbits(8)
                    v = bytes(v)
                elif c < 0.8:
                    v = bad_utf8(r, m) or (b + b"\x80")
                elif c < 0.9:
                    v = b + r.choice([b"\x00", b"\x0c", b"\xff\xff", b"\x0a\x05ab", b"\x08\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"])
                else:
                    # a group closed with another number, or not closed
                    v = b + r.choice([tag(77, 3) + tag(78, 4), tag(77, 3) + varint(5 << 3) + b"\x01", tag(77, 3) + tag(79, 3) + tag(79, 4) + tag(77, 4)])
                record(out, name, 2, v, read(cls, v))
                counts[2] += 1
    with gzip.GzipFile(sys.argv[2], "wb", mtime=0) as f:
        f.write(b"".join(out))
    print("np2_fixtures_gen: %d messages, %d variants, %d broken" % tuple(counts))


if __name__ == "__main__":
    main()
