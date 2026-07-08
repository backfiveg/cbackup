# cbackup — Linux C++ Data Backup Tool

A fully-featured, Linux-native data backup system written in C++17.

## Features

| Feature | Flag | Points |
|---------|------|--------|
| Directory tree backup (copy mode) | `backup` | Core |
| Directory tree restore | `restore` | Core |
| Special files (symlinks, FIFOs, devices) | `-` | EX-01 |
| Metadata preservation (uid/gid/mode/timestamps) | `-m` | EX-02 |
| Custom filter (include/exclude globs) | `--include` / `--exclude` | EX-03 |
| Binary pack / unpack archive (.cbk) | `pack` / `unpack` | EX-04 |
| zlib compression | `-z` | EX-05 |
| Scheduled backup with eviction | `cron` | EX-06 |
| Real-time inotify daemon | `daemon` | EX-07 |

## Build

### Prerequisites (inside Docker or Linux)

```bash
apt-get install -y build-essential cmake make zlib1g-dev libgtest-dev
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

### Pack into single archive (with compression + metadata)

```bash
cbackup pack -z -m --source /path/to/src --dest /path/to/archive.cbk
```

### Unpack archive

```bash
cbackup unpack --file /path/to/archive.cbk --dest /path/to/recover_dir
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

## Archive Format (.cbk)

```
[ArchiveHeader: magic(4B) + version(2B) + compressed(1B) + reserved(9B)]
[EntryHeader + name + data] × N
[Footer checksum (4B)]
```

- Magic: `CBKP` (0x43424B50)
- Version: `0x0100`
- Each entry stores: type, name, file data (optionally zlib-compressed), uid/gid/mode/timestamps/dev info.
- Security: path traversal (`..`) entries are rejected on unpack.

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
│   ├── compress/compressor.h
│   ├── cron/cron_scheduler.h
│   ├── daemon/inotify_daemon.h
│   └── utils/{logger,path_utils,fd_wrapper}.h
├── src/
│   ├── main.cpp
│   ├── cli/cli_parser.cpp
│   ├── backup/{backup,restore}.cpp
│   ├── filter/filter.cpp
│   ├── pack/{packer,unpacker}.cpp
│   ├── compress/compressor.cpp
│   ├── cron/cron_scheduler.cpp
│   ├── daemon/inotify_daemon.cpp
│   └── utils/{logger,path_utils,fd_wrapper}.cpp
└── tests/
    ├── test_backup.cpp
    ├── test_filter.cpp
    ├── test_pack.cpp
    ├── test_compress.cpp
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
