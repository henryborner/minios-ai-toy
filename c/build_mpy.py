#!/usr/bin/env python3
"""Standalone parallel builder for MiniOS Ring 3 MicroPython (MPY.BIN)."""
from __future__ import annotations

import concurrent.futures
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CDIR = ROOT / "c"
PORT_DIR = CDIR / "mpy_port"
MPY_TOP = ROOT / "tools" / "micropython"
BUILD = PORT_DIR / "build"
GENHDR = BUILD / "genhdr"
OUT_ELF = BUILD / "mpy.elf"
OUT_BIN = BUILD / "mpy.bin"
FROZEN_DIR = BUILD / "frozen"
FROZEN_C = BUILD / "frozen_content.c"

BIG_CODE_MAX = 0x00080000  # 512 KB virtual window (0x400000..0x480000)


def find_tool(names: list[str], extra: list[Path]) -> str:
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    for path in extra:
        if path.exists():
            return str(path)
    raise SystemExit(f"tool not found: {names} / {extra}")


PY_CORE_BASENAMES = [
    "mpstate.c", "nlr.c", "malloc.c", "gc.c", "pystack.c", "qstr.c", "vstr.c",
    "mpprint.c", "unicode.c", "mpz.c", "reader.c", "lexer.c", "parse.c",
    "scope.c", "compile.c", "emitcommon.c", "emitbc.c", "asmbase.c",
    "asmx64.c", "emitnx64.c", "asmx86.c", "emitnx86.c", "asmthumb.c",
    "emitnthumb.c", "emitinlinethumb.c", "asmarm.c", "emitnarm.c",
    "asmxtensa.c", "emitnxtensa.c", "emitinlinextensa.c", "emitnxtensawin.c",
    "asmrv32.c", "emitnrv32.c", "emitndebug.c", "formatfloat.c",
    "parsenumbase.c", "parsenum.c", "emitglue.c", "persistentcode.c",
    "runtime.c", "runtime_utils.c", "scheduler.c", "nativeglue.c",
    "pairheap.c", "ringbuf.c", "cstack.c", "stackctrl.c", "argcheck.c",
    "warning.c", "profile.c", "map.c", "obj.c", "objarray.c", "objattrtuple.c",
    "objbool.c", "objboundmeth.c", "objcell.c", "objclosure.c", "objcomplex.c",
    "objdeque.c", "objdict.c", "objenumerate.c", "objexcept.c", "objfilter.c",
    "objfloat.c", "objfun.c", "objgenerator.c", "objgetitemiter.c", "objint.c",
    "objint_longlong.c", "objint_mpz.c", "objlist.c", "objmap.c", "objmodule.c",
    "objobject.c", "objpolyiter.c", "objproperty.c", "objnone.c",
    "objnamedtuple.c", "objrange.c", "objreversed.c", "objringio.c",
    "objset.c", "objsingleton.c", "objslice.c", "objstr.c", "objstrunicode.c",
    "objstringio.c", "objtuple.c", "objtype.c", "objzip.c", "opmethods.c",
    "sequence.c", "stream.c", "binary.c", "builtinimport.c", "builtinevex.c",
    "builtinhelp.c", "modarray.c", "modbuiltins.c", "modcollections.c",
    "modgc.c", "modio.c", "modmath.c", "modcmath.c", "modmicropython.c",
    "modstruct.c", "modsys.c", "moderrno.c", "modthread.c", "vm.c", "bc.c",
    "showbc.c", "repl.c", "smallint.c", "frozenmod.c",
]

EXTMOD_FILES = [
    "extmod/modbinascii.c",
    "extmod/modheapq.c",
    "extmod/modjson.c",
    "extmod/modrandom.c",
    "extmod/modre.c",
    "extmod/modtime.c",
]

SHARED_FILES = [
    "shared/libc/printf.c",
    "shared/libc/string0.c",
    "shared/readline/readline.c",
    "shared/runtime/interrupt_char.c",
    "shared/runtime/pyexec.c",
    "shared/runtime/stdout_helpers.c",
    "shared/runtime/sys_stdio_mphal.c",
]

LIBM_DBL_FILES = [
    f"lib/libm_dbl/{name}" for name in [
        "__cos.c", "__expo2.c", "__fpclassify.c", "__rem_pio2.c",
        "__rem_pio2_large.c", "__signbit.c", "__sin.c", "__tan.c",
        "acos.c", "acosh.c", "asin.c", "asinh.c", "atan.c", "atan2.c",
        "atanh.c", "ceil.c", "copysign.c", "cos.c", "cosh.c", "erf.c",
        "exp.c", "expm1.c", "floor.c", "fmod.c", "frexp.c", "ldexp.c",
        "lgamma.c", "log.c", "log10.c", "log1p.c", "modf.c", "nearbyint.c",
        "pow.c", "rint.c", "round.c", "scalbn.c", "sin.c", "sinh.c",
        "sqrt.c", "tan.c", "tanh.c", "tgamma.c", "trunc.c",
    ]
]

PORT_C_FILES = [
    "main.c",
    "mphalport.c",
    "minilibc.c",
    "vfs_minios.c",
    "modminios.c",
]


def run_cmd(args: list[str], cwd: Path | None = None, capture: bool = False) -> bytes:
    if capture:
        return subprocess.check_output(args, cwd=cwd)
    subprocess.run(args, cwd=cwd, check=True)
    return b""


def generate_headers(clang: str, base_cflags: list[str], qstr_sources: list[Path]) -> None:
    GENHDR.mkdir(parents=True, exist_ok=True)
    py_exe = sys.executable
    py_dir = MPY_TOP / "py"

    # 1. mpversion.h
    mpver = GENHDR / "mpversion.h"
    run_cmd([py_exe, str(py_dir / "makeversionhdr.py"), str(mpver)], cwd=MPY_TOP)

    # 2. Preprocess all QSTR-bearing C sources -> qstr.i.last
    qstr_i_last = GENHDR / "qstr.i.last"
    pp_args = [
        py_exe,
        str(py_dir / "makeqstrdefs.py"),
        "pp",
        clang, "-E",
        "output", str(qstr_i_last),
        "cflags", *base_cflags, "-DNO_QSTR",
        "cxxflags", "-DNO_QSTR",
        "sources", *[str(p) for p in qstr_sources],
        "dependencies", str(PORT_DIR / "mpconfigport.h"),
        "changed_sources",
    ]
    run_cmd(pp_args, cwd=MPY_TOP)

    # 3. Split & cat qstr / module / root_pointer / compress
    modes = [
        ("qstr", "qstrdefs.collected.h"),
        ("module", "moduledefs.collected"),
        ("root_pointer", "root_pointers.collected"),
        ("compress", "compressed.collected"),
    ]
    for mode, collected_name in modes:
        split_dir = GENHDR / mode
        if split_dir.exists():
            shutil.rmtree(split_dir)
        split_dir.mkdir(parents=True, exist_ok=True)
        run_cmd([
            py_exe, str(py_dir / "makeqstrdefs.py"),
            "split", mode, str(qstr_i_last), str(split_dir), "_",
        ], cwd=MPY_TOP)
        run_cmd([
            py_exe, str(py_dir / "makeqstrdefs.py"),
            "cat", mode, "_", str(split_dir), str(GENHDR / collected_name),
        ], cwd=MPY_TOP)

    # 4. Generate moduledefs.h & root_pointers.h
    mod_h = run_cmd([
        py_exe, str(py_dir / "makemoduledefs.py"),
        str(GENHDR / "moduledefs.collected"),
    ], cwd=MPY_TOP, capture=True)
    (GENHDR / "moduledefs.h").write_bytes(mod_h)

    rp_h = run_cmd([
        py_exe, str(py_dir / "make_root_pointers.py"),
        str(GENHDR / "root_pointers.collected"),
    ], cwd=MPY_TOP, capture=True)
    (GENHDR / "root_pointers.h").write_bytes(rp_h)

    # 5. Generate qstrdefs.preprocessed.h & qstrdefs.generated.h
    raw_qstrs = (
        (py_dir / "qstrdefs.h").read_text(encoding="utf-8") + "\n"
        + (PORT_DIR / "qstrdefsport.h").read_text(encoding="utf-8") + "\n"
        + (GENHDR / "qstrdefs.collected.h").read_text(encoding="utf-8") + "\n"
    )
    protected_lines = []
    re_q = re.compile(r"^(Q\(.*\))$")
    for line in raw_qstrs.splitlines():
        m = re_q.match(line.strip())
        if m:
            protected_lines.append(f'"{m.group(1)}"')
        else:
            protected_lines.append(line)
    protected_src = ("\n".join(protected_lines) + "\n").encode("utf-8")

    proc = subprocess.run(
        [clang, "-E", *base_cflags, "-x", "c", "-"],
        input=protected_src,
        stdout=subprocess.PIPE,
        check=True,
    )
    re_unq = re.compile(r'^"(Q\(.*\))"$')
    unwrapped = []
    for line in proc.stdout.decode("utf-8", "replace").splitlines():
        m = re_unq.match(line.strip())
        if m:
            unwrapped.append(m.group(1))
        else:
            unwrapped.append(line)
    pre_path = GENHDR / "qstrdefs.preprocessed.h"
    pre_path.write_text("\n".join(unwrapped) + "\n", encoding="utf-8")

    gen_qstr = run_cmd([
        py_exe, str(py_dir / "makeqstrdata.py"), str(pre_path),
    ], cwd=MPY_TOP, capture=True)
    (GENHDR / "qstrdefs.generated.h").write_bytes(gen_qstr)


def build_mpy(stage_to_c_build: bool = False) -> Path:
    if not (MPY_TOP / "py" / "vm.c").exists():
        raise SystemExit(
            f"MicroPython upstream source missing at {MPY_TOP}.\n"
            "Clone v1.24.1 into tools/micropython first."
        )

    nasm = find_tool(["nasm"], [ROOT / "tools" / "nasm-2.16.03" / "nasm.exe"])
    clang = find_tool(["clang"], [
        Path("C:/msys64/ucrt64/bin/clang.exe"),
        Path("C:/msys64/usr/bin/clang.exe"),
    ])
    ld = find_tool(["ld.lld", "ld.lld.exe"], [
        Path("C:/msys64/ucrt64/bin/ld.lld.exe"),
    ])
    objcopy = find_tool(["objcopy"], [
        Path("C:/msys64/ucrt64/bin/objcopy.exe"),
        Path("C:/msys64/usr/bin/objcopy.exe"),
    ])
    mpy_cross = find_tool(["mpy-cross"], [
        Path("C:/Users/Henry/AppData/Local/Programs/Python/Python313/Scripts/mpy-cross.exe"),
    ])

    BUILD.mkdir(parents=True, exist_ok=True)

    inc_flags = [
        f"-I{PORT_DIR / 'include'}",
        f"-I{PORT_DIR}",
        f"-I{ROOT}",
        f"-I{MPY_TOP}",
        f"-I{BUILD}",
    ]
    base_cflags = [
        "--target=i386-unknown-none-elf",
        "-m32",
        "-march=i586",
        "-m80387",
        "-mno-sse",
        "-mno-mmx",
        "-std=gnu11",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-pic",
        "-fno-pie",
        "-fno-stack-protector",
        "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables",
        "-fdata-sections",
        "-ffunction-sections",
        "-Os",
        "-DNDEBUG",
        "-Wno-unused-parameter",
        "-Wno-sign-compare",
        "-Wno-gnu-designator",
        *inc_flags,
    ]

    py_sources = [MPY_TOP / "py" / name for name in PY_CORE_BASENAMES]
    ext_sources = [MPY_TOP / rel for rel in EXTMOD_FILES]
    shared_sources = [MPY_TOP / rel for rel in SHARED_FILES]
    libm_sources = [MPY_TOP / rel for rel in LIBM_DBL_FILES]
    port_sources = [PORT_DIR / name for name in PORT_C_FILES]

    qstr_sources = [
        *[p for p in py_sources if not p.name.startswith("nlr")],
        *ext_sources,
        MPY_TOP / "shared/readline/readline.c",
        MPY_TOP / "shared/runtime/pyexec.c",
        MPY_TOP / "shared/runtime/sys_stdio_mphal.c",
        *port_sources,
    ]

    print("[mpy] generating QSTR and module headers...")
    generate_headers(clang, base_cflags, qstr_sources)

    print("[mpy] freezing c/pylib tools...")
    FROZEN_DIR.mkdir(parents=True, exist_ok=True)
    frozen_names = ["calc", "top", "demos", "xxd", "ed", "futil", "bench", "art"]
    frozen_mpys = []
    for name in frozen_names:
        src = CDIR / "pylib" / (name + ".py")
        out = FROZEN_DIR / (name + ".mpy")
        run_cmd([mpy_cross, src.name, "-o", str(out)], cwd=src.parent)
        frozen_mpys.append(out)
    FROZEN_C.unlink(missing_ok=True)
    frozen_c = run_cmd([sys.executable, str(MPY_TOP / "tools" / "mpy-tool.py"),
                        "-f", "-q", str(GENHDR / "qstrdefs.preprocessed.h"),
                        *[p.name for p in frozen_mpys]],
                       cwd=FROZEN_DIR, capture=True)
    FROZEN_C.write_bytes(frozen_c)

    # Assemble start.asm
    start_o = BUILD / "start.o"
    run_cmd([nasm, "-f", "elf32", "-o", str(start_o), str(PORT_DIR / "start.asm")])

    all_c_sources: list[tuple[Path, Path, list[str]]] = []
    for src in py_sources + ext_sources + shared_sources:
        rel = src.relative_to(MPY_TOP)
        obj = BUILD / "up" / rel.with_suffix(".o")
        extra = ["-O2"] if src.name in ("vm.c", "gc.c") else []
        all_c_sources.append((src, obj, extra))

    for src in libm_sources:
        rel = src.relative_to(MPY_TOP)
        obj = BUILD / "up" / rel.with_suffix(".o")
        all_c_sources.append((src, obj, ["-Wno-double-promotion", "-Wno-float-conversion"]))

    for src in port_sources:
        obj = BUILD / "port" / src.with_suffix(".o").name
        all_c_sources.append((src, obj, []))
    all_c_sources.append((FROZEN_C, BUILD / "frozen_content.o", []))

    def compile_one(item: tuple[Path, Path, list[str]]) -> Path:
        src, obj, extra = item
        obj.parent.mkdir(parents=True, exist_ok=True)
        if obj.exists() and obj.stat().st_mtime >= src.stat().st_mtime:
            gen_mtime = (GENHDR / "qstrdefs.generated.h").stat().st_mtime
            if obj.stat().st_mtime >= gen_mtime:
                return obj
        subprocess.run(
            [clang, *base_cflags, *extra, "-c", str(src), "-o", str(obj)],
            check=True,
        )
        return obj

    print(f"[mpy] compiling {len(all_c_sources)} C files...")
    workers = min(16, (os.cpu_count() or 4))
    objects: list[Path] = [start_o]
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for obj in pool.map(compile_one, all_c_sources):
            objects.append(obj)

    print("[mpy] linking mpy.elf...")
    run_cmd([
        ld,
        "-m", "elf_i386",
        "--gc-sections",
        "-T", str(PORT_DIR / "mpy_linker.ld"),
        "-o", str(OUT_ELF),
        *[str(o) for o in objects],
    ])
    run_cmd([objcopy, "-O", "binary", str(OUT_ELF), str(OUT_BIN)])

    raw = OUT_BIN.read_bytes()
    if len(raw) < 20 or raw[:4] != b"UMP1":
        raise SystemExit("[mpy] invalid UMP1 header in output binary!")
    entry_off, img_sz, bss_sz, heap_req = struct.unpack_from("<IIII", raw, 4)
    total_mem = img_sz + bss_sz
    print(
        f"[mpy] MPY.BIN built: file={len(raw):,} B ({len(raw)/1024:.1f} KB), "
        f"entry=0x{entry_off:x}, image={img_sz:,} B, bss={bss_sz:,} B, "
        f"code+bss={total_mem:,} B / {BIG_CODE_MAX:,} B"
    )
    if total_mem > BIG_CODE_MAX:
        raise SystemExit(f"[mpy] ERROR: image+bss ({total_mem}) exceeds BIG_CODE_MAX ({BIG_CODE_MAX})")

    if stage_to_c_build:
        dst = CDIR / "build" / "mpy.bin"
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(OUT_BIN, dst)
        print(f"[mpy] staged -> {dst}")

    return OUT_BIN


def main() -> int:
    stage = "--stage" in sys.argv
    if "--clean" in sys.argv and BUILD.exists():
        shutil.rmtree(BUILD)
    build_mpy(stage_to_c_build=stage)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())








