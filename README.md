# ImgToTable

轻量级本地桌面工具骨架，首要支持 Windows，同时支持 macOS 14+（Apple Silicon）。采用 C++20、Qt 6 Widgets、CMake 和可选的 ONNX Runtime C++ API；没有 Web、QML、Qt Quick、包管理器或复杂架构依赖。

当前提供文件选择、路径显示、运行按钮、只读结果区和状态栏。**尚未实现图片转表格**：未提供真实模型、文件预处理或结果解析。“运行”只验证文件并提示待实现的处理流程，不会生成假结果。

## 目录

```text
CMakeLists.txt
cmake/CopyModel.cmake         # 构建时复制/移除模型
src/main.cpp                 # 应用入口，持有整个应用周期的推理引擎
ui/MainWindow.h/.cpp          # 简单 Qt Widgets 界面
core/InferenceEngine.h/.cpp   # 无 Qt 依赖的同步推理接口
platform/PlatformUtils.h/.cpp # Unicode 文件路径和运行时模型位置
resources/models/README.md   # 放置真实 model.onnx 的说明
tests/InferenceEngineTests.cpp
README.md
LICENSE                      # 保留仓库现有 GPLv3
.gitignore
```

当前平台差异只涉及文件路径和运行时资源目录，集中在 `platform/` 和 CMake。尚不需要 Windows 专用源文件或 Objective-C++，因此没有空的 `windows/`、`macos/` 目录。以后实际需要原生 API 时，再通过 `if(WIN32)` / `elseif(APPLE)` 添加对应文件。

## 开发环境与依赖

- CMake 3.24 或更高、Ninja、支持 C++20 的编译器。
- Qt 6 桌面开发包：Core、Gui、Widgets。通过 Qt 官方安装器安装与编译器及 CPU 架构匹配的版本；推荐 Qt Creator。
- Windows：Windows 10/11 x64，Visual Studio Build Tools 2022，勾选“使用 C++ 的桌面开发”、MSVC v143 和 Windows SDK。使用匹配 MSVC 的 Qt Kit，不使用 MinGW Kit。
- macOS：macOS 14.0 或更高，仅支持 Apple Silicon（arm64）；Xcode 15+ / 对应 Command Line Tools / Apple Clang（`xcode-select --install`），Qt macOS Kit。
- ONNX Runtime 可暂不安装；未设置 `ONNXRUNTIME_ROOT` 时，界面仍可构建启动，并显示推理不可用的原因。若设置了路径但 SDK 不完整，CMake 会明确报错。

选择 Qt 版本时核对其支持的 Windows/macOS 最低版本及编译器；不要假定所有 Qt 6 版本支持同一组系统版本。参考 [Qt Windows 支持](https://doc.qt.io/qt-6/windows.html) 和 [Qt macOS 支持](https://doc.qt.io/qt-6/macos.html)。

### 使用脚本下载 Qt 和 ONNX Runtime

根目录的 [download_deps.py](download_deps.py) 支持 Windows x64 和 Apple Silicon（不再支持 Intel Mac），需要 **Python 3.12+**。默认下载 Qt **6.8.3** 的 `qtbase`（含 Core/Gui/Widgets）和 ONNX Runtime **1.30.0 CPU C/C++ SDK**。固定版本便于重复配置，并非自动选择最新版本；1.30.0 提供 Windows x64 和 macOS arm64 的包。

Windows（PowerShell，安装 Python 时启用 Python Launcher）：

```powershell
py -3.12 .\download_deps.py
```

macOS：

```sh
python3 download_deps.py
```

下载及解压位置默认是脚本旁的 `third_party/`，已被 Git 忽略。脚本不需要管理员权限，不修改系统 PATH，也不自动执行构建；完成后打印实际路径的 CMake 命令。Windows 编译仍需 MSVC 开发者环境。

```sh
# 只查看路径和构建命令，不联网、不写文件
python3 download_deps.py --dry-run
# 只下载一个 SDK
python3 download_deps.py --only onnx
# 指定目录、版本及 Mac 目标架构
python3 download_deps.py --destination "$HOME/SDKs/imgtotable" \
  --qt-version 6.8.3 --onnx-version 1.30.0 --arch arm64
```

Windows 使用同样参数，将 `python3` 换成 `py -3.12`。macOS 默认选择 arm64，包括 Apple Silicon 上的 Rosetta 环境；`--arch x86_64` 仅适用于 Windows。更换 Qt 版本时需确保仍提供 `win64_msvc2022_64` / `clang_64` Kit。更换 ONNX 版本时需检查该发行版是否提供所需平台包，项目不再支持 Intel Mac。

Qt 下载借助 [aqtinstall](https://aqtinstall.readthedocs.io/en/latest/cli.html)，脚本将其固定为 3.3.0 并安装到 `third_party/.aqt-venv/`，从 Qt 下载仓库获取 SDK，不污染全局 Python。它只是下载辅助工具，不是 C++ 项目的构建依赖。ONNX 从 [微软官方 1.30.0 Release](https://github.com/microsoft/onnxruntime/releases/tag/v1.30.0) 直接下载，通过 HTTPS 传输、临时目录解压及文件布局检查；脚本未固定 ONNX 压缩包的 SHA-256。需要能访问 PyPI、Qt 下载站及 GitHub，网络错误会返回非零退出码，不会关闭 TLS 校验。

重复执行会跳过已成功安装的 SDK。下载失败可重试；ONNX 临时目录会自动清理，Qt 未完成的安装会重新执行。不要在同一目标目录并发运行脚本。脚本不安装 Qt Creator、Python、MSVC、Xcode、CMake、Ninja 或模型文件，相关开发工具按上方说明单独安装。Qt Creator 中可通过下载后的 `bin/qmake.exe`（Windows）或 `bin/qmake`（macOS）注册 Qt 版本并配置 Kit。

如果日志长时间停在 `Downloading qtbase...` / `Redirected: ftp.jaist.ac.jp`，可能是自动分配的镜像速度慢。aqt 不显示逐字节下载进度，这一行本身并不能证明下载已停止。可先按 `Ctrl+C` 停止原来的命令，再指定镜像重试：

```sh
python3 download_deps.py --qt-mirror https://mirrors.aliyun.com/qt
```

该参数只改变 Qt 下载地址，ONNX 仍从 GitHub 下载。保留 aqt 的官方校验和来源与完整性校验；不修改虚拟环境里的 aqt 文件。未完成的 Qt 包会重新下载，不保证断点续传。也可将参数换成其他提供 Qt `online/` 仓库的 HTTPS 镜像根地址，参考 [aqt 镜像配置](https://aqtinstall.readthedocs.io/en/latest/configuration.html)。

`Failed to locate XML data` 不一定表示 Qt 版本不存在，也可能是镜像对 Python 下载器返回了 HTTP 403。在本机排查中，中科大镜像的 curl 请求成功，但 Python 请求被拒绝；应换用上面的镜像，而不是关闭校验或更换 Qt 版本。镜像可用性会随网络环境变化。

### ONNX Runtime SDK

从 [ONNX Runtime 官方 Releases](https://github.com/microsoft/onnxruntime/releases) 下载 **CPU C/C++ SDK 压缩包**并解压。Python 的 `pip install onnxruntime` 不能替代这里需要的 SDK。Windows 选择 win-x64；macOS 选择 osx-arm64 包。SDK、Qt 和构建目标架构必须一致。

放置位置不限，例如 Windows 的 `C:/SDKs/onnxruntime` 或 macOS 的 `$HOME/SDKs/onnxruntime`；也可放入已被 Git 忽略的 `third_party/onnxruntime/`。`ONNXRUNTIME_ROOT` 应指向包含 `include` 与 `lib` 的目录，而不是它们的父目录：

```text
onnxruntime/
  include/onnxruntime_cxx_api.h
  include/onnxruntime_c_api.h
  ...（保留完整 include 目录）
  lib/onnxruntime.lib            # Windows 导入库
  lib/onnxruntime.dll            # Windows 运行库，也支持放在 bin/
  lib/libonnxruntime*.dylib       # macOS 动态库及其版本别名
```

不要混用不同 SDK 版本的头文件与库。使用支持 `GetInputNameAllocated` 的现代 SDK（1.13+）；实际集成时建议固定一个经过测试的发行版本。

## Windows 构建

在 **x64 Native Tools Command Prompt for VS 2022** 中运行。把示例 Qt 和 SDK 路径替换为实际安装目录：

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.x.y/msvc2022_64" -DONNXRUNTIME_ROOT="C:/SDKs/onnxruntime"
cmake --build build
ctest --test-dir build --output-on-failure
set "PATH=C:\Qt\6.x.y\msvc2022_64\bin;%PATH%"
build\imgtotable.exe
```

构建时会把 `onnxruntime.dll` 复制到程序及测试可执行文件旁。Qt Creator 的 Kit 会提供 Qt 运行环境；命令行直接运行需要 Qt 的 `bin` 在 PATH 中。若提示缺少 Qt 平台插件，可在开发构建上执行：

```bat
C:\Qt\6.x.y\msvc2022_64\bin\windeployqt.exe --release build\imgtotable.exe
```

只构建界面时省略 `-DONNXRUNTIME_ROOT=...`；已配置过 SDK 的构建目录需显式传 `-DONNXRUNTIME_ROOT=""` 清空缓存。

## macOS 构建

以下适用于 Qt 官方安装器的目录布局；替换版本号和 SDK 路径：

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.x.y/macos" \
  -DONNXRUNTIME_ROOT="$HOME/SDKs/onnxruntime"
cmake --build build
ctest --test-dir build --output-on-failure
open build/imgtotable.app
# 需要观察 qInfo/qWarning 日志时，从终端直接启动：
./build/imgtotable.app/Contents/MacOS/imgtotable
```

CMake 默认使用 `arm64` 和最低 macOS `14.0`，并拒绝 x86_64、Universal 或低于 14.0 的部署目标。Qt/SDK 必须包含 arm64。旧构建目录若缓存了 Intel 架构，请改用新目录。

ONNX Runtime dylib 在构建后复制到 `.app/Contents/Frameworks`，并配置 bundle 相对 RPATH。开发构建仍可能引用本机 SDK/Qt 路径；这不是可分发的完整应用包。

## Qt Creator

打开根目录 `CMakeLists.txt`，选择 MSVC x64 或 Apple Clang 的 Qt 6 Desktop Kit。使用 Ninja，在项目的 CMake 配置中设置 `ONNXRUNTIME_ROOT`，然后构建并运行 `imgtotable`。Qt Creator Kit 通常会自动提供 Qt 路径；若查找失败，设置 `CMAKE_PREFIX_PATH` 为该 Kit 的 Qt 安装前缀。

修改 SDK 根目录、架构或编译器时推荐使用新构建目录。可通过 `-DBUILD_TESTING=OFF` 关闭测试 target。

## 模型与推理

`resources/models/` 已保存官方 PP-OCRv6 small 检测、识别模型及 SLANet_plus 表格结构模型，均为 ONNX 格式，附带原始 `inference.yml`。文件版本、来源和校验值见 [模型说明](resources/models/README.md)。当前应用尚未串联这三个模型；下面的 `model.onnx` 步骤仅适用于现有的单模型推理骨架。

1. 将真实模型放入 `resources/models/model.onnx`。仓库不提供无效的占位二进制模型。
2. 重新构建。Windows 复制到可执行文件旁的 `models/model.onnx`；macOS 复制到 `.app/Contents/Resources/models/model.onnx`。路径相对于可执行文件，独立于工作目录，支持中文路径。
3. 重启应用加载新模型。模型只在启动时加载一次；点击“运行”不会重新加载。若删除源模型，下次构建也会清理旧的运行时模型副本。

当前复制规则针对单文件模型；带 external data 的模型需要同时增加对应数据文件的复制规则。模型默认被 Git 忽略，确认分发许可后可用 `git add -f resources/models/model.onnx` 纳入版本控制。

`InferenceEngine` 通过 RAII 管理 `Ort::Env` 和 `Ort::Session`，销毁顺序保证 Session 先于 Env 释放。ONNX 类型封装在实现文件内。接口为：

- `loadModel(path)`：返回成功/失败，失败后处于未加载状态。
- `isModelLoaded()` / `lastError()`：查询状态与具体诊断。
- `run(values, shape, output)`：实际 CPU 推理，当前只支持单个 float32 输入和单个 float32 输出。支持具体形状，包括标量的空 shape；本示例拒绝零长度/负数维度，并检查元素数量与溢出。动态模型维度由调用者提供实际尺寸。其他模型形状约束由 ONNX Runtime 验证。

所有 ONNX Runtime 操作的异常均在核心中捕获并转为错误信息。失败时清空输出，不留下上一次的结果；运行失败保留已加载的模型。UI 显示初始化错误，同时使用 `qWarning()` 记录路径和具体原因；未来连接推理调用时同样应展示并记录 `lastError()`。

目前没有已知模型的输入规范，所以 UI 不调用真实推理。**接入真实推理前必须把预处理及 `run()` 移到单个工作线程**，例如一个 QThread，通过排队信号回传结果，禁用重复提交，并在退出时等待线程结束。当前引擎不是线程安全的，不要并发调用或在推理时重新加载。模型初始化目前在显示窗口前同步执行；如果测得初始化明显耗时，也一起移到该工作线程。不需要先引入 Qt Concurrent 或线程池。

## 测试

不需要 Qt 或 SDK 即可验证核心的未加载状态、缺失模型、错误信息和失败时的输出清理：

```sh
cmake -S . -B build-core -G Ninja -DIMGTOTABLE_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

测试不依赖 `assert`，Release 配置也会执行检查。配置 SDK 后，这些测试还会走启用 ONNX 的缺失文件路径。若有一个 float32、形状 `[1,3]` 的单输入单输出 Identity ONNX 模型，可设置 `-DIMGTOTABLE_TEST_MODEL=/path/to/identity.onnx`，增加真实加载、数值推理、无效输入及失败恢复测试。没有模型时不会假装完成真实推理测试。

## 后续开发

最值得优先实现的是确定真实模型的输入/输出规范，再实现图片预处理、单工作线程推理和结构化结果展示。根据这个闭环的实际需要逐步补充多输入/输出支持及核心测试。

CPU 是默认且唯一启用的 Execution Provider。后续 CUDA / DirectML 集成只需在 `InferenceEngine::loadModel()` 创建 Session 前配置 SessionOptions，并显式链接对应 SDK/运行库；需要针对所选 Provider 调整会话选项、依赖复制和平台限制。参考 [官方 Execution Providers](https://onnxruntime.ai/docs/execution-providers/)，不要先做 GPU 自动检测或额外抽象。

发布阶段再使用 Windows `windeployqt` 或 macOS `macdeployqt` 收集 Qt 依赖并检查 ONNX 运行库；之后可制作 EXE/MSIX、Microsoft Store 包或 `.app`/dmg，并完成签名、公证。目前未实现安装器、发布脚本或完整可移植部署。Qt CMake 接口参考 [qt_add_executable](https://doc.qt.io/qt-6/qt-add-executable.html)，推理接口参考 [Ort::Session](https://onnxruntime.ai/docs/api/c/struct_ort_1_1_session.html)。

## 许可证

按仓库维护者要求保留已有 [GNU GPLv3 LICENSE](LICENSE)。第三方 Qt、ONNX Runtime 和模型的许可证分别适用；发布时保留相关许可证与通知。
