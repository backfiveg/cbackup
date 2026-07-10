# GUI 界面改进说明

> 本文档记录了对 `gui/templates/index.html` 所做的全部 UX 改进，按时间倒序排列。

---

## 第三次改进 (2026-07-10)：用户体验全面优化

### 用户反馈的问题

1. **操作执行完毕后 source/dest 路径仍在表单中残留** — 用户希望有清空表单的方式
2. **加密打包后密码仍然显示在表单上** — 安全风险，密码应在操作完成后自动清除

### 从用户角度发现的其他问题

3. 没有重置表单的按钮 — 只能手动逐字段清空
4. 执行按钮在运行期间未禁用 — 可重复点击导致混乱
5. 算法和过滤面板对所有 subcommand 都可见 — backup/restore 不适用但仍显示
6. 无键盘快捷键 — 必须用鼠标点击执行
7. 必填字段无标识 — 用户不知道哪些字段必须填
8. dest 字段的 placeholder 不随 subcommand 变化 — pack 时提示 `/data/archive.cbk` 但 unpack/restore 应该是目录路径
9. subcommand 切换时密码不清空 — 从 pack 切到其他操作时密码仍然存在
10. 执行期间无耗时显示 — 用户不知道操作运行了多久

### 修改清单

#### 1. 新增「重置」按钮

```
修复前：只有「执行」「停止」「清屏」三个按钮
修复后：新增「↺ 重置」按钮，一键清空所有表单字段
```

- 清空所有路径字段（source, dest, file, schedule, keep）
- 重置压缩/加密下拉框为默认值
- 清空密码字段
- 清空全部 6 维过滤字段
- 取消勾选 metadata/verbose
- 终端输出 `[FORM] 表单已重置`

#### 2. 密码安全：自动清除

```
修复前：密码在执行后仍保留在表单中
修复后：以下 3 种情况自动清除密码
```

| 触发条件 | 说明 |
|----------|------|
| 操作执行完毕（done 事件） | 无论成功或失败，立即清除 |
| 用户手动点击「停止」 | 中断操作时也清除 |
| 切换 subcommand | `applyLayout()` 中清除 |

#### 3. 执行按钮运行时禁用

```
修复前：执行期间按钮仍可点击，重复点击导致多个 SSE 连接
修复后：
  - 按钮变为灰色，显示「⏳ 执行中...」
  - disabled 属性阻止重复点击
  - 操作完成后恢复为绿色「▶ 执行」
```

#### 4. 算法 / 过滤面板按 subcommand 显隐

```
修复前：算法组合 + 6维过滤 面板对所有 subcommand 始终可见
修复后：按 subcommand 动态显隐
```

| subcommand | 压缩/加密下拉框 | 密码字段 | 6维过滤面板 |
|------------|:---:|:---:|:---:|
| pack       | ✅ | ✅ | ✅ |
| unpack     | ❌ | ✅ (解密用) | ❌ |
| cron       | ✅ | ✅ | ✅ |
| backup     | ❌ | ❌ | ❌ |
| restore    | ❌ | ❌ | ❌ |

实现方式：
- `algoVisible(sub)` — 控制压缩/加密下拉框
- `passVisible(sub)` — 控制密码字段（pack/cron/unpack 都需要）
- `$('filter-group').classList.toggle('hidden', ...)` — 控制过滤面板
- 隐藏过滤面板时自动清空过滤字段值

#### 5. 键盘快捷键 Enter

```
修复前：必须鼠标点击「执行」按钮
修复后：按 Enter 键即可执行（执行期间 disabled 时 Enter 无效）
```

在 `<body>` 上添加 `onkeydown` 监听：
```javascript
onkeydown="if(event.key==='Enter' && !$('run').disabled) $('run').click()"
```

#### 6. 必填字段标识

在 CSS 中添加 `.req::after` 伪元素，为必填字段标签添加红色 `*`：

```css
.req::after { content: " *"; color: #f87171; font-size: 10px; }
```

必填字段：`--source`、`--dest`、`--file`（unpack 时）

#### 7. dest 字段动态 placeholder

| subcommand | placeholder |
|------------|-------------|
| pack       | `/backup/project.cbk` |
| unpack     | `/restore/location` |
| backup     | `/backup/location` |
| restore    | `/restore/location` |
| cron       | `/backup/location` |

实现：`destPlaceholders` 映射表，在 `applyLayout()` 中动态设置。

#### 8. 执行耗时显示

终端标题栏在执行期间动态显示已运行秒数：
```
修复前：cbackup — live output（静态）
修复后：cbackup — 运行中 (12s)（每秒更新）
```

#### 9. 底部提示栏

新增一行操作提示：
```
提示：按 Enter 执行 | 带 * 为必填项 | 密码在操作完成后自动清除
```

#### 10. 「停止」按钮视觉反馈

运行期间「停止」按钮切换为红色主题，明确表示可中断操作。

---

## 第二次改进 (2026-07-10)：防止字段残留导致参数污染

### 问题

切换 subcommand 时，隐藏字段的旧值仍保留并被发送到后端。
例如：从 `pack` 切换到 `unpack` 后，隐藏的 `source` 字段仍保留 `/tmp/guitest/src`，
导致 `--source` 被错误地传给 `unpack` 命令。

### 修改

#### 1. `applyLayout()` 清空隐藏字段

```javascript
// 修复前
el.classList.toggle('hidden', !shouldShow);

// 修复后
el.classList.toggle('hidden', !shouldShow);
if (!shouldShow) {
    const input = el.querySelector('input');
    if (input) input.value = '';
}
```

#### 2. `buildQuery()` 按 subcommand 过滤参数

```javascript
// 修复前：所有字段无条件发送
const keys = ['subcmd','source','dest','file',...];

// 修复后：只发送当前 subcommand 相关的字段
const layoutKeys = (layout[sub] || []);
const algoKeys = packScoped ? ['compress','encrypt','password'] : [];
if (sub === 'unpack') algoKeys.push('password');
const filterKeys = packScoped ? ['include','exclude','type','mtime','size','owner'] : [];
const keys = [...alwaysKeys, ...layoutKeys, ...algoKeys, ...filterKeys];
```

#### 3. dest 字段 placeholder 修正

```
修复前：/data/archive.cbk（所有 subcommand 相同，对 unpack 误导）
修复后：/backup/location（更通用）
```

---

## 第一次改进 (初始版本)

### 已有功能

- Flask + SSE 实时终端输出
- subcommand 白名单校验（防止命令注入）
- `shell=False` + argv 列表（安全）
- 布局切换（pack/unpack/backup/restore/cron 显示不同字段组）
- 停止按钮（关闭 SSE 连接）
- 清屏按钮

---

## 对应的 Commit

| Commit | 说明 |
|--------|------|
| `a3f85af` | GUI 表单切换 subcommand 时清理隐藏字段 + buildQuery 按 subcommand 过滤 |
| 本次 | 用户体验全面优化：重置按钮、密码自动清除、执行按钮禁用、面板显隐、Enter 快捷键、必填标识、动态 placeholder、运行计时 |
