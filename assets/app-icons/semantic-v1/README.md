# CrossMux 应用图标 · Semantic v1

18 个应用各有独立彩色母版，再从母版生成目标尺寸和灰阶版本。母版由
内置 `image_gen` 生成，提示词保存在 `prompts.json`；应用语义、稳定文件名和
`UIIcon` 映射保存在 `catalog.json`。不依赖旧图标放大，也不从低阶版本反推高阶版本。

## 文件与预览

- `masters/`：原始高分辨率 RGBA 彩色图，保留生成结果和透明通道。
- `color/{32,48,64,96,128}/`：按透明内容边界统一留白的 RGBA PNG。
- `g2/`、`g4/`、`g16/`：2、4、16 **级**灰阶（分别是 1、2、4 bits/pixel）。
- `g*/<尺寸>/plain/`：直接量化版本，五档尺寸均提供。
- `g{2,4}/<64|96|128>/dither/`：中间调抖动版本。32/48 px 以清晰轮廓为先。
- 每个灰阶目录含每个应用的 PNG、同名 `.bin`，以及该规格整套 `icons.h`。
- `manifest.json`：母版哈希、实际尺寸、各变体路径、灰阶、stride、数据长度和哈希。
- `previews/color-contact-sheet.png`：全套彩色总览。
- `previews/comparison-{32,48,64,96,128}.png`：实际像素尺寸的灰阶对照。

PNG 使用固定灰阶调色板，实际像素只包含指定级数。它适合预览、解码器和
资源管线；`.bin` 是无头紧凑像素数组，可直接使用旁边的 `icons.h` 嵌入固件。
灰阶文件已合成到纯白背景，不再保留半透明像素；这避免设备端二次混色生成额外灰阶。

## 选择规格

| 使用场景 | 起始选择 | 说明 |
| --- | --- | --- |
| 小尺寸列表 | 32 或 48 px / plain | 按实际绘制尺寸选文件，避免再次放大 |
| 仅黑白的 64 px 网格 | g2 / 64 / dither | 保留中间调；强光或强调轮廓时比较 plain |
| 支持 4 级的 64 px 网格 | g4 / 64 / plain | 首批视觉检查中比全面抖动更干净 |
| 96/128 px、2/4 级显示 | 对比 plain 与 dither | 抖动会增加纹理，实际效果受像素密度和刷新波形影响 |
| 已接通原生 16 级的显示链路 | g16 / 对应尺寸 / plain | 保留平滑明暗，不额外抖动 |
| 彩色屏幕或网页 | color / 对应尺寸 | 透明背景；按需要合成到主题背景 |

固件中的 `SemanticAppIcons` 已接入应用网格和列表。PaperS3 的 LGFX 原生通路
使用 g16/32/plain 列表和 g16/96/plain 网格；空间不足时缩小网格。四阶设备
使用 g4/32/plain 和 g4/64/dither，通过双遮罩分带刷新。黑白回退和夜间模式
使用 g2/32/plain 与 g2/64/dither。其他页面的系统图标暂沿用原有设计。
原生 16 阶提交复用 LGFX 的 PSRAM 画布，没有增加全屏缓冲。

## 原始位图约定

格式为不压缩、无文件头、逐行排列、从上到下、行内从左到右、每字节高位优先：

- 2 级：0 黑，1 白；每字节 8 像素。
- 4 级：0/1/2/3 对应 0/85/170/255；每字节 4 像素。
- 16 级：0..15 对应亮度 0,17,...,255；每字节 2 像素。
- `stride = ceil(width × bitsPerPixel / 8)`；行尾补位为白。
- 正向坐标、未预旋转、白色不透明背景；反色可按 `max-index` 处理。

| 尺寸 | 2 级 / 图 | 4 级 / 图 | 16 级 / 图 |
| --- | ---: | ---: | ---: |
| 32×32 | 128 B | 256 B | 512 B |
| 48×48 | 288 B | 576 B | 1,152 B |
| 64×64 | 512 B | 1,024 B | 2,048 B |
| 96×96 | 1,152 B | 2,304 B | 4,608 B |
| 128×128 | 2,048 B | 4,096 B | 8,192 B |

例如嵌入一档素材：

```cpp
#include "assets/app-icons/semantic-v1/g2/64/dither/icons.h"
namespace icons = crossmux_icons_g2_64_dither;
// icons::library, icons::transfer, ... 是 const 像素数组。
// icons::width / height / stride / bitsPerPixel 给出布局。
// 交给使用相同位序和黑白极性的 HAL/renderer 接口；不要绕过显示 HAL。
```

`g4` 的 2bpp 数组是**绝对灰阶值**，不能直接当作现有 LSB/MSB overlay masks。
适配 overlay 时需按该接口的灰阶编码转换；`g16` 也需原生 4bpp 绘制接口。
只包含需要的规格，并检查最终链接产物；不要把全部分辨率都编进固件。

## 可重复生成

```sh
python3 -m venv .cache/icon-assets-venv
.cache/icon-assets-venv/bin/python -m pip install -r scripts/requirements-app-icons.txt
.cache/icon-assets-venv/bin/python scripts/prepare_app_icons.py
.cache/icon-assets-venv/bin/python scripts/verify_app_icons.py
```

转换顺序：按 alpha≥16 的边界裁切 → 等比缩放到目标边长的 87.5% → 居中留白 →
合成白底 → Rec.709 权重的 sRGB 亮度映射 → 固定轻度对比度/锐化 → 量化或抖动 →
打包。始终先缩放、最后量化，避免缩放后的像素再次混出未声明的灰阶。

纯黑白 `plain` 版本做尺寸专用阈值处理：32/48 px 使用 166，64 px 及以上使用
146，以补偿缩小后深色轮廓与白底混合造成的断线；4/16 级仍按最近灰阶量化。

抖动为蛇形 Floyd–Steinberg，误差传播系数 0.9；锁定较亮的纸面、较深的轮廓及
接近可表达灰阶的像素，仅在中间调扩散。它是设计取舍，不是屏幕反射率校准。
所有设备版本都能从同一母版再生成；实机辨识度、残影和刷新效果需在目标设备上验收。

图像生成无法保证重新调用得到相同像素，因此保留母版及其哈希；从这些固定母版
生成 PNG / BIN / C++ 数据是确定的。预览图的文字字体可能随主机字体改变。

## 本次验证

- 18 份 1254×1254 RGBA 母版，90 份目标尺寸彩色 PNG。
- 378 个灰阶变体：实际调色板、PNG 位深、尺寸、白色边界及哈希均通过检查。
- 每个变体的 PNG、BIN 和 C++ 数组逐像素一致；1/2/4 bpp 已用已知字节验证位序及行尾补位。
- 21 份规格头文件同时通过 C++17 `-Wall -Wextra -Werror -fsyntax-only` 检查。
- 已人工查看全套彩色总览、32 px 黑白放大图和 64 px 各灰阶对照。
- 已接入设备菜单；`scripts/tests/test_semantic_app_icons.py` 的 14 组实际绘制与 PNG
  对比通过，覆盖黑白/四阶/16 阶、不同尺寸、反色和分带。
- 2026-10-19，PaperS3 用户确认新图标和灰阶显示正常；其他目标设备的显示、
  长时间刷新和残影验收仍待进行。编译或 B/W 截屏不能代替光学灰阶验收。
