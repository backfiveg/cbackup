# cbackup GUI 交互式测试流程

> 本文档引导你通过浏览器图形界面，一步一步测试 cbackup 的全部功能。
> 每项测试包含：准备工作 → 操作步骤 → 预期现象 → 验证方法。

---

## 0. 准备工作

### 0.1 启动 GUI

**方式一：Docker（推荐）**
```bash
docker run --rm -p 8080:8080 cbackup
```
如果 8080 被占用，换一个端口：
```bash
docker run --rm -p 8081:8080 cbackup
# 浏览器访问 http://localhost:8081
```

**方式二：本地 Python**
```bash
cd build && cmake .. && make -j$(nproc) && cd ..
python3 gui/app.py
# 浏览器访问 http://localhost:8080
```

### 0.2 进入容器准备测试数据（Docker 方式需另开终端）

```bash
# 另开一个终端，进入同一个容器（如果用的本地方式则直接在宿主机操作）
docker exec -it $(docker ps -q --filter "ancestor=cbackup") bash
```

如果用的是 `python3 gui/app.py` 本地方式，直接在本地终端准备数据即可。

### 0.3 创建标准测试目录树

```bash
# 创建测试根目录
mkdir -p /tmp/guitest/src/docs
mkdir -p /tmp/guitest/src/code
mkdir -p /tmp/guitest/src/.git/objects
mkdir -p /tmp/guitest/src/build

# 各类测试文件
echo "Hello World - this is a text file." > /tmp/guitest/src/readme.txt
echo '#include <iostream>' > /tmp/guitest/src/code/main.cpp
echo 'void helper() {}' > /tmp/guitest/src/code/utils.cpp
echo '// old code' > /tmp/guitest/src/code/legacy.cpp
dd if=/dev/zero of=/tmp/guitest/src/docs/big_report.pdf bs=1M count=50 2>/dev/null   # 50MB 文件
dd if=/dev/urandom of=/tmp/guitest/src/build/output.bin bs=1M count=200 2>/dev/null  # 200MB 文件
echo "temporary" > /tmp/guitest/src/build/cache.tmp
echo "git data" > /tmp/guitest/src/.git/objects/abc123

# 创建有不同时间戳的文件
touch -t 202401010000 /tmp/guitest/src/code/legacy.cpp   # 旧文件 (2024年)
touch /tmp/guitest/src/code/main.cpp                       # 新文件 (现在)

# 创建符号链接 (需要 --metadata 支持)
ln -s readme.txt /tmp/guitest/src/link_readme 2>/dev/null || echo "symlink may not work on all systems"

echo "=== 测试数据准备完毕 ==="
echo "源目录: /tmp/guitest/src"
ls -laR /tmp/guitest/src/
```

---

## 1. 基础功能测试

### 测试1-1：基础目录备份 (backup)

**目的**：验证最基本的备份功能

| 项 | 内容 |
|----|------|
| 左侧表单 | 操作=*backup*，源目录=`/tmp/guitest/src`，目标=`/tmp/guitest/bak1` |
| 预期终端输出 | `[DONE] Backup complete: N files, X.XX MB` |
| 验证 | `ls -laR /tmp/guitest/bak1` — 目录结构与 src 一致 |

操作步骤：
1. 选择操作：**backup — 目录复制备份**
2. 源目录填入：`/tmp/guitest/src`
3. 目标填入：`/tmp/guitest/bak1`
4. 点击 **▶ 执行**

预期终端显示类似：
```
$ /workspace/build/cbackup backup --source /tmp/guitest/src --dest /tmp/guitest/bak1
[DONE] Backup complete: 7 files, XX.XX MB
[EXIT] code=0
```

---

### 测试1-2：基础还原 (restore)

**目的**：验证还原功能

| 项 | 内容 |
|----|------|
| 左侧表单 | 操作=*restore*，源目录=`/tmp/guitest/bak1`，目标=`/tmp/guitest/rst1` |
| 预期 | 还原成功后，rst1 内容与原始 src 一致 |
| 验证 | `diff -r /tmp/guitest/src /tmp/guitest/rst1` |

操作步骤：
1. 选择操作：**restore — 目录还原**
2. 源目录填入：`/tmp/guitest/bak1`
3. 目标填入：`/tmp/guitest/rst1`
4. 点击 **▶ 执行**

---

## 2. 打包 (pack) 核心功能测试

### 测试2-1：基础打包 (无压缩无加密)

**目的**：验证最简打包/解包流程

| 项 | 内容 |
|----|------|
| 操作 | **pack** |
| 源 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/pack_basic.cbk` |
| 压缩 | 不压缩 |
| 加密 | 不加密 |
| 预期 | `[DONE] Pack complete: N entries` |

然后测试解包：
| 项 | 内容 |
|----|------|
| 操作 | **unpack** |
| 归档文件 | `/tmp/guitest/pack_basic.cbk` |
| 目标 | `/tmp/guitest/out_basic` |
| 密钥 | 留空 |
| 预期 | 文件一致 |

---

### 测试2-2：Zlib 压缩打包

**目的**：验证第三方 Zlib 压缩

| 项 | 内容 |
|----|------|
| 操作 | **pack** |
| 源 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/pack_zlib.cbk` |
| 压缩 | **zlib (第三方库)** |
| 加密 | 不加密 |
| 预期 | 终端显示打包完成，归档体积比 pack_basic.cbk 小 |

---

### 测试2-3：Huffman 手工压缩打包

**目的**：验证自研 Huffman 压缩

| 项 | 内容 |
|----|------|
| 操作 | **pack** |
| 源 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/pack_huff.cbk` |
| 压缩 | **huffman (手工实现)** |
| 加密 | 不加密 |
| 预期 | 打包成功，与 Zlib 对比归档大小 |

**至此你应该已经产出了 3 个 .cbk 文件。记录它们的大小：**
```bash
ls -lh /tmp/guitest/pack_*.cbk
```

---

## 3. 加密功能测试

### 测试3-1：RC4 加密打包（手工实现）

**目的**：验证 RC4 流密码

| 项 | 内容 |
|----|------|
| 操作 | **pack** |
| 源 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/pack_rc4.cbk` |
| 压缩 | 不压缩 |
| 加密 | **rc4 (手工实现)** |
| 密钥 | 输入 `myrc4key` |

然后解包验证：

| 项 | 内容 |
|----|------|
| 操作 | **unpack** |
| 归档文件 | `/tmp/guitest/pack_rc4.cbk` |
| 目标 | `/tmp/guitest/out_rc4` |
| 密钥 | `myrc4key` |
| 预期 | ✅ 解密成功，文件一致 |

---

### 测试3-2：AES-128 加密打包（OpenSSL）

**目的**：验证 AES-128-CBC 分组加密

| 项 | 内容 |
|----|------|
| 操作 | **pack** |
| 源 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/pack_aes.cbk` |
| 压缩 | 不压缩 |
| 加密 | **aes-128 (OpenSSL)** |
| 密钥 | `secret123!` |

解包用正确密钥 → 成功；再用**错误**密钥 → 失败验证安全：

| 测试 | 密钥 | 预期 |
|------|------|------|
| 正确解包 | `secret123!` | ✅ 成功 |
| 错误解包 | `wrongpass` | ❌ 失败 (红色 EXIT code≠0) |

---

### 测试3-3：终极组合 — Huffman + AES + 完整过滤

**目的**：验证压缩→加密管线串行正确

| 项 | 内容 |
|----|------|
| 操作 | **pack** |
| 源 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/pack_ultimate.cbk` |
| 压缩 | **huffman (手工实现)** |
| 加密 | **aes-128 (OpenSSL)** |
| 密钥 | `ultimate123` |

后面加上完整过滤参数（见下一节）。先只测算法组合：
1. 填好上述表单 → ▶ 执行
2. 解包：unpack → archive=`/tmp/guitest/pack_ultimate.cbk` → dest=`/tmp/guitest/out_ultimate` → 密码=`ultimate123`
3. 验证文件一致

---

## 4. 六维自定义过滤测试

### 测试4-1：按名称过滤 (--include)

**目的**：只备份匹配模式的文件

| 项 | 内容 |
|----|------|
| 操作 | **pack** |
| 源 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/filter_name.cbk` |
| 按名称 | 填入 `*.cpp` |

**预期**：只有 .cpp 文件被打包，.txt/.tmp/.pdf/.bin 被过滤掉

解包验证：
```bash
ls /tmp/guitest/out_name/
# 预期只有 code/ 目录下的 .cpp 文件，没有 readme.txt
```

---

### 测试4-2：按路径排除 (--exclude)

**目的**：排除特定目录或文件

| 项 | 内容 |
|----|------|
| 操作 | **pack** |
| 源 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/filter_exclude.cbk` |
| 按路径 | 填入 `.git/*` |

**预期**：`.git/objects/abc123` 被打包跳过

再测试排除 `build/*`：

| 项 | 内容 |
|----|------|
| 目标 | `/tmp/guitest/filter_exclude2.cbk` |
| 按路径 | `build/*` |
| 预期 | build 目录下的文件全被跳过 |

---

### 测试4-3：按类型过滤 (--type)

**目的**：只备份指定类型的文件

| 值 | 含义 |
|----|------|
| `regular` | 普通文件 |
| `symlink` | 符号链接 |
| `dir` | 目录 |
| `special` | FIFO/设备 |

操作步骤：
1. 操作=**pack**，源=`/tmp/guitest/src`，目标=`/tmp/guitest/filter_type.cbk`
2. 按类型填入：`regular`（只要普通文件）
3. ▶ 执行

**预期**：只打包普通文件，符号链接被跳过

---

### 测试4-4：按时间过滤 (--mtime)

**目的**：按修改时间筛选

| 语法 | 含义 | 测试用例 |
|------|------|---------|
| `-7d` | 最近 7 天内修改的 | 填入 `-7d` |
| `+365d` | 超过一年前的 | 填入 `+365d` |

操作步骤：
1. 操作=**pack**，源=`/tmp/guitest/src`，目标=`/tmp/guitest/filter_mtime.cbk`
2. 按时间填入：`-7d`（只要最近7天修改的文件）
3. ▶ 执行

**预期**：
- `main.cpp`(新文件) → ✅ 被包含
- `legacy.cpp`(2024年) → ❌ 被过滤
- `readme.txt`(刚创建) → ✅ 被包含

---

### 测试4-5：按尺寸过滤 (--size)

**目的**：按文件大小筛选

| 语法 | 含义 |
|------|------|
| `<10M` | 小于 10MB |
| `>100M` | 大于 100MB |
| `<500K` | 小于 500KB |

操作步骤：
1. 操作=**pack**，源=`/tmp/guitest/src`，目标=`/tmp/guitest/filter_size.cbk`
2. 按尺寸填入：`<10M`
3. ▶ 执行

**预期**：
- `readme.txt`(几十字节) → ✅ 通过
- `big_report.pdf`(50MB) → ❌ 被过滤
- `output.bin`(200MB) → ❌ 被过滤

---

### 测试4-6：按属主过滤 (--owner)

**目的**：按文件所有者筛选

1. 操作=**pack**，源=`/tmp/guitest/src`，目标=`/tmp/guitest/filter_owner.cbk`
2. 按用户填入：`root`（Docker 内默认用户）
3. ▶ 执行

**预期**：root 拥有的文件被包含，其他用户被过滤

---

### 测试4-7：6维组合过滤

**目的**：全部维度同时生效

| 过滤维度 | 填入值 |
|----------|--------|
| 按名称 | `*.cpp` |
| 按路径 | `.git/*` |
| 按类型 | `regular` |
| 按时间 | `-7d` |
| 按尺寸 | `<100M` |
| 按用户 | `root` |

操作步骤：
1. 操作=**pack**，源=`/tmp/guitest/src`，目标=`/tmp/guitest/filter_all6.cbk`
2. 全部填入上表参数
3. ▶ 执行

**预期**：只有同时满足所有 6 个条件的文件被包含。大概率只有 `main.cpp` 和 `utils.cpp`。

---

## 5. 元数据保留测试

### 测试5-1：保留权限与时间戳

**目的**：验证 `-m` (metadata) 标志

1. 创建特殊权限文件：
```bash
echo "executable" > /tmp/guitest/src/run.sh
chmod 0755 /tmp/guitest/src/run.sh
touch -t 202401010101.30 /tmp/guitest/src/run.sh
stat /tmp/guitest/src/run.sh
```

2. GUI 操作：
   - 操作=**pack**，源=`/tmp/guitest/src`，目标=`/tmp/guitest/pack_meta.cbk`
   - ✅ 勾选 **保留元数据 -m**

3. 解包：
   - 操作=**unpack**，归档=`/tmp/guitest/pack_meta.cbk`，目标=`/tmp/guitest/out_meta`

4. 验证权限被保留：
```bash
stat /tmp/guitest/src/run.sh
stat /tmp/guitest/out_meta/run.sh
# 对比 mode, atime, mtime 是否一致
```

---

## 6. 错误处理与边界测试

### 测试6-1：源目录不存在

| 项 | 内容 |
|----|------|
| 操作 | **backup** |
| 源 | `/tmp/nonexistent_xyz` |
| 目标 | `/tmp/guitest/dummy` |
| 预期 | 终端显示红色错误，EXIT code≠0 |

### 测试6-2：加密但未设密码

| 项 | 内容 |
|----|------|
| 操作 | **pack** |
| 源 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/test.cbk` |
| 加密 | **aes-128** |
| 密钥 | **留空！** |
| 预期 | 终端报错 "Encryption requested but no password" 或 "require a password" |

### 测试6-3：非法子命令被拦截

直接在浏览器地址栏输入：
```
http://localhost:8080/stream?subcmd=rm&source=/etc&dest=/dev/null
```
**预期**：终端显示 `[ERROR] invalid subcommand: 'rm'`（红色），不执行任何命令。

### 测试6-4：停止按钮

1. 执行一个耗时操作（如备份大量数据）
2. 在执行过程中点击 **■ 停止**
3. **预期**：终端停止更新，状态变为 STOPPED

---

## 7. 定时备份测试 (cron)

### 测试7-1：验证 cron 参数传递

| 项 | 内容 |
|----|------|
| 操作 | **cron** |
| 源 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/cron_out` |
| 计划 | `*/1 * * * *`（每分钟） |
| 保留份数 | `2` |

**说明**：cron 是阻塞式的，会一直运行。在 GUI 中测试时：
1. 设置参数后 ▶ 执行
2. 观察终端显示 `Cron scheduler started`
3. 等待下一分钟触发，观察到备份执行
4. 点击 **■ 停止** 终止

> 实际测试建议：schedule 设为 `*/1 * * * *`（每分钟触发），等 1~2 次备份后停止即可。

**验证淘汰机制**：
```bash
ls -lt /tmp/guitest/cron_out/*.cbk
# 预期只保留最新的 2 个 .cbk 文件
```

---

## 8. 综合场景测试（演示路线）

按以下顺序执行，模拟一次完整的演示流程：

### 场景：开发者备份项目源码

**场景描述**：你有一个 C++ 项目目录，需要：
- 排除 `.git/` 和 `build/` 目录
- 只保留 `.cpp` 和 `.h` 文件
- 排除超过 100MB 的大文件
- 使用 Huffman 压缩 + AES 加密

**GUI 操作**：

| 字段 | 值 |
|------|-----|
| 操作 | **pack** |
| 源目录 | `/tmp/guitest/src` |
| 目标 | `/tmp/guitest/demo_project.cbk` |
| 压缩 | **huffman (手工实现)** |
| 加密 | **aes-128 (OpenSSL)** |
| 密钥 | `demoproject2026` |
| 按名称 | `*.cpp` (运行两次，再加一次填 `*.h`) |
| 按路径 | `build/*` (运行一次，再加 `.git/*` 再运行一次) |
| 按尺寸 | `<100M` |
| ✅ 保留元数据 | 勾选 |

然后演示解包恢复：
| 字段 | 值 |
|------|-----|
| 操作 | **unpack** |
| 归档文件 | `/tmp/guitest/demo_project.cbk` |
| 目标 | `/tmp/guitest/demo_restored` |
| 密钥 | `demoproject2026` |

---

## 9. 完整测试清单 (Checklist)

打印或用 Markdown 勾选，逐项完成：

```
□ 测试1-1  : backup 基础备份
□ 测试1-2  : restore 基础还原
□ 测试2-1  : pack 基础打包 + unpack 解包
□ 测试2-2  : pack + Zlib 压缩
□ 测试2-3  : pack + Huffman 压缩 (手工实现)
□ 测试3-1  : pack + RC4 加密 (手工实现) + 正确/错误密码解包
□ 测试3-2  : pack + AES-128 加密 (OpenSSL) + 正确/错误密码解包
□ 测试3-3  : Huffman + AES 终极组合
□ 测试4-1  : 过滤 - 按名称 (*.cpp)
□ 测试4-2  : 过滤 - 按路径排除 (.git/*, build/*)
□ 测试4-3  : 过滤 - 按类型 (regular)
□ 测试4-4  : 过滤 - 按时间 (-7d)
□ 测试4-5  : 过滤 - 按尺寸 (<10M)
□ 测试4-6  : 过滤 - 按属主 (root)
□ 测试4-7  : 过滤 - 6维组合
□ 测试5-1  : 元数据保留 -m
□ 测试6-1  : 错误处理 - 源目录不存在
□ 测试6-2  : 错误处理 - 加密无密码
□ 测试6-3  : 安全防护 - 非法子命令拦截
□ 测试6-4  : 停止按钮
□ 测试7-1  : cron 定时备份 + 淘汰
□ 场景8   : 综合演示路线
```

---

## 10. 快速验证命令（容器内执行）

在 GUI 操作的同时，可以在终端验证结果：

```bash
# 查看所有产出的 .cbk 文件大小
ls -lh /tmp/guitest/*.cbk

# 验证解包内容
ls -laR /tmp/guitest/out_*/

# 对比两个目录
diff -r /tmp/guitest/src /tmp/guitest/bak1

# 查看某个归档文件的二进制头 (魔数 CBKP)
xxd /tmp/guitest/pack_basic.cbk | head -5
# 预期看到: 4b42 4350 = "CBKP"
```
