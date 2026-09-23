# ImgToTable

本地截图、图片编辑、文字识别与表格识别工具。使用 C++20、Qt 6 Widgets、ONNX Runtime CPU。目标平台是 Windows 10/11 x64（MSVC 2022）和 macOS 14+ Apple Silicon。识别在本机运行，不上传截图。

## 使用

启动后点击“开始截图”，或按全局快捷键 `Ctrl+Shift+2`。拖动选择区域后，点“截图”进入编辑器，或点“截图并识别表格”直接启动表格识别。按 Esc 取消；关闭主窗口后程序留在系统托盘或菜单栏，可在那里修改快捷键和退出。也可以点击“打开图片”导入 PNG、JPEG、BMP。

编辑器支持裁剪、左右旋转、撤销、重做、复制图片和保存 PNG。点击“识别文字”得到可编辑文字；点击“识别表格”得到可校正单元格，可复制 CSV 到剪贴板或导出 CSV 文件。CSV 将合并单元格的内容放在左上角，其余位置留空。一次识别一个表格；图片包含多个表格时，先裁剪到目标表格。编辑图片后，按需重新识别。

macOS 首次截图需要允许“屏幕录制”权限；授予后如果仍无法截图，请重启应用。默认开发构建使用临时签名，每次代码或打包资源变化后签名身份也会变化，macOS 可能要求重新授权。要在持续开发中保留权限，请用 `IMGTOTABLE_CODESIGN_IDENTITY` 指定稳定的代码签名证书（见下方构建命令）；正式发布仍需使用开发者证书签名。快捷键被占用时会提示冲突。

## 依赖与模型

需要 CMake 3.24+、Ninja、Qt 6 Core/Gui/Widgets 和 ONNX Runtime C/C++ SDK。`download_deps.py` 可下载固定版本的 Qt 6.8.3 和 ONNX Runtime 1.30.0；运行 `python3 download_deps.py --dry-run` 可查看路径和构建命令。Windows 需 MSVC 2022，macOS 仅构建 arm64。

`resources/models/` 中的 PP-OCRv6 small 检测、识别模型及 SLANet_plus 表格结构模型分别运行在三个 ONNX 会话中。其来源、版本、校验值和许可见 [模型说明](resources/models/README.md)。构建时将每个目录的 `inference.onnx` 和 `inference.yml` 复制到程序旁的 `models/`（Windows）或 app 包内的 `Contents/Resources/models/`（macOS）；模型路径不依赖当前工作目录。模型权重与 SDK 不提交到 Git。未配置 ONNX Runtime 时仍可构建界面，但识别会显示不可用的原因。

推理在一个后台任务中顺序调用三个模型，ONNX Runtime 会话共享线程池，并按 CPU 核心情况在算子内部并行。运行期间不接受重复识别请求。

## 构建与测试

macOS：

```sh
cmake -S . -B build-app -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/third_party/Qt/6.8.3/macos" \
  -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime-osx-arm64-1.30.0"
cmake --build build-app
ctest --test-dir build-app --output-on-failure
open build-app/imgtotable.app
```

若钥匙串中已有代码签名证书，可先运行 `security find-identity -v -p codesigning` 查看名称或 SHA-1，再在配置命令末尾加上 `-DIMGTOTABLE_CODESIGN_IDENTITY="Apple Development: ..."`（也可填证书 SHA-1）。首次使用该签名构建时需重新授予一次录屏权限，此后代码更新应沿用同一授权。若该命令显示 `0 valid identities found`，仍可用默认临时签名开发，但修改程序后可能需要重新授权。

Windows 在 x64 Native Tools Command Prompt 中配置对应的 Qt MSVC Kit 和 ONNX Runtime win-x64 SDK：

```bat
cmake -S . -B build-app -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" -DONNXRUNTIME_ROOT="C:/SDKs/onnxruntime"
cmake --build build-app
ctest --test-dir build-app --output-on-failure
```

只运行无 Qt 的核心测试：

```sh
cmake -S . -B build-core -G Ninja -DIMGTOTABLE_BUILD_APP=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

配置 SDK 且本地有三个模型权重时，`recognizer_core` 会用真实模型和仓库内的 2×2 表格图片测试文字识别、表格结构、单元格匹配和 CSV。没有模型权重时只运行接口和错误处理测试。Windows 发布时还需用 `windeployqt` 收集 Qt 库；macOS 发布需要 `macdeployqt`、签名与公证。当前构建产物是开发版本。

## 许可证

本项目保留 [GPLv3](LICENSE)。Qt、ONNX Runtime 和模型另有各自的许可；分发时应附带相应声明。
