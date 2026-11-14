#!/usr/bin/env python3
"""Makes rpcs3/libretro/make/llvm/llvm.mk and the configured headers beside it
from LLVM's own build description, so that the Makefile builds the LLVM the
core links - from the sources in 3rdparty/llvm/llvm, with make and a compiler and
nothing else - without CMake.

For the maintainer only, when LLVM is moved to another release: configure a
checkout of llvm-project twice with CMake and Ninja (README in this folder gives the
two commands), then

    python3 rpcs3/libretro/make/llvm/gen.py LINUX_BUILD_DIR MINGW_BUILD_DIR

What it takes from them:
  - which libraries the core's components need (tools/llvm-config's
    LibraryDependencies.inc), for each target (X86, AArch64) and JIT listener;
  - each library's sources and the include directories and defines each is
    compiled with (ninja -t commands), the platform's own defines left out
    for the Makefile to give;
  - every TableGen run they need, and the sources of llvm-min-tblgen and
    llvm-tblgen, which the Makefile builds first, for the build machine;
  - the configured headers (llvm/Config, Extension.def): config.h as each
    system has it (Linux; MinGW from the cross configuration; macOS written
    from Linux's, see darwin_config), llvm-config.h with the native target
    picked by the compiler's architecture, and the target lists for each
    target.
"""

import os
import re
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
SRC = os.path.join(CORE, "3rdparty", "llvm", "llvm", "llvm")

# What the core links, as 3rdparty/llvm/CMakeLists.txt asks for it
COMPONENTS = ["core", "executionengine", "mcjit", "passes"]
TARGET_COMPONENTS = {"x86": ["x86"], "aarch64": ["aarch64"]}
# Intel's VTune listener is left out: its ittapi sources are not in the
# LLVM tree (LLVM's CMake downloads them), and only a profiler uses it
LISTENERS = ["perfjitevents"]

PLATFORM_DEFINES = {"-D_GNU_SOURCE", "-D_FILE_OFFSET_BITS=64", "-DNDEBUG",
                    "-D_GLIBCXX_USE_CXX11_ABI=1"}


def components(build):
    """component -> (library or None, [required components])"""
    text = open(os.path.join(build, "tools", "llvm-config", "LibraryDependencies.inc")).read()
    out = {}
    for m in re.finditer(r'\{ "([^"]+)", (nullptr|"[^"]+"), (?:true|false), \{([^}]*)\} \}', text):
        lib = None if m.group(2) == "nullptr" else m.group(2).strip('"')
        out[m.group(1)] = (lib, re.findall(r'"([^"]+)"', m.group(3)))
    return out


def closure(table, roots):
    seen, order = set(), []

    def visit(c):
        if c in seen:
            return
        seen.add(c)
        for d in table[c][1]:
            visit(d)
        order.append(c)

    for r in roots:
        visit(r)
    return [table[c][0] for c in order if table[c][0]]


def commands(build, targets):
    out = subprocess.run(["ninja", "-C", build, "-t", "commands"] + targets,
                         capture_output=True, text=True, check=True).stdout
    return out.splitlines()


class Rewriter:
    def __init__(self, build):
        self.build = build.rstrip("/")

    def path(self, p):
        if p.startswith(SRC + "/") or p == SRC:
            return "$(LLVM_SRC)" + p[len(SRC):]
        if p.startswith(self.build + "/"):
            rel = p[len(self.build) + 1:]
            return "$(LLVM_GEN)/" + rel
        return p


def parse_compile(line, rw):
    args = shlex.split(line)
    src = args[args.index("-c") + 1]
    obj = args[args.index("-o") + 1]
    incs, defs = [], []
    for a in args:
        if a.startswith("-I"):
            p = a[2:]
            if p == os.path.join(rw.build, "include"):
                incs.append("$(LLVM_CONFIG_INC) -I$(LLVM_GEN)/include")
            else:
                incs.append(rw.path(p))
        elif a.startswith("-D") and a not in PLATFORM_DEFINES:
            defs.append(a)
    return src, obj, incs, defs


def library_objects(build, lib):
    """the objects of lib/lib<lib>.a, from its archive command"""
    for line in commands(build, ["lib/lib%s.a" % lib]):
        if (" ar " in line or "/ar " in line) and "lib/lib%s.a" % lib in line and line.startswith(": &&"):
            objs = re.findall(r"(\S+\.o)\b", line)
            return objs
    raise SystemExit("no archive command for " + lib)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    linux, mingw = (os.path.abspath(a) for a in sys.argv[1:])
    rw = Rewriter(linux)
    table = components(linux)

    common = closure(table, COMPONENTS)
    sets = {"common": common}
    for t, roots in TARGET_COMPONENTS.items():
        sets[t] = [l for l in closure(table, COMPONENTS + roots) if l not in common]
    for l in LISTENERS:
        sets[l] = [x for x in closure(table, COMPONENTS + [l]) if x not in common]

    # every compile line by object
    lines = commands(linux, ["lib/lib%s.a" % l for s in sets.values() for l in s]
                     + ["bin/llvm-tblgen", "bin/llvm-min-tblgen"])
    compiles, tblgens = {}, {}
    for line in lines:
        if line.startswith(("/usr/bin/c++ ", "/usr/bin/cc ")) and " -c " in line:
            src, obj, incs, defs = parse_compile(line, rw)
            compiles[obj] = (src, incs, defs, line.startswith("/usr/bin/cc "))
        elif "tblgen " in line and line.startswith("cd "):
            m = re.match(r"cd (\S+) && (\S+) (.*?) && /usr/bin/cmake -E cmake_transform_depfile", line)
            if not m:
                continue
            cwd, tool, rest = m.groups()
            args = shlex.split(rest)
            o = args.index("-o")
            out = os.path.normpath(os.path.join(cwd, args[o + 1]))
            del args[o:o + 2]
            if "-d" in args:
                d = args.index("-d")
                del args[d:d + 2]
            args = [a for a in args if a != "--write-if-changed"]
            td = [a for a in args if a.endswith(".td") and not a.startswith("-")]
            args = ["-I" + rw.path(a[2:]) if a.startswith("-I") else rw.path(a) for a in args]
            tblgens[out] = (os.path.basename(tool), args, rw.path(td[0]))

    # which TableGen runs each set needs
    def tblgen_for(libs, targets=None):
        need = set()
        for line in commands(linux, targets or ["lib/lib%s.a" % l for l in libs]):
            if "tblgen " in line and line.startswith("cd "):
                m = re.match(r"cd (\S+) && \S+ .*? -o (\S+)", line)
                if m:
                    need.add(os.path.normpath(os.path.join(m.group(1), m.group(2))))
        return need

    mk = []
    w = mk.append
    w("# Generated by rpcs3/libretro/make/llvm/gen.py from the LLVM submodule's")
    w("# build description: do not edit. The Makefile includes it to build the")
    w("# LLVM the core links from 3rdparty/llvm/llvm, without CMake.")
    w("")

    # the tools, for the build machine
    def tool_objs(tool):
        """the objects on the tool's link line, and those of the libraries
        it links, as the archives hold them"""
        for line in commands(linux, ["bin/" + tool]):
            if line.startswith(": &&") and (" -o bin/%s " % tool) in line:
                objs = []
                for a in shlex.split(line):
                    if a.endswith(".o"):
                        objs.append(a)
                    elif re.match(r"lib/libLLVM\w+\.a$", a):
                        objs += library_objects(linux, a[len("lib/lib"):-len(".a")])
                return list(dict.fromkeys(objs))
        raise SystemExit("no link command for " + tool)

    groups = {}

    def group_of(incs, defs):
        key = (" ".join("-I" + i for i in incs), " ".join(defs))
        if key not in groups:
            groups[key] = "LLVM_F%d" % (len(groups) + 1)
        return groups[key]

    def emit_objs(name, objs, tree):
        entries = []
        for obj in objs:
            src, incs, defs, is_c = compiles[obj]
            entries.append((rw.path(src), group_of(incs, defs), is_c))
        w("%s_SRCS := \\" % name)
        for s, g, c in entries:
            w("\t%s \\" % s)
        w("")
        # group -> sources, for the flags
        by = {}
        for s, g, c in entries:
            by.setdefault(g, []).append(s)
        for g, srcs in sorted(by.items()):
            w("$(call llvm_objs,%s,%s): LLVM_CPPFLAGS = $(%s)" % (tree, " ".join(srcs), g))
        w("")

    for tool in ("llvm-min-tblgen", "llvm-tblgen"):
        emit_objs("LLVM_" + tool.replace("-", "_").upper(), tool_objs(tool), "$(LLVM_HOST_OBJ)")

    for name, libs in sets.items():
        objs = []
        for l in libs:
            objs += library_objects(linux, l)
        objs = list(dict.fromkeys(objs))
        emit_objs("LLVM_LIB_" + name.upper(), objs, "$(LLVM_OBJ)")

    # TableGen runs
    for name, libs in sets.items():
        need = sorted(tblgen_for(libs) - (tblgen_for(common) if name != "common" else set()))
        w("LLVM_TBLGEN_%s := \\" % name.upper())
        for out in need:
            w("\t%s \\" % rw.path(out))
        w("")
    # what llvm-tblgen itself is built with: llvm-min-tblgen's output
    w("LLVM_TBLGEN_TOOL := \\")
    for out in sorted(tblgen_for(None, ["bin/llvm-tblgen"])):
        w("\t%s \\" % rw.path(out))
    w("")
    for out, (tool, args, td) in sorted(tblgens.items()):
        exe = "$(LLVM_MIN_TBLGEN)" if tool == "llvm-min-tblgen" else "$(LLVM_TBLGEN)"
        w("%s: %s | %s" % (rw.path(out), exe, td))
        w("\t@mkdir -p $(@D)")
        w("\t$(SAY) TBLGEN $@")
        w("\t$(Q)%s %s -o $@" % (exe, " ".join(args)))
        w("")

    for (incs, defs), name in sorted(groups.items(), key=lambda kv: int(kv[1][6:])):
        w("%s = %s %s" % (name, incs, defs))

    open(os.path.join(HERE, "llvm.mk"), "w").write("\n".join(mk) + "\n")
    write_headers(linux, mingw)


def write_headers(linux, mingw):
    inc = os.path.join(HERE, "include")
    os.makedirs(os.path.join(inc, "common", "llvm", "Config"), exist_ok=True)
    os.makedirs(os.path.join(inc, "common", "llvm", "Support"), exist_ok=True)
    cfg = lambda b, n: open(os.path.join(b, "include", "llvm", "Config", n)).read()

    for n in ("abi-breaking.h",):
        open(os.path.join(inc, "common", "llvm", "Config", n), "w").write(cfg(linux, n))
    open(os.path.join(inc, "common", "llvm", "Support", "Extension.def"), "w").write(
        open(os.path.join(linux, "include", "llvm", "Support", "Extension.def")).read())
    open(os.path.join(inc, "common", "llvm", "Support", "VCSRevision.h"), "w").write(
        "#undef LLVM_REVISION\n#undef LLVM_REPOSITORY\n")

    # What the machine that configured it happened to have installed is not
    # what a build machine has: valgrind's header among them (a build without
    # it stopped at Valgrind.cpp). The core does not run under valgrind's
    # client requests.
    linux_cfg = re.sub(r"#define HAVE_VALGRIND_VALGRIND_H 1", "/* #undef HAVE_VALGRIND_VALGRIND_H */",
                       cfg(linux, "config.h"))
    systems = {"linux": linux_cfg, "windows": cfg(mingw, "config.h")}
    systems["darwin"] = darwin_config(systems["linux"])
    for system, text in systems.items():
        d = os.path.join(inc, system, "llvm", "Config")
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, "config.h"), "w").write(text)
        base = cfg(mingw if system == "windows" else linux, "llvm-config.h")
        open(os.path.join(d, "llvm-config.h"), "w").write(native_target(base, system))

    for target, arch in (("x86", "X86"), ("aarch64", "AArch64")):
        d = os.path.join(inc, target, "llvm", "Config")
        os.makedirs(d, exist_ok=True)
        for n, macro in (("Targets.def", "LLVM_TARGET"), ("AsmPrinters.def", "LLVM_ASM_PRINTER"),
                         ("AsmParsers.def", "LLVM_ASM_PARSER"), ("Disassemblers.def", "LLVM_DISASSEMBLER"),
                         ("TargetMCAs.def", "LLVM_TARGETMCA"), ("TargetExegesis.def", "LLVM_EXEGESIS")):
            text = cfg(linux, n)
            # only this target's line
            text = re.sub(r"^%s\((X86|AArch64)\)\n" % macro, lambda m: m.group(0) if m.group(1) == arch else "", text, flags=re.M)
            open(os.path.join(d, n), "w").write(text)
        text = cfg(linux, "Targets.h")
        open(os.path.join(d, "Targets.h"), "w").write(text)


def native_target(text, system):
    """llvm-config.h with the triples and native target the compiler builds for"""
    triples = {"linux": ("x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu"),
               "windows": ("x86_64-w64-windows-gnu", "aarch64-w64-windows-gnu"),
               "darwin": ("x86_64-apple-darwin", "arm64-apple-darwin")}[system]
    lines = []
    for line in text.splitlines():
        m = re.match(r"#define (LLVM_DEFAULT_TARGET_TRIPLE|LLVM_HOST_TRIPLE) ", line)
        if m:
            lines += ["#if defined(__aarch64__) || defined(_M_ARM64)",
                      '#define %s "%s"' % (m.group(1), triples[1]), "#else",
                      '#define %s "%s"' % (m.group(1), triples[0]), "#endif"]
            continue
        m = re.match(r"#define (LLVM_NATIVE_\w+) (\S+)", line)
        if m:
            x86 = m.group(2)
            a64 = x86.replace("X86", "AArch64")
            lines += ["#if defined(__aarch64__) || defined(_M_ARM64)", "#define %s %s" % (m.group(1), a64),
                      "#else", "#define %s %s" % (m.group(1), x86), "#endif"]
            continue
        if system == "darwin" and line.startswith("#define LLVM_USE_PERF "):
            line = "#define LLVM_USE_PERF 0"
        if line.startswith("#define LLVM_USE_INTEL_JITEVENTS "):
            line = "#define LLVM_USE_INTEL_JITEVENTS 0"
        if system == "linux" and line.startswith("#define LLVM_USE_PERF "):
            line = "#define LLVM_USE_PERF 1"
        lines.append(line)
    return "\n".join(lines) + "\n"


def darwin_config(text):
    """macOS's config.h, from Linux's: what Darwin has and lacks"""
    have = {"HAVE_MALLINFO": None, "HAVE_MALLINFO2": None, "HAVE_MALLCTL": None,
            "HAVE_MALLOC_MALLOC_H": "1", "HAVE_MALLOC_ZONE_STATISTICS": "1",
            "HAVE_LINK_H": None, "HAVE_MACH_MACH_H": "1", "HAVE_PROC_PID_RUSAGE": "1",
            "HAVE_PTHREAD_GETNAME_NP": "1", "HAVE_PTHREAD_SETNAME_NP": "1",
            "HAVE_SYSEXITS_H": "1", "HAVE_UNW_ADD_DYNAMIC_FDE": "1",
            "HAVE_CRASHREPORTER_INFO": None, "HAVE_CRASHREPORTERCLIENT_H": None,
            "HAVE_DECL_ARC4RANDOM": "1", "HAVE_LSEEK64": None, "HAVE_PIPE2": None,
            "HAVE_SBRK": "1", "HAVE_VALGRIND_VALGRIND_H": None,
            "HAVE_DECL_FE_ALL_EXCEPT": "1", "HAVE_DECL_FE_INEXACT": "1",
            "HAVE_GETAUXVAL": None, "HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC": None,
            "HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC": "1", "HAVE_FUTIMENS": "1",
            "HAVE_FUTIMES": "1", "HAVE_POSIX_FALLOCATE": None, "HAVE_SETENV": "1",
            "HAVE_DLADDR": "1", "HAVE_DLOPEN": "1", "HAVE_REGISTER_FRAME": "1",
            "HAVE_DEREGISTER_FRAME": "1", "HAVE_BACKTRACE": "TRUE",
            "LLVM_ENABLE_CRASH_DUMPS": "0"}
    out = []
    for line in text.splitlines():
        m = re.match(r"(?:#define|/\* #undef) (\w+)", line)
        if m and m.group(1) in have:
            v = have[m.group(1)]
            line = "#define %s %s" % (m.group(1), v) if v else "/* #undef %s */" % m.group(1)
        if line.startswith("#define LLVM_PLUGIN_EXT "):
            line = '#define LLVM_PLUGIN_EXT ".dylib"'
        if line.startswith("#define LTDL_SHLIB_EXT "):
            line = '#define LTDL_SHLIB_EXT ".dylib"'
        if line.startswith("#define LLVM_HOST_TRIPLE ") or line.startswith("#define LLVM_DEFAULT_TARGET_TRIPLE "):
            pass
        out.append(line)
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    main()
