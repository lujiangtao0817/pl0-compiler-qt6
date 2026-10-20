# PL/0 可视化实验室：GUI 使用与答辩说明

## 1. 作品定位

本界面不是独立的演示动画，而是直接连接 `pl0` 编译核心和 P-Code 虚拟机的教学调试器。

- Token、符号表和 P-Code 均来自同一次真实编译结果；
- 可在 Baseline 教材基线与 Extended 扩展模式间切换；
- 语法轨迹来自递归下降分析器的真实进入、消费与退出事件；
- 每次“单步”只执行一条真实 P-Code 指令；
- PC、BP、SP、运行栈、活动记录和程序输出均来自 VM 状态快照；
- 源码修改后，旧编译结果立即失效；
- 编译失败时不能运行旧 P-Code。

## 2. 环境要求

- Windows 10/11；
- Qt 官方 MinGW 13.1，或 Visual Studio 2022 及更新版本；
- CMake 3.20 或更新版本；
- Qt 6.2 或更新版本，组件至少包含 `Qt6::Widgets`。

建议全组固定同一个 Qt 6 版本和同一套编译工具链，禁止混用 MinGW 与 MSVC 产物。

## 3. Qt6 构建

当前正式版使用 Qt 6.12 MinGW 64-bit。在 PowerShell 中进入项目目录，然后执行：

```powershell
$env:Path="C:/Qt/Tools/mingw1310_64/bin;C:/Qt/Tools/Ninja;$env:Path"
C:/Qt/Tools/CMake_64/bin/cmake.exe -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/mingw_64"
C:/Qt/Tools/CMake_64/bin/cmake.exe --build build
```

如果已安装 Qt 的 MSVC 套件，也可使用 Visual Studio 生成器：

```powershell
cmake -S . -B build -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release
```

请把示例路径替换为本机实际 Qt6 安装目录。生成的程序为 `pl0-gui.exe`。

仅在没有 Qt6、需要做源码兼容检查时，可临时配置：

```powershell
cmake -S . -B build-compat -DPL0_QT5_COMPAT=ON
```

正式提交和答辩版本必须使用默认的 Qt6 构建。

## 4. 界面功能

### 源码编辑

- 行号、当前行高亮和 PL/0 基础语法高亮；
- 新建、打开、保存和另存为；
- 内置表达式、循环、过程调用三个教学示例；
- 点击 Token、符号声明、P-Code、语法轨迹或诊断可跳转到对应源码范围。

### 编译分析

- 词法单元：词素、种类、值与源码位置；
- 符号表：名称、种类、类型、层级、地址和大小；
- P-Code：地址、操作码、层差和参数/OPR 子操作；
- 语法轨迹：树形展示递归下降规则、Token 消费和源码位置；
- 诊断：级别、阶段、错误代码、位置和消息。
- 教学证据导出：将源码、Token、符号表、P-Code、诊断、VM 快照、运行栈和程序输出生成 Markdown 报告。

### 运行调试

- 运行：以可观察的节奏连续执行 P-Code；
- 暂停：保持当前 VM 状态；
- 单步：恰好执行一条 P-Code；
- 断点：双击 P-Code 任意一行设置或取消；连续运行会在该指令执行前暂停；
- 重置：恢复到当前编译结果的初始状态；
- 停止：终止本轮连续运行；
- 输入：VM 遇到 `READ` 时进入等待输入状态，不阻塞 GUI；
- 当前指令使用蓝色高亮，本步变化的栈单元使用橙色高亮。
- 变量监视：按符号名显示所属层级、绝对栈地址和当前值；数组会以内联列表展示；
- 活动记录会把入口地址解析为真实过程名，便于观察嵌套调用和递归。

寄存器区域中的 SP 表示“下一空闲栈单元”，对应 VM 的 `stack_top`。活动记录展示 BP、SL、DL 和 RA；递归调用时同一过程可以同时产生多个帧。

## 5. 快捷键

| 操作 | 快捷键 |
|---|---|
| 新建 | Ctrl+N |
| 打开 | Ctrl+O |
| 保存 | Ctrl+S |
| 另存为 | Ctrl+Shift+S |
| 编译 | Ctrl+B |
| 运行 | F5；笔记本功能键被系统占用时使用 Ctrl+R 或点击工具栏“运行” |
| 暂停 | F6 |
| 单步一条 P-Code | F10 |
| 重置 | Ctrl+F5 |
| 导出教学证据 | Ctrl+Shift+E |

## 6. 推荐答辩流程

1. 选择“表达式与优先级”，编译后展示 Token、符号表、P-Code 源码映射和语法轨迹；
2. 双击乘法前的一条 P-Code 设置断点，按 F5 展示执行前暂停，再按 F10 观察栈变化；
3. 选择“过程调用与非局部变量”，在 `CAL` 前设置断点；
4. 单步执行 `CAL`，展示新活动记录及 SL、DL、RA；
5. 说明非局部变量通过静态链定位，过程返回依赖动态链和返回地址；
6. 将“循环与复合赋值”切换至 Baseline 编译，展示扩展语法的精确诊断，再切回 Extended；
7. 选择带 `read` 的程序，展示非阻塞输入状态；
8. 使用“文件 → 导出教学证据”，展示源码、语法轨迹、断点和 VM 快照均可追溯。

## 7. 自动验证

核心测试：

```powershell
ctest --test-dir build --output-on-failure
```

界面提供一个用于发布前视觉检查的截图参数：

```powershell
pl0-gui --compile-demo --step-demo --screenshot preview.png
```

该命令会载入内置示例、编译、执行八条 P-Code、保存窗口截图并退出。

## 8. Windows 发布

使用 Release 构建后执行：

```powershell
windeployqt --release build/pl0-gui.exe
```

发布前应在没有 Qt Creator 的电脑上验证：程序可离线启动、中文路径可用、125%/150% 缩放正常、示例编译运行正常。
