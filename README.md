# lightpanda-musl

Native, **statically-linked, pure-musl** builds of [Lightpanda](https://lightpanda.io)
(`1.1.0-dev`) for two architectures, plus the build recipe that produces them. No glibc
anywhere at runtime — these run on any Linux regardless of libc (Alpine/musl,
Debian/Ubuntu/glibc, …) because they are fully static.

Largest value is the **i686 (i386) port**: modern V8 engines dropped 32-bit support, but
this repo proves a usable browser can still be hand-built for one. The capability
toolchain (`target_cpu=x86` V8 + compat shim + sysroot) carries directly to an i386
Chromium should that ever be needed.

## Releases

The binaries are too large to commit (GitHub's 100 MB/file limit), so they ship as
**GitHub Releases** — statically-linked musl, archived per architecture:

| Asset | Arch | Size |
|-------|------|------|
| `lightpanda-1.1.0-dev-x86_64-musl` | x86_64 | ~208 MB |
| `lightpanda-1.1.0-dev-i686-musl`   | i686 (Intel i386) | ~177 MB |

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
