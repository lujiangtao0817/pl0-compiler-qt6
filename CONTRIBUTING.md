# 小组协作与提交约定

## 本地仓库

本仓库保存 PL/0 编译核心、Qt 6 Widgets 界面、测试和使用文档。
已有开发目录的成员应先确认改动是否已经同步，避免用旧文件覆盖新版本。
第一次加入时运行：

```powershell
git clone https://github.com/lujiangtao0817/pl0-compiler-qt6.git
cd pl0-compiler-qt6
```

## 每次迭代

1. 在干净的 main 分支执行 `git pull --ff-only`。
2. 用 `git switch -c feat/任务名称` 创建任务分支，修复可用 `fix/问题名称`。
3. 完成一项可说明的改动，运行相关测试并检查 `git diff`。
4. 用 `git add 文件路径` 选择文件，再运行 `git diff --cached` 确认提交内容。
5. 用 `git commit -m "fix: 描述具体修复"` 提交，执行 `git push -u origin 分支名称`。
6. 在 GitHub 创建 Pull Request，写明改动内容、验证结果和相关截图，由另一位成员检查后合入 main。

已有未提交改动时，先检查 `git status` 并妥善保存，再切换分支或拉取。
遇到冲突时共同核对内容，不要强制推送覆盖他人的提交。

## 提交内容

每次提交对应实际完成的工作，例如修复运行时定位、补充数组边界用例、完善界面交互或修正文档。
提交时间保留实际时间。不要添加空提交，也不要把同一改动反复撤销、恢复以增加记录。
构建目录、可执行文件、Qt 运行库、缓存和本机配置由 `.gitignore` 排除；发布包另行管理。

## Windows 构建和验证

使用 Qt 官方匹配的 MinGW 13.1 工具链，保证 MinGW 和 Ninja 已加入当前终端 PATH。
推荐把仓库克隆到纯英文路径，例如 `C:\Projects\pl0-compiler-qt6`，避免 moc 的中文路径兼容问题。
Qt 安装位置不同的成员应调整下列路径。

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.12.0\mingw_64\bin;" + $env:PATH
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPL0_ENABLE_ASAN=OFF -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64 -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build
& "C:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir build --output-on-failure
python tools/run_samples.py --build-dir build --runtime-dir C:/Qt/Tools/mingw1310_64/bin
```

核心测试由CTest的 `pl0` 聚合目标运行；其中实际用例数量以测试程序输出为准。
同时开启GUI与测试时，还会注册 `parse_replay` 界面回放回归测试，使用Qt离屏平台运行。
GUI 改动还应实际打开程序，核对操作与截图，命令行测试不能代替界面验收。
