#!/usr/bin/env python3
"""RPCN's messages, from np2_structs.proto, as plain C++ (np2_structs.h and
np2_structs.cpp, next to this script), read and written by Emu/NP/pb_wire.h
instead of the protobuf library.

The classes keep the names protobuf's generated code gave them, so the code
that fills and reads them is RPCS3's as before, but strings and bytes read
from a packet are views into it (std::string_view), repeated fields are
std::vector, and a message is written straight into the caller's buffer.

When RPCS3 changes np2_structs.proto, run this again:
    python3 rpcs3/Emu/NP/generated/np2_structs_gen.py

--out DIR writes them there instead (the CI test checks the ones here are
what the .proto makes); --dump FILE also writes, for the test, a canonical
text form of every message (rpcs3/libretro/tests/np2_fixtures_gen.py makes
the same from the protobuf library's reading).
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

SCALARS = {
    # proto type: (C++ type, to varint, from varint)
    "uint32": ("std::uint32_t", "{}", "static_cast<std::uint32_t>({})"),
    "uint64": ("std::uint64_t", "{}", "{}"),
    "int32": ("std::int32_t", "pb::from_int32({})", "static_cast<std::int32_t>(static_cast<std::uint32_t>({}))"),
    "int64": ("std::int64_t", "pb::from_int64({})", "static_cast<std::int64_t>({})"),
    "bool": ("bool", "({} ? 1u : 0u)", "({} != 0)"),
}
TEXTS = ("string", "bytes")


class Field:
    def __init__(self, repeated, ptype, name, number):
        self.repeated = repeated
        self.ptype = ptype
        self.name = name
        self.acc = name.lower()
        self.number = number
        self.kind = "scalar" if ptype in SCALARS else "text" if ptype in TEXTS else "message"


def parse(path):
    text = open(path, encoding="utf-8").read()
    text = re.sub(r"//[^\n]*", "", text)
    messages = []
    for m in re.finditer(r"message\s+(\w+)\s*\{([^}]*)\}", text):
        fields = []
        for f in re.finditer(r"(repeated\s+)?(\w+)\s+(\w+)\s*=\s*(\d+)\s*;", m.group(2)):
            fields.append(Field(bool(f.group(1)), f.group(2), f.group(3), int(f.group(4))))
        rest = re.sub(r"(repeated\s+)?(\w+)\s+(\w+)\s*=\s*(\d+)\s*;", "", m.group(2)).strip()
        if rest:
            sys.exit("np2_structs_gen.py: %s: cannot read '%s'" % (m.group(1), rest))
        fields.sort(key=lambda f: f.number)
        messages.append((m.group(1), fields))
    for kw in ("oneof", "enum", "map<", "optional ", "import"):
        if kw in text:
            sys.exit("np2_structs_gen.py: the .proto uses '%s', which this does not write" % kw)
    names = {n for n, _ in messages}
    for n, fields in messages:
        for f in fields:
            if f.kind == "message" and f.ptype not in names:
                sys.exit("np2_structs_gen.py: %s.%s: unknown type %s" % (n, f.name, f.ptype))
    return messages


def ordered(messages):
    """Each message after the ones it holds"""
    by_name = dict(messages)
    done, out, busy = set(), [], set()

    def visit(n):
        if n in done:
            return
        if n in busy:
            sys.exit("np2_structs_gen.py: %s holds itself" % n)
        busy.add(n)
        for f in by_name[n]:
            if f.kind == "message":
                visit(f.ptype)
        busy.discard(n)
        done.add(n)
        out.append((n, by_name[n]))

    for n, _ in messages:
        visit(n)
    return out


def ctype(f):
    if f.kind == "scalar":
        return SCALARS[f.ptype][0]
    if f.kind == "text":
        return "pb::text"
    return f.ptype


def header(messages):
    o = []
    w = o.append
    w("#pragma once")
    w("")
    w("// RPCN's messages, made by np2_structs_gen.py from np2_structs.proto: do not")
    w("// edit. Read and written by Emu/NP/pb_wire.h. A message read with")
    w("// ParseFromArray borrows its strings and bytes from the buffer it was read")
    w("// from, which must outlive it; one filled by its setters owns them.")
    w("")
    w('#include "Emu/NP/pb_wire.h"')
    w("")
    w("#include <cstddef>")
    w("#include <cstdint>")
    w("#include <string>")
    w("#include <string_view>")
    w("#include <vector>")
    w("")
    w("namespace np2_structs")
    w("{")
    first = True
    for name, fields in messages:
        if not first:
            w("")
        first = False
        w("\tclass %s final" % name)
        w("\t{")
        w("\tpublic:")
        for f in fields:
            t = ctype(f)
            a = f.acc
            if f.repeated:
                w("\t\tconst std::vector<%s>& %s() const { return m_%s; }" % (t, a, a))
                w("\t\tstd::vector<%s>* mutable_%s() { return &m_%s; }" % (t, a, a))
                w("\t\tint %s_size() const { return static_cast<int>(m_%s.size()); }" % (a, a))
                if f.kind == "scalar":
                    w("\t\t%s %s(int i) const { return m_%s[static_cast<std::size_t>(i)]; }" % (t, a, a))
                    w("\t\tvoid add_%s(%s v) { m_%s.push_back(v); }" % (a, t, a))
                elif f.kind == "text":
                    w("\t\tstd::string_view %s(int i) const { return m_%s[static_cast<std::size_t>(i)].get(); }" % (a, a))
                    w("\t\tvoid add_%s(std::string_view v) { m_%s.emplace_back().set(v); }" % (a, a))
                    w("\t\tvoid add_%s(std::string&& v) { m_%s.emplace_back().set(std::move(v)); }" % (a, a))
                    w("\t\tvoid add_%s(const char* v) { m_%s.emplace_back().set(v); }" % (a, a))
                    w("\t\tvoid add_%s(const void* p, std::size_t n) { m_%s.emplace_back().set(p, n); }" % (a, a))
                else:
                    w("\t\tconst %s& %s(int i) const { return m_%s[static_cast<std::size_t>(i)]; }" % (t, a, a))
                    w("\t\t%s* mutable_%s(int i) { return &m_%s[static_cast<std::size_t>(i)]; }" % (t, a, a))
                    w("\t\t%s* add_%s() { return &m_%s.emplace_back(); }" % (t, a, a))
            elif f.kind == "scalar":
                w("\t\t%s %s() const { return m_%s; }" % (t, a, a))
                w("\t\tvoid set_%s(%s v) { m_%s = v; }" % (a, t, a))
            elif f.kind == "text":
                w("\t\tstd::string_view %s() const { return m_%s.get(); }" % (a, a))
                w("\t\tvoid set_%s(std::string_view v) { m_%s.set(v); }" % (a, a))
                w("\t\tvoid set_%s(std::string&& v) { m_%s.set(std::move(v)); }" % (a, a))
                w("\t\tvoid set_%s(const char* v) { m_%s.set(v); }" % (a, a))
                w("\t\tvoid set_%s(const void* p, std::size_t n) { m_%s.set(p, n); }" % (a, a))
            else:
                w("\t\tbool has_%s() const { return m_has_%s; }" % (a, a))
                w("\t\tconst %s& %s() const { return m_%s; }" % (t, a, a))
                w("\t\t%s* mutable_%s() { m_has_%s = true; return &m_%s; }" % (t, a, a, a))
        if fields:
            w("")
        w("\t\t// Reads a message; false when it is not one. Strings and bytes point")
        w("\t\t// into @data.")
        w("\t\tbool ParseFromArray(const void* data, std::size_t size);")
        w("\t\t// The size the message is written in, kept for writing it")
        w("\t\tstd::size_t ByteSizeLong() const;")
        w("\t\t// Writes it into @out, ByteSizeLong() bytes, after ByteSizeLong()")
        w("\t\tvoid SerializeWithCachedSizesToArray(void* out) const;")
        w("\t\tbool SerializeToString(std::string* out) const;")
        w("")
        w("\t\tbool merge(pb::reader& r);")
        w("\t\tvoid write(pb::writer& w) const;")
        w("\t\tstd::size_t cached_size() const { return m_cached_size; }")
        w("")
        w("\tprivate:")
        for f in fields:
            t = ctype(f)
            a = f.acc
            if f.repeated:
                w("\t\tstd::vector<%s> m_%s;" % (t, a))
                if f.kind == "scalar":
                    w("\t\tmutable std::size_t m_%s_packed = 0;" % a)
            elif f.kind == "scalar":
                w("\t\t%s m_%s = {};" % (t, a))
            elif f.kind == "text":
                w("\t\tpb::text m_%s;" % a)
            else:
                w("\t\t%s m_%s;" % (t, a))
                w("\t\tbool m_has_%s = false;" % a)
        w("\t\tmutable std::size_t m_cached_size = 0;")
        w("\t};")
    w("} // namespace np2_structs")
    w("")
    return "\n".join(o)


def source(messages):
    o = []
    w = o.append
    w("// RPCN's messages, made by np2_structs_gen.py from np2_structs.proto: do not")
    w("// edit.")
    w("")
    w('#include "np2_structs.h"')
    w("")
    w("namespace np2_structs")
    w("{")
    first = True
    for name, fields in messages:
        if not first:
            w("")
        first = False
        # size
        w("\tstd::size_t %s::ByteSizeLong() const" % name)
        w("\t{")
        w("\t\tstd::size_t n = 0;")
        for f in fields:
            a, num = f.acc, f.number
            if f.repeated:
                if f.kind == "scalar":
                    conv = SCALARS[f.ptype][1].format("v")
                    w("\t\tif (!m_%s.empty())" % a)
                    w("\t\t{")
                    w("\t\t\tstd::size_t p = 0;")
                    w("\t\t\tfor (const auto v : m_%s)" % a)
                    w("\t\t\t\tp += pb::varint_size(%s);" % conv)
                    w("\t\t\tm_%s_packed = p;" % a)
                    w("\t\t\tn += pb::len_size(%d, p);" % num)
                    w("\t\t}")
                elif f.kind == "text":
                    w("\t\tfor (const auto& v : m_%s)" % a)
                    w("\t\t\tn += pb::len_size(%d, v.size());" % num)
                else:
                    w("\t\tfor (const auto& v : m_%s)" % a)
                    w("\t\t\tn += pb::len_size(%d, v.ByteSizeLong());" % num)
            elif f.kind == "scalar":
                conv = SCALARS[f.ptype][1].format("m_" + a)
                w("\t\tif (m_%s)" % a)
                w("\t\t\tn += pb::tag_size(%d) + pb::varint_size(%s);" % (num, conv))
            elif f.kind == "text":
                w("\t\tif (!m_%s.empty())" % a)
                w("\t\t\tn += pb::len_size(%d, m_%s.size());" % (num, a))
            else:
                w("\t\tif (m_has_%s)" % a)
                w("\t\t\tn += pb::len_size(%d, m_%s.ByteSizeLong());" % (num, a))
        w("\t\tm_cached_size = n;")
        w("\t\treturn n;")
        w("\t}")
        w("")
        # write
        w("\tvoid %s::write(pb::writer& %s) const" % (name, "w" if fields else "/*w*/"))
        w("\t{")
        for f in fields:
            a, num = f.acc, f.number
            if f.repeated:
                if f.kind == "scalar":
                    conv = SCALARS[f.ptype][1].format("v")
                    w("\t\tif (!m_%s.empty())" % a)
                    w("\t\t{")
                    w("\t\t\tw.tag(%d, pb::wire_len);" % num)
                    w("\t\t\tw.varint(m_%s_packed);" % a)
                    w("\t\t\tfor (const auto v : m_%s)" % a)
                    w("\t\t\t\tw.varint(%s);" % conv)
                    w("\t\t}")
                elif f.kind == "text":
                    w("\t\tfor (const auto& v : m_%s)" % a)
                    w("\t\t\tw.bytes(%d, v.get());" % num)
                else:
                    w("\t\tfor (const auto& v : m_%s)" % a)
                    w("\t\t{")
                    w("\t\t\tw.tag(%d, pb::wire_len);" % num)
                    w("\t\t\tw.varint(v.cached_size());")
                    w("\t\t\tv.write(w);")
                    w("\t\t}")
            elif f.kind == "scalar":
                conv = SCALARS[f.ptype][1].format("m_" + a)
                w("\t\tif (m_%s)" % a)
                w("\t\t{")
                w("\t\t\tw.tag(%d, pb::wire_varint);" % num)
                w("\t\t\tw.varint(%s);" % conv)
                w("\t\t}")
            elif f.kind == "text":
                w("\t\tif (!m_%s.empty())" % a)
                w("\t\t\tw.bytes(%d, m_%s.get());" % (num, a))
            else:
                w("\t\tif (m_has_%s)" % a)
                w("\t\t{")
                w("\t\t\tw.tag(%d, pb::wire_len);" % num)
                w("\t\t\tw.varint(m_%s.cached_size());" % a)
                w("\t\t\tm_%s.write(w);" % a)
                w("\t\t}")
        w("\t}")
        w("")
        # merge
        w("\tbool %s::merge(pb::reader& r)" % name)
        w("\t{")
        w("\t\tstd::uint32_t f, wt;")
        w("\t\twhile (!r.done())")
        w("\t\t{")
        w("\t\t\tif (!r.tag(f, wt))")
        w("\t\t\t\treturn false;")
        if fields:
            w("\t\t\tswitch (f)")
            w("\t\t\t{")
            for f in fields:
                a, num = f.acc, f.number
                w("\t\t\tcase %d:" % num)
                if f.kind == "scalar":
                    conv = SCALARS[f.ptype][2].format("v")
                    target = ("m_%s.push_back(%s)" if f.repeated else "m_%s = %s") % (a, conv)
                    w("\t\t\t\tif (wt == pb::wire_varint)")
                    w("\t\t\t\t{")
                    w("\t\t\t\t\tstd::uint64_t v;")
                    w("\t\t\t\t\tif (!r.varint(v))")
                    w("\t\t\t\t\t\treturn false;")
                    w("\t\t\t\t\t%s;" % target)
                    w("\t\t\t\t\tcontinue;")
                    w("\t\t\t\t}")
                    if f.repeated:
                        w("\t\t\t\tif (wt == pb::wire_len)")
                        w("\t\t\t\t{")
                        w("\t\t\t\t\tstd::string_view s;")
                        w("\t\t\t\t\tif (!r.len(s))")
                        w("\t\t\t\t\t\treturn false;")
                        w("\t\t\t\t\tpb::reader p(s.data(), s.size());")
                        w("\t\t\t\t\twhile (!p.done())")
                        w("\t\t\t\t\t{")
                        w("\t\t\t\t\t\tstd::uint64_t v;")
                        w("\t\t\t\t\t\tif (!p.varint(v))")
                        w("\t\t\t\t\t\t\treturn false;")
                        w("\t\t\t\t\t\t%s;" % target)
                        w("\t\t\t\t\t}")
                        w("\t\t\t\t\tcontinue;")
                        w("\t\t\t\t}")
                elif f.kind == "text":
                    w("\t\t\t\tif (wt == pb::wire_len)")
                    w("\t\t\t\t{")
                    w("\t\t\t\t\tstd::string_view s;")
                    w("\t\t\t\t\tif (!r.len(s)%s)" % (" || !pb::utf8_valid(s)" if f.ptype == "string" else ""))
                    w("\t\t\t\t\t\treturn false;")
                    w("\t\t\t\t\t%s.lend(s);" % (("m_%s.emplace_back()" if f.repeated else "m_%s") % a))
                    w("\t\t\t\t\tcontinue;")
                    w("\t\t\t\t}")
                else:
                    w("\t\t\t\tif (wt == pb::wire_len)")
                    w("\t\t\t\t{")
                    w("\t\t\t\t\tstd::string_view s;")
                    w("\t\t\t\t\tif (!r.len(s))")
                    w("\t\t\t\t\t\treturn false;")
                    w("\t\t\t\t\tpb::reader sub(s.data(), s.size());")
                    if f.repeated:
                        w("\t\t\t\t\tif (!m_%s.emplace_back().merge(sub))" % a)
                    else:
                        w("\t\t\t\t\tm_has_%s = true;" % a)
                        w("\t\t\t\t\tif (!m_%s.merge(sub))" % a)
                    w("\t\t\t\t\t\treturn false;")
                    w("\t\t\t\t\tcontinue;")
                    w("\t\t\t\t}")
                w("\t\t\t\tbreak;")
            w("\t\t\tdefault:")
            w("\t\t\t\tbreak;")
            w("\t\t\t}")
        w("\t\t\tif (!r.skip(f, wt))")
        w("\t\t\t\treturn false;")
        w("\t\t}")
        w("\t\treturn true;")
        w("\t}")
        w("")
        w("\tbool %s::ParseFromArray(const void* data, std::size_t size)" % name)
        w("\t{")
        w("\t\t*this = {};")
        w("\t\tpb::reader r(data, size);")
        w("\t\treturn merge(r);")
        w("\t}")
        w("")
        w("\tvoid %s::SerializeWithCachedSizesToArray(void* out) const" % name)
        w("\t{")
        w("\t\tpb::writer w(out);")
        w("\t\twrite(w);")
        w("\t}")
        w("")
        w("\tbool %s::SerializeToString(std::string* out) const" % name)
        w("\t{")
        w("\t\tout->resize(ByteSizeLong());")
        w("\t\tSerializeWithCachedSizesToArray(out->data());")
        w("\t\treturn true;")
        w("\t}")
    w("} // namespace np2_structs")
    w("")
    return "\n".join(o)


def dump(messages):
    """The canonical text of a message, field by field in number order:
    N=value; numbers in decimal, strings and bytes in hex after 'x', a
    message in braces or '-' when absent, a repeated field in brackets"""
    o = []
    w = o.append
    w("// The canonical text of RPCN's messages, for the CI test: made by")
    w("// np2_structs_gen.py --dump; np2_fixtures_gen.py writes the same from the")
    w("// protobuf library's reading.")
    w("")
    w('#include "np2_structs.h"')
    w("")
    w("#include <cstdio>")
    w("#include <string>")
    w("")
    w("namespace np2_dump")
    w("{")
    w("\tinline void num(std::string& o, unsigned long long v) { o += std::to_string(v); }")
    w("\tinline void num(std::string& o, long long v) { o += std::to_string(v); }")
    w("\tinline void hex(std::string& o, std::string_view s)")
    w("\t{")
    w("\t\tstatic const char digits[] = \"0123456789abcdef\";")
    w("\t\to += 'x';")
    w("\t\tfor (const unsigned char c : s)")
    w("\t\t{")
    w("\t\t\to += digits[c >> 4];")
    w("\t\t\to += digits[c & 15];")
    w("\t\t}")
    w("\t}")
    for name, _ in messages:
        w("\tvoid dump(std::string& o, const np2_structs::%s& m);" % name)

    def val(f, expr):
        if f.kind == "scalar":
            if f.ptype in ("int32", "int64"):
                return "num(o, static_cast<long long>(%s));" % expr
            return "num(o, static_cast<unsigned long long>(%s));" % expr
        if f.kind == "text":
            return "hex(o, %s);" % expr
        return "dump(o, %s);" % expr

    for name, fields in messages:
        w("")
        w("\tvoid dump(std::string& o, const np2_structs::%s& %s)" % (name, "m" if fields else "/*m*/"))
        w("\t{")
        w("\t\to += '{';")
        for f in fields:
            a = f.acc
            w("\t\to += \"%d=\";" % f.number)
            if f.repeated:
                w("\t\to += '[';")
                w("\t\tfor (int i = 0; i < m.%s_size(); i++)" % a)
                w("\t\t{")
                w("\t\t\t%s" % val(f, "m.%s(i)" % a))
                w("\t\t\to += ',';")
                w("\t\t}")
                w("\t\to += ']';")
            elif f.kind == "message":
                w("\t\tif (m.has_%s())" % a)
                w("\t\t\t%s" % val(f, "m.%s()" % a))
                w("\t\telse")
                w("\t\t\to += '-';")
            else:
                w("\t\t%s" % val(f, "m.%s()" % a))
            w("\t\to += ';';")
        w("\t\to += '}';")
        w("\t}")
    w("")
    w("\t// Reads @data as message @type, the canonical text into @out; false when")
    w("\t// it is not one; re-written into @again")
    w("\tinline bool by_name(std::string_view type, const void* data, std::size_t size, std::string& out, std::string& again)")
    w("\t{")
    for name, _ in messages:
        w("\t\tif (type == \"%s\")" % name)
        w("\t\t{")
        w("\t\t\tnp2_structs::%s m;" % name)
        w("\t\t\tif (!m.ParseFromArray(data, size))")
        w("\t\t\t\treturn false;")
        w("\t\t\tdump(out, m);")
        w("\t\t\tm.SerializeToString(&again);")
        w("\t\t\treturn true;")
        w("\t\t}")
    w("\t\treturn false;")
    w("\t}")
    w("} // namespace np2_dump")
    w("")
    return "\n".join(o)


def main():
    out, dump_to, args = HERE, None, sys.argv[1:]
    while args:
        if args[0] == "--out" and len(args) > 1:
            out = args[1]
        elif args[0] == "--dump" and len(args) > 1:
            dump_to = args[1]
        else:
            sys.exit("usage: np2_structs_gen.py [--out DIR] [--dump FILE]")
        args = args[2:]
    messages = ordered(parse(os.path.join(HERE, "np2_structs.proto")))
    open(os.path.join(out, "np2_structs.h"), "w", newline="\n").write(header(messages))
    open(os.path.join(out, "np2_structs.cpp"), "w", newline="\n").write(source(messages))
    if dump_to:
        open(dump_to, "w", newline="\n").write(dump(messages))


if __name__ == "__main__":
    main()
