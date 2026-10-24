# CrossStore 内置商店图标

来源：`CrossStore/CrossStore` 仓库 `appstore/`，母版与全部灰阶预览由该仓库维护。此目录只保存独立构建固件需要的 CSI1 快照。

更新：在 CrossStore 运行 `python3 scripts/prepare_icons.py --builtin appstore`，复制生成的 `appstore/assets/icon.csi` 到此目录，然后运行 `python3 scripts/generate_store_icon.py`。PlatformIO 自动运行生成器；不要编辑或提交生成的头文件。

CSI1：8 字节头，依次为 g2-32、g2-64、g4-32、g4-64、g16-32、g16-96 的行优先 MSB 位图。总计 7048 字节常量，无图标像素堆分配。使用 `PackageIcons` 绘制，保留 Mono 4 灰阶位面和 PaperS3 原生 16 灰阶。
