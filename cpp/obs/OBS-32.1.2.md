# OBS 32.1.2 上的独立 MoQ 插件构建

这份说明只覆盖本 fork 在 **macOS arm64、OBS 32.1.2** 上重编 `obs-moq`。它不安装插件，不升级 OBS，不改旧 SRT、panel、MediaMTX、端口或 LaunchAgent。

默认的 `cpp/obs/buildspec.json` 仍然指向 OBS **32.2.2** 和 obs-deps **2026-07-15**。`just obs setup` 会下载那一套，链接出的插件要求 FFmpeg 62 / Qt 6.11.1，不能装进现有 OBS 32.1.2。不要改那份默认 spec 来迁就这台机器，也不要做 `libavcodec.62` 软链接或替换系统库。

## 已核对的依赖

| 组件 | 锁定值 | 依据 |
|---|---|---|
| OBS 源码 | 32.1.2 | 本机目标版本。源码包 SHA256 `b4a59410cddb46d0e31df1ee13b8ec66f30862d7e980c1a8c4e3b5d16fae6053`（本环境下载 `32.1.2.tar.gz` 后计算） |
| obs-deps / Qt6 | 2025-08-23 | OBS 32.1.2 的 `CMakePresets.json` 写的就是这个日期 |
| FFmpeg | libavcodec 61.19.101，libavutil 59.39.100 | 已从 `macos-deps-2025-08-23-arm64.tar.xz` 列出文件名。该包 SHA256 `09cbeb3cbf4fbb9e58e4cf4ca030a724fb1be41d2d7851c8b356f476e04544db` |
| Qt | 6.8.3 | 同一发布的 arm64 Qt 包里 `qconfig.h` 的 `QT_VERSION_STR`。包 SHA256 `ed70bb04a5ffc4c820d691e7f1d9913462bc4e018383e98c5548876f6dec0304` |
| 插件实际下载的 universal 包 | prebuilt `9403bb43…041c42`，Qt `990f1163…ffca29` | 取自 OBS 32.1.2 `CMakePresets.json` 的 `macos-universal`。本环境核过 arm64 包的库版本，没有在 Linux 上解 universal Mach-O |

官方预编译 `obs-moq 0.6.26` arm64 包链接 libavcodec compatibility **62.0.0**、Qt **6.11.1**。默认 spec 下载的 obs-deps **2026-07-15** arm64 包里文件名是 `libavcodec.62.28.102.dylib`、`libavutil.60.26.102.dylib`，Qt `qconfig.h` 是 **6.11.1**。静态不匹配已经足够拒绝安装。本环境没有加载插件，也没有在 Mac 上编译。

## 构建

在 macOS arm64、已安装完整 Xcode 和 Rust 1.98.1 的机器上：

```bash
sh/obs/macos-obs32.1.2.sh
```

脚本会：

1. 核对 `buildspec.obs32.1.2.json` 的版本和哈希形状。
2. 读 `/Applications/OBS.app`（可用 `OBS_APP` 改路径）的 `CFBundleShortVersionString`，不是 32.1.2 就停止。
3. 用 `otool -arch arm64 -L` 核对 OBS 主程序上的 libavcodec 61.19.101 / libavutil 59.39.100 / QtCore 6.8.3。
4. 若 `/opt/homebrew/lib/libavcodec.62.dylib` 或 `/usr/local/lib/libavcodec.62.dylib` 是软链接，拒绝继续。
5. 用 `OBS_MOQ_BUILDSPEC=buildspec.obs32.1.2.json` 配置 Xcode 工程，安装前缀是 `cpp/obs/build_macos_obs32.1.2/dist`，不是 OBS 的插件目录。

非 macOS arm64 上脚本退出码 2，并先做 spec 检查。那不是构建失败，是这台机器不能编译 Mach-O。

## 运行前检查

这些检查都在**获准的维护窗口**里做。本文件不授权现在就做。

- OBS 未在直播或录制，直播伴侣也未在播。
- 旧 SRT 源、`srt-relay`、旧 MediaMTX、panel、fxfiles、LaunchAgent 的路径和端口保持原样。新插件不监听 10000、18880、18889、18189、18190、18896。
- 证书目录和日志目录分开。叶证书不要带 CA 用途。不要打开 insecure，不要关证书校验。
- 高级设置里把协议钉在 `moq-transport-22`，并关闭 WebSocket fallback。连上之后看日志里的协商版本，握手成功不等于已经出画。
- 广播名使用 `.hang`。不要把 moq-lite 当成和手机的 IETF 互通。
- 插件只复制到新的 OBS 配置做隔离加载。先不要放进正在使用的那份 OBS。

## 撤销

本次仓库改动没有安装任何东西。若以后把构建产物拷进 OBS，只删除那一个插件包，例如：

`~/Library/Application Support/obs-studio/plugins/obs-moq.plugin`

同时删掉构建目录 `cpp/obs/build_macos_obs32.1.2/`。不要删除 OBS.app，不要改场景、配置档、SRT 源、旧 relay、旧 MediaMTX、panel、手机 APK 或 LaunchAgent。插件若已加载，卸载需要先退出 OBS；那一步必须另批，不能用脚本结束 OBS 进程。

## 协议锁

本树 `rs/moq-net` 实际会协商的 IETF 草案是 **moq-transport 18 / 20 / 22**，默认偏好里最新的 IETF 是 **22**。draft 23 没有实现，不能为了“最新”去要求它。moq-lite（`moq-lite-01` 到 `moq-lite-06`，以及需要显式打开的 `moq-lite-07-wip`）是另一套协议。手机、relay、OBS 插件必须显式使用同一 IETF 草案。

媒体目录锁定 **hang**（广播名以 `.hang` 结尾，catalog track 为 `catalog.json`）。封装是 CMAF：H.264 为 **avc1**（样本是长度前缀，SPS/PPS 在 avcC），音频在已做的环回里是 AAC-LC。Opus 是手机目标，但附件环回没有测 Opus，不能把 AAC 结果说成 Opus 已通。

MSF（`.msf`，draft-ietf-moq-msf-01，外加 CMAF packaging）在 **moq-cli 0.14.17**（commit `49bde3ea0bad9df966a89a93136f788252ca0ae6`）上会失败：当时的 MSF 消费者把 CMAF 的 `description` 留空，fMP4 导出把 avc1 样本交给 Annex B 解析器。长度在 512 到 65535 且第三字节不是 0 或 1 的 NAL 就会报 `h264: annexb: invalid Annex B start code`。当前 main 会从 init 里抽出 avcC，回归测试锁住这一点。附件那次 `.hang` 环回的 payload 哈希一致，仍然是当时 CLI 的证据，不是这次重新跑出来的。

## 手机库

不要依赖已发布的 `dev.moq:moq:0.5.3`。它的 POM 把 `dev.moq:moq-ffi` 写成 `[0.4.3,0.5)`。Maven 上 moq-ffi 最新是 **0.4.27**（2026-11-16）。0.5.3 的 sources jar 是扁平的 `dev.moq.*` 别名，没有本树的 `dev.moq.media` / `dev.moq.video` 分包。

本树 `kt/gradle.properties` 写着 `moq.version=0.5.1`、`moqffi.version=0.0.0-dev`。`rs/moq-ffi/Cargo.toml` 的 version 字段仍是 0.4.12，那是 main 上尚未按发布流程改写的 crate 版本，不等于 Maven 上的 0.4.12 产物。手机、relay、OBS 插件必须用**同一次提交**编出来的 `kt/` 和 `rs/moq-ffi`。

已有 H.264 硬编帧走 `dev.moq.media` 的 `MoqMediaFrame` / track 或 container producer。`dev.moq.video.Producer` 吃的是 I420/RGBA，会在 FFI 里再编码，不能用来复用现成硬编帧。音频只采集一次 PCM，再按当前协议编码。不要把 AAC 解成 PCM 再编 Opus，也不要为 MoQ 再开一路相机或麦克风。

## 直连 listener 和同机 relay

官方 CLI 可以在同一进程里监听并发布，附件在 127.0.0.1:19443 上做过这件事。这不能推出 OBS 插件或 Android 已经能当 listener。

少一跳也不等于更低延迟。直连仍然要做对象排序、证书、重连和迟帧丢弃；插件若在 OBS 进程内监听，崩溃会连带正在看的旧 SRT 源。同机独立 relay 多一次本机 UDP 转发，但它不解析媒体，崩溃也不该带走 OBS。两部手机加 OBS 回传共享一个 relay 时，路径和权限更好分开。哪条更短要在同一画质、同一网络上量，不能按跳数下结论。两条路都不能经过旧 SRT、RTSP、TCP、旧 MediaMTX 或 panel。

## 回传编码

OBS 合成画面是新像素，必须再编码。插件在认识的编码器上关掉 B 帧，并把关键帧间隔压到不超过 2 秒。OBS 32.1.2 的 `plugins/mac-videotoolbox/encoder.c` 会把 `kVTCompressionPropertyKey_RealTime` 设成 false，也没有设置 `EnableLowLatencyRateControl`。`bframes=false` 只对应 `AllowFrameReordering`。这是 OBS 自带编码器的行为，本改动没有改 OBS。不能把它写成已经打开了 Apple 的低延迟码控。

## 这次没有测的

- macOS 编译、插件加载、OBS 出画、VideoToolbox 实帧。
- 手机 Vivid、硬编帧复用、单次 PCM、硬解到 Surface。
- 端到端延迟、弱网、断网重连、双机负载、证书轮换。
- 用本树重新跑 IETF22 QUIC 环回。附件里的 `.hang` 成功和 `.msf` 失败仍是 CLI 0.14.17 的记录。
- Opus、回传音画同步、旧 watchdog 是否只看 SRT。
