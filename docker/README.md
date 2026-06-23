# rampart docker oven

Builds a **portable `centos7-<arch>` rampart** inside a manylinux "oven", without
disturbing your normal native build. `<arch>` (`x86_64` or `aarch64`) is derived
from the host automatically.

Two base tiers, chosen with **`-b`**:
- **`-b 2014`** (default) — [manylinux2014], **glibc 2.17** floor (CentOS 7 and
  everything newer). Maximum reach.
- **`-b 2_28`** — manylinux_2_28 (AlmaLinux 8), **glibc 2.28** floor, but a much
  newer gcc-toolset → modern SIMD (matters on ARM: SVE2/i8mm SimSIMD), and it's
  the base for the GPU tier.

This is the **only** entry point; nothing in any `CMakeLists.txt` references
`docker/`. The "grab libraries" step (yum deps + OpenBLAS from source) runs
**once** at image-build time and is cached — so re-running `build` just recompiles.

```
docker/build.sh [-b 2014|2_28] [-d <install-dir>] <stage>
```

## Commands

| Command | What it does |
|---|---|
| `docker/build.sh build` | Compile centos7 rampart into `build/manylinux2014/` |
| `docker/build.sh test` | Run `build/manylinux2014/src/run_tests.sh` |
| `docker/build.sh install` | Replace `/usr/local/rampart-ml` (asks first) |
| `docker/build.sh all` | `build` + `test` + `install` |
| `docker/build.sh shell` | Interactive shell in the oven |
| `docker/build.sh save-image` | Persist the oven image to a `.tar.gz` (see below) |
| `docker/build.sh --rebuild-image [stage]` | Force a fresh oven image first (after a `Dockerfile` edit) |
| `docker/build.sh install --yes` | Skip the install confirmation prompt |
| `docker/build.sh -b 2_28 build` | Build on the manylinux_2_28 base (glibc 2.28) instead of 2014 |
| `docker/build.sh -d <dir> install` | Install into `<dir>` instead of `/usr/local/rampart-ml` |

`-b` and `-d` are leading options (any order), e.g. `docker/build.sh -b 2_28 -d /usr/local/rampart-ml-28 build`. The 2_28 image is named `rampart-manylinux_2_28`.

After `install`, publish on the **host** with the non-compile mkrp steps:

```
mkrp bundle && mkrp package && mkrp publish
```

`mkrp` reads the platform from the installed binary's `rampart.buildPlatform`
(`centos7-<arch>; <uname>`), not from `/usr/local/rampart-build` — your normal
debian `/usr/local/rampart-build` file is never touched. Restore your normal
native build with `sudo make -j install`.

## Mounted directories

Nothing host-facing is baked into the image — it's all bind-mounted at
`docker run` time. `$REPO` is the rampart repo root (`/usr/local/src/rampart`).

| Stage | Host path → container path | Mode |
|---|---|---|
| **build** | `/usr/local/src/rampart` → `/src` | rw |
| **test** | `/usr/local/src/rampart` → `/src` | rw |
| | `/etc/passwd` → `/etc/passwd` | ro |
| | `/etc/group` → `/etc/group` | ro |
| **install** | `/usr/local/src/rampart` → `/src` | rw |
| | `/usr/local/rampart-ml` → `/usr/local/rampart-ml` | rw |
| **shell** | `/usr/local/src/rampart` → `/src` | rw |

Why each one:

- **Repo (`/src`)** — always rw: the build writes outputs to `build/manylinux2014/`
  on the host and chowns them back to you (via `HOST_UID`/`HOST_GID`).
- **`/usr/local/rampart-ml`** (or the `-d` dir) — mounted rw only at `install`, to
  replace it. With `-d`, only the *host* side changes; the container path stays
  `/usr/local/rampart-ml`. Only this subdir is mounted, not all of `/usr/local`.
- **`/etc/passwd` + `/etc/group`** (ro) — only on `test`, which runs as your uid
  (`--user`) so the uid resolves to a name (`run_tests.sh` needs `whoami`).
  `build`/`install` run as root and use the `HOST_UID` env var instead.

Everything else (devtoolset-11, OpenBLAS, yum deps) lives **inside** the image.

## The oven image

The image (`rampart-manylinux2014`) lives in your local docker store and
persists there across reboots and container runs — you don't need anything else
to reuse it. `build.sh` finds it automatically.

`save-image` additionally writes it to `build/rampart-manylinux2014.image.tar.gz`
(a large file). This is only needed to:

1. **move it to another machine** (`docker load` there),
2. **back it up** before an aggressive prune / docker reinstall,
3. keep a frozen snapshot independent of the daemon.

If that tarball exists, `ensure_image` restores it with `docker load` instead of
rebuilding. After editing the `Dockerfile`, rebuild with `--rebuild-image`.

> A plain `docker image prune -f` only removes **dangling** (untagged) images and
> will not touch `rampart-manylinux2014`. Only `docker rmi`, `docker image prune
> -a`, `docker system prune -a`, or a docker reinstall remove it — and even then
> the `Dockerfile` reproduces it deterministically (needs network).

[manylinux2014]: https://github.com/pypa/manylinux
