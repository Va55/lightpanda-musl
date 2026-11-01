# Building native-musl Lightpanda (x86_64 and i386) — validated recipe

Produces fully static, pure-musl Lightpanda binaries for **x86_64** and **i386 (32-bit x86 / i686)**.
Verified to build and run with zero glibc on both 64-bit and 32-bit musl (Alpine) hosts.

## The critical discovery

With Lightpanda's Zig build, `-Dtarget=*-linux-musl` reaches **only the final Zig exe
link**. The two heavy halves compile for glibc regardless:

- **V8** (zig-v8-fork's `build-v8`): compiles against glibc (objects reference glibc symbols).
- **Rust FFI** (html5ever + std): `browser/build.zig` runs `cargo build` with no `--target`,
  so it builds for the glibc host.

Consequence: the musl link throws a **finite, enumerable set of undefined glibc-only
symbols**. Don't recompile all of V8 + Rust for musl (a multi-day rabbit hole). Instead
provide the missing glibc symbols over musl via a small compat shim
([musl_glibc_shim.c](../musl_glibc_shim.c), the "ZL-V8 drop-in layer") and fold it into the link.

Undefined symbols the shim covers:

- Large-file `*64` family (musl is already 64-bit; each aliases its plain form):
  `mmap64, fstat64, stat64, lstat64, lseek64, ftruncate64, fseeko64, ftello64, readdir64,
  tmpfile64, fopen64, open64, __open64_2, __xstat64/__fxstat64/__lxstat64/__fxstat`.
- `__isoc23_strtoll/strtoull/strtol/strtoul/sscanf` (glibc C23 variants; musl already C23).
- Fortify `_chk`: `__vsnprintf_chk/__vprintf_chk/__vfprintf_chk/__snprintf_chk/__printf_chk/__fprintf_chk`
  (routed to the plain func; drops hardening).
- `backtrace, backtrace_symbols, backtrace_symbols_fd` (musl has none; shim uses
  `_Unwind_Backtrace` from the C++ runtime + `dladdr`).
- `__sysconf` (= `sysconf`), `__mbrlen` (= `mbrlen`), `__libc_stack_end` (set from
  `/proc/self/maps` `[stack]` end in a constructor; V8 reads it for the main-thread stack start).

> **The `*64` block must be UNCONDITIONAL (no `#if __x86_64__` guard).** i686 musl does
> NOT export the `*64` symbols either — Rust's i686-musl std references them and the link
> fails with `undefined symbol: fopen64/...` otherwise. musl's `off_t`/`struct stat` are
> 64-bit on every arch, so the aliases are valid on i386 too.

## Fold the shim into the V8 archive

```sh
cd <v8>/out/linux/release_*/obj/zig
cp libc_v8.a libc_v8_musl.a
zig cc -target <x86_64|x86>-linux-musl -O2 -c musl_glibc_shim.c -o shim.o
ar r libc_v8_musl.a shim.o
```

## Build

Build host: any Linux box with Docker (a 24-core / ~30 GB host builds V8 in ~7 min).
Run everything inside an isolated container (Debian trixie) with `zig 0.17.0`, a Rust
toolchain, `clang`, `ninja`, `git`, `cmake`, `binutils` (`apt-get install -y binutils`),
and for the i686 port `gcc-multilib`.

V8:

```sh
zig build -Dtarget=x86_64-linux-musl -Doptimize=ReleaseFast prepare-v8   # gclient sync, ~2 GiB
zig build -Dtarget=x86_64-linux-musl -Doptimize=ReleaseFast build-v8     # -> libc_v8.a
```

Lightpanda — point at the shim-carrying archive:

```sh
cd browser
# x86_64
zig build -Dtarget=x86_64-linux-musl -Doptimize=ReleaseFast \
  -Dprebuilt_v8_path=<abs path>/libc_v8_musl.a
# -> browser/zig-out/bin/lightpanda (~208 MB, statically linked)
```

### i386 (32-bit x86 / i686) — the real port

The i386 build is a full port, not a flag. `-Dtarget=x86-linux-musl` alone silently builds
an x86_64 V8 (the target never reaches V8's own compile; zig-v8-fork's GN args never set
`target_cpu`). Required additional work, all validated:

1. **i386 V8** — patch zig-v8-fork's `build.zig` `GnArgs` Linux case for `arch == .x86`:
   `target_cpu="x86"` + `v8_enable_pointer_compression=false` +
   `v8_enable_sandbox=false` + `treat_warnings_as_errors=false`. Produces real
   Intel 80386 objects (~123 MB vs ~141 MB x86_64). Needs `gcc-multilib` for
   `bits/wordsize.h`.
2. **Rust FFI cross** — pass `-Dcargo_target=i686-unknown-linux-musl` to `zig build`.
   This requires an upstream-able patch to `build.zig`'s `linkRust`: add a `cargo_target`
   build option; when set, `cargo` gets `--target <triple>` **and** the archive is read
   from `<target-dir>/<triple>/release/liblightpanda_ffi.a` (cargo writes cross-target
   output into the triple subdir, not the host `release/` path — otherwise the host x86_64
   Rust archive gets linked into the i386 exe → `is incompatible with elf_i386`).
3. **~40 32-bit Zig port fixes** in Lightpanda's own source. Representative fixes
   (the rest are the same shape — a 64-bit constant or u64↔usize cast that breaks when
   `usize` is u32):
   - `string.zig`: `assert(@alignOf(String) == 8)` → `== @sizeOf(usize)` (4 on i386;
     size/offset invariants already hold on both).
   - `VideoFrame.zig:389,791`: `for (0..<u64 bound>)` → `@intCast` to usize.
   - 64-bit atomics on 32-bit Zig (`std.atomic.Value(u64)` unsupported): a helper wrapping
     `std.atomic.Value(u32)` for the watchdog/log/stall fields (load/store/swap/fetchAdd/
     cmpxchg via `@truncate`; call sites drop their ordering args).
   - `TaggedOpaque.zig`: gate the 48-bit tagged-pointer compact scheme behind
     `@sizeOf(usize) >= 8`; force expanded plain pointers on 32-bit (never eval `1<<48`).
   - `Integer.zig` init: Zig has no `.uint` tag (unsigned types report `.int`) — branch on
     signedness.
   - `header_parser.zig broadcast()`: build the SWAR byte-mask by looping `@sizeOf(usize)`
     shifts (a hardcoded `0x01..01` u64 overflows 32-bit usize).
   - `Snapshot.zig`/`Env.zig`: cap `NewDefaultAllocator` arg to `maxInt(usize)` on 32-bit
     (4 GB doesn't fit `size_t`).
   - `EventManagerBase.zig:296`: drop `*align(8)` from the `@fieldParentPtr` local.
   - `sys/libcurl.zig`/`network/http.zig`: `curl_off_t != c_long` on i386 (it's
     `c_longlong`) — use the `curl_off_t` type for the curl_off_t-typed getinfo infos.

```sh
cd browser
rustup target add i686-unknown-linux-musl
zig build -Dtarget=x86-linux-musl -Doptimize=ReleaseFast \
  -Dcargo_target=i686-unknown-linux-musl \
  -Dprebuilt_v8_path=<ia32>/obj/zig/libc_v8_musl_ia32.a
# -> browser/zig-out/bin/lightpanda (~177 MB, ELF32 Intel i386, statically linked)
```

## Deploy + verify

```sh
sudo install -m 0755 lightpanda /usr/local/bin/lightpanda
ldd /usr/local/bin/lightpanda        # "not a dynamic executable" (statically linked)
lightpanda version                    # -> Lightpanda 1.1.0-dev
lightpanda fetch --dump markdown https://example.com
lightpanda fetch --dump semantic_tree_text https://example.com   # accessibility tree
```

## Notes / pitfalls

- Long builds: run the container detached with logs redirected to a **host-mounted** path
  and poll the host-side log file (a container's writable layer is lost on exit).
- The comment hazard: the string `strto*/sscanf` inside a `/* */` block comment terminates
  the comment (`*/` is a real compile error). Write `strtoX`.
- V8 source is ~2 GiB via `chromium.googlesource.com`; gclient auto-retries blips.
