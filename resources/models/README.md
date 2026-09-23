# 模型文件

已下载 PaddlePaddle 官方发布的 ONNX 模型（`small` 档）：

| 用途 | 目录 | 来源版本 | ONNX SHA-256 |
| --- | --- | --- | --- |
| PP-OCRv6 文字检测 | `PP-OCRv6_small_det/` | [28fe589](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx/tree/28fe5895c24fd108c19eb3e8479f4ab385fbfc62) | `d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e` |
| PP-OCRv6 文字识别 | `PP-OCRv6_small_rec/` | [b8f84f0](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx/tree/b8f84f0b80c529de40b4fbb3544b84fa7233a513) | `5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634` |
| SLANet_plus 表格结构识别 | `SLANet_plus/` | [7dbe640](https://huggingface.co/PaddlePaddle/SLANet_plus_onnx/tree/7dbe640e127602bf506815e822c09758de73c482) | `7790c0c13ce064782c9d22ebeb16b4da8216f83d3ba576da962c106ef58386da` |

每个目录含 `inference.onnx` 和对应的 `inference.yml`。模型来源页面标注 Apache-2.0 许可。权重文件已被 Git 忽略，不会随代码提交；如要分发，请同时保留许可证声明。

当前应用仍只会尝试加载根目录下的 `model.onnx`，尚未实现三模型 OCR 与表格结构流程。这里保存模型供下一步接入使用，不能仅靠复制模型完成图片转表格。
