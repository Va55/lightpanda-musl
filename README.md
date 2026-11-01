# lightpanda-static

**Fully static** Lightpanda builds — pure-musl, and archiving **i386 (32-bit x86)** as well as
x86_64. "static" is the point: no glibc (and no libc-loader) needed at runtime, so they run on
any Linux regardless of distro (Alpine/musl, Debian/Ubuntu/glibc, distroless, …). The build
recipe that produces them is included.

Largest value is the **i386 (32-bit x86 / i686) port**: modern V8 engines dropped 32-bit support, but
this repo proves a usable browser can still be hand-built for one. The capability
toolchain (`target_cpu=x86` V8 + compat shim + sysroot) carries directly to an i386
Chromium should that ever be needed.

## Releases

The binaries are too large to commit (GitHub's 100 MB/file limit), so they ship as
**GitHub Releases** — statically-linked musl, archived per architecture:

| Asset | Arch | Size |
|-------|------|------|
| `lightpanda-1.1.0-dev-x86_64-musl` | x86_64 | ~208 MB |
| `lightpanda-1.1.0-dev-i686-musl`   | i386 (32-bit x86) | ~177 MB |

> **Naming note:** the 32-bit asset file is named `i686` (its real ISA floor — V8 requires
> CMOV/MMX, i.e. Pentium Pro). "i386" is the human-facing label: that is how Debian and the
> ecosystem name 32-bit x86, and it's what the port is for. A true 80386/486 (no CMOV) cannot
> run it — same situation as Debian's `i386` arch, which also requires i686 in practice.

## Install

```sh
sudo install -m 0755 <downloaded binary> /usr/local/bin/lightpanda
```

## Verify

```sh
lightpanda version                                    # -> Lightpanda 1.1.0-dev
ldd /usr/local/bin/lightpanda                          # "not a dynamic executable" (static)
lightpanda fetch --dump semantic_tree_text https://example.com
```

## Build from source

Full toolchain and commands in [build/README.md](build/README.md). The key built-in
ingredient is [build/musl_glibc_shim.c](build/musl_glibc_shim.c) — a small glibc-symbol
compat layer (`*64` LFS family, `__isoc23_*`, fortify `_chk`, `backtrace*`,
`__libc_stack_end`) folded into the V8 archive so Lightpanda links pure-musl.

```sh
# x86_64
zig build -Dtarget=x86_64-linux-musl -Doptimize=ReleaseFast \
  -Dprebuilt_v8_path=<libc_v8_musl.a, shim folded in>

# i686 (i386 port)
rustup target add i686-unknown-linux-musl
zig build -Dtarget=x86-linux-musl -Doptimize=ReleaseFast \
  -Dcargo_target=i686-unknown-linux-musl \
  -Dprebuilt_v8_path=<libc_v8_musl_ia32.a, shim folded in>
```

## License

MIT — see [LICENSE](LICENSE).
