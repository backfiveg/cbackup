# cbackup — Linux C++ Data Backup Tool

A fully-featured, Linux-native data backup system written in C++17.

## Features

| Feature | Flag | Points |
|---------|------|--------|
| Directory tree backup (copy mode) | `backup` | Core |
| Directory tree restore | `restore` | Core |
| Special files (symlinks, FIFOs, devices, hardlinks) | `-` | EX-01 |
| Metadata preservation (uid/gid/mode/timestamps) | `-m` | EX-02 |
| Custom filter — 6 dimensions | `--include/--exclude/--type/--mtime/--size/--owner` | EX-03 |
| Binary pack / unpack archive (.cbk) | `pack` / `unpack` | EX-04 |
| Compression — **Huffman (hand-impl)** + zlib | `--compress huffman\|zlib` | EX-05 |
| Encryption — **RC4 (hand-impl)** + AES-128 (OpenSSL) | `--encrypt rc4\|aes -p <key>` | EX-06 |
| Scheduled backup with eviction | `cron` | EX-07 |
| Real-time inotify daemon | `daemon` | EX-08 |
| Web GUI (Flask + SSE live terminal) | see `gui/` | EX-09 |

Both compression and encryption use a **factory pattern** to select the
algorithm at runtime, and are streamed together as `compress → encrypt` on
pack and `decrypt → decompress` on unpack.

## Build

### Prerequisites (inside Docker or Linux)

```bash
apt-get install -y build-essential cmake make zlib1g-dev libssl-dev libgtest-dev
# For the Web GUI:
apt-get install -y python3 python3-pip && pip3 install flask
```

### Compile

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Run Tests

```bash
cd build
ctest --output-on-failure
# or directly:
./cbackup_tests
```

### Valgrind

```bash
valgrind --leak-check=full ./cbackup backup --source /tmp/src --dest /tmp/dst
```

### Profiling

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
./cbackup backup --source /large/dir --dest /tmp/dst
gprof cbackup gmon.out | less
```

## Docker

```bash
docker build -t cbackup .
docker run --rm cbackup --help
docker run --rm -v /host/src:/src -v /host/dst:/dst cbackup backup --source /src --dest /dst
```

## Usage

### Basic backup

```bash
cbackup backup --source /path/to/src --dest /path/to/backup_dir
```

### Backup with metadata + special files

```bash
cbackup backup -m --source /path/to/src --dest /path/to/backup_dir
```

### Filtered backup

```bash
cbackup backup --source /path/to/src --dest /path/to/dest \
    --exclude "*.tmp" --include "*.cpp"
```

### Filtered backup — all 6 dimensions (EX-03)

```bash
cbackup backup --source /data/src --dest /data/dst \
    --include "*.cpp" --exclude ".git/*" \
    --type regular,symlink --mtime "-7d" --size "<500M" --owner root
```

- `--include` : by name (glob)
- `--exclude` : by path (glob)
- `--type`    : `regular,symlink,dir,special`
- `--mtime`   : `-7d` (modified within 7 days) / `+30d` (older than 30 days)
- `--size`    : `<500M` / `>100K` (units B/K/M/G)
- `--owner`   : username or numeric uid

### Pack into single archive (with compression + metadata)

```bash
# zlib
cbackup pack -z -m --source /path/to/src --dest /path/to/archive.cbk
# hand-implemented Huffman
cbackup pack --compress huffman -m --source /path/to/src --dest /path/to/archive.cbk
```

### Pack + compress + encrypt (EX-05 + EX-06)

```bash
# Huffman compression + AES-128 encryption + filters (the "ultimate" command)
cbackup pack --compress huffman --encrypt aes -p "secret123" \
    --mtime "-7d" --size "<500M" --owner root \
    --source /data/src --dest /data/archive.cbk

# hand-implemented RC4 stream cipher
cbackup pack --encrypt rc4 -p "secret123" --source /data/src --dest /data/out.cbk
```

### Unpack archive (password required if encrypted)

```bash
cbackup unpack --file /path/to/archive.cbk --dest /path/to/recover_dir -p "secret123"
```

### Restore from backup directory

```bash
cbackup restore --source /path/to/backup_dir --dest /path/to/recover_dir
```

### Real-time inotify daemon

```bash
cbackup daemon --watch /path/to/src --sync-to /path/to/dest --log /var/log/cbackup.log
```

### Scheduled cron backup (keep 5 latest)

```bash
cbackup cron --schedule "0 2 * * *" --source /path/to/src --dest /path/to/dest --keep 5
```

## Web GUI (EX-09)

A Flask "glue" server (`gui/app.py`) renders a geek-console dashboard and
invokes the C++ `cbackup` binary via `subprocess.Popen` (shell disabled),
streaming stdout/stderr to the browser through Server-Sent Events (SSE) for a
live "black terminal" effect. All business logic remains 100% C++.

```bash
# Build the C++ engine first, then launch the GUI:
cd build && cmake .. && make -j$(nproc) && cd ..
python3 gui/app.py           # serves http://localhost:8080

# Or via Docker (port exposed):
docker build -t cbackup .
docker run --rm -p 8080:8080 -v /host/data:/data cbackup
```

## Archive Format (.cbk)

```
[ArchiveHeader: magic(4B) + version(2B) + compress_algo(1B) + cipher_algo(1B) + reserved(8B)]
[EntryHeader + name + payload] × N
[Footer checksum (4B)]
```

- Magic: `CBKP` (0x43424B50), Version: `0x0200`
- `compress_algo`: 0=none, 1=zlib, 2=huffman ; `cipher_algo`: 0=none, 1=rc4, 2=aes128
- Each payload is processed as **compress → encrypt** on pack; `EntryHeader.orig_len`
  keeps the original size so unpack can `decrypt → decompress` correctly.
- AES payloads are prefixed with a 16-byte random IV; the key is `SHA-256(password)[0:16]`.
- Security: path-traversal (`..`) entries are rejected on unpack.

## Project Structure

```
cbackup/
├── CMakeLists.txt
├── Dockerfile
├── .devcontainer/devcontainer.json
├── .gitignore
├── README.md
├── include/
│   ├── cli/cli_parser.h
│   ├── backup/backup.h
│   ├── filter/filter.h
│   ├── pack/packer.h
│   ├── compress/{compressor,huffman}.h
│   ├── crypto/crypto.h
│   ├── cron/cron_scheduler.h
│   ├── daemon/inotify_daemon.h
│   └── utils/{logger,path_utils,fd_wrapper}.h
├── src/
│   ├── main.cpp
│   ├── cli/cli_parser.cpp
│   ├── backup/{backup,restore}.cpp
│   ├── filter/filter.cpp
│   ├── pack/{packer,unpacker}.cpp
│   ├── compress/{compressor,huffman}.cpp
│   ├── crypto/{crypto,rc4,aes}.cpp
│   ├── cron/cron_scheduler.cpp
│   ├── daemon/inotify_daemon.cpp
│   └── utils/{logger,path_utils,fd_wrapper}.cpp
├── gui/
│   ├── app.py                 # Flask glue + SSE
│   ├── requirements.txt
│   └── templates/index.html   # geek-console dashboard
└── tests/
    ├── test_backup.cpp
    ├── test_filter.cpp
    ├── test_pack.cpp
    ├── test_compress.cpp
    ├── test_huffman.cpp
    ├── test_crypto.cpp
    ├── test_cron.cpp
    └── test_path_utils.cpp
```

## Code Quality

```bash
# cpplint
cpplint --recursive src/ include/

# valgrind
valgrind --leak-check=full --error-exitcode=1 ./cbackup_tests

# perf
perf stat ./cbackup pack --source /large/dir --dest /tmp/out.cbk
```

## Branch Strategy

- `main` — stable, tested, ready for submission
- `dev` — daily integration branch
- `feature/*` — individual feature branches

## Commit Convention

- `feat:` new feature
- `fix:` bug fix
- `refactor:` refactoring
- `docs:` documentation
- `test:` test additions/changes
