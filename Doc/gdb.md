# GDB 调试指南 (Slave Service)

本文档旨在为 `slave_service` 提供 GDB 调试的详细指南。

## 1. 编译准备

为了使用 GDB 进行调试，必须在编译时包含调试信息（`-g` 选项）。

## 2. 启动 GDB

### 2.1 直接启动
直接加载可执行文件进行调试：
```bash
gdb ./slave_service
```
进入 gdb 交互界面后，可以使用 `run` (或 `r`) 命令开始运行程序。

### 2.2 附加到正在运行的进程
如果 `slave_service` 已经在运行，可以通过 PID 附加调试：

1. 查找进程 ID (PID):
   ```bash
   ps -ef | grep slave_service
   ```
2. 启动 GDB 并附加:
   ```bash
   sudo gdb attach <PID>
   ```
   *注意：附加到正在运行的进程可能需要 sudo 权限。*

## 3. 常用 GDB 命令

| 命令 | 简写 | 描述 |
| --- | --- | --- |
| `run` | `r` | 开始运行程序 |
| `break <location>` | `b` | 设置断点 (例如 `b main`, `b utils.c:50`) |
| `continue` | `c` | 继续运行直到下一个断点 |
| `next` | `n` | 单步执行 (不进入函数) |
| `step` | `s` | 单步执行 (进入函数) |
| `print <var>` | `p` | 打印变量值 |
| `backtrace` | `bt` | 查看函数调用栈 |
| `list` | `l` | 查看源代码 |
| `quit` | `q` | 退出 GDB |
| `info locals` | - | 查看当前栈帧的局部变量 |
| `info breakpoints` | `i b` | 查看所有断点 |

## 4. 调试示例

### 设置断点
```bash
(gdb) b main
Breakpoint 1 at 0x1234: file main.c, line 20.
```

### 查看变量
```bash
(gdb) p server_fd
$1 = 3
```

### 调试多线程 (如果有)
```bash
(gdb) info threads
(gdb) thread <ID>  # 切换线程
```

## 5. 常见问题排查

- **程序崩溃 (Segmentation Fault)**:
  运行程序直到崩溃，然后使用 `bt` 查看崩溃时的调用栈，定位出错的代码行。

- **逻辑错误**:
  在关键函数入口设置断点，使用 `n` 或 `s` 逐步跟踪变量变化。
