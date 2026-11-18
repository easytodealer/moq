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

## 源的应用内 CA 和订阅 token

`moq_source` 以前只暴露 `url`、`broadcast`、`quic_keylog`，拨号用库默认：可以协商 moq-lite，WebSocket 默认开启，没有指定根证书时走系统根。FFI 的 `ClientConfig` 已经能表达应用内 PEM、`system_roots=false`、单一 token、版本列表和关闭 WebSocket。缺的是源没有把这些交出去。

三个新属性默认都是空字符串，所以旧场景仍走原来的库默认：

- `tls_root`：只信任这一份 CA PEM，不读系统证书库，也不打开 insecure。
- `token_file`：属主可读的订阅 JWT 文件。正文不写入场景，日志只记路径和拒绝原因。
- `tls_server_name`：空则按 URL 主机校验，IP SAN 走这个主机。填了就用它替换校验名，不能是 `*`。

三个新属性都空着时，源保持这次改动之前的库默认：可以协商 moq-lite，WebSocket 仍按库默认，也不要求本地 CA。旧的 `url` / `broadcast` / `quic_keylog` 场景因此不用改。

任一有值就进入严格订阅。缺另一项、不是 `https`、URL 自带 `jwt` 或 userinfo、PEM 解不出 X.509 证书或夹着私钥、token 文件空、不是 JWT、不是属主且模式含组/其他读、是符号链接、读的过程中变了、超过 16 KiB、未验签载荷写了 publish，都直接拒绝，不拨号。通过时版本只有 `moq-transport-22`，WebSocket 关闭，`insecure` 和系统根都是关，token 只放进 `ClientConfig::tokens` 一次。URL 的 path 和 `broadcast` 不拼接。`tls_server_name` 为空就校验 URL 主机；填了就只校验这一个名字。IP 覆盖名必须等于 URL 主机。通配符不行。

威胁模型：场景 JSON 不放 JWT。错误和描述字符串不带 token 正文，也不带 token 文件路径。打开用 `O_NOFOLLOW`，权限看的是这个 fd 的 `fstat`，所以路径在打开之后被换成别的文件不会读到新 inode。这不是钥匙串。能读该文件的进程仍能读到 token。载荷里的 publish/subscribe 检查没有验签，只用来拒绝明显配错的文件；中继仍要验证签名和授权。握手和出画没有在这台云主机上做过。

## Opus 启动告警

OBS 32.1.2 的 `obs_encoder_info` 没有 `get_bitrate`。`frontend/utility/audio-encoders.cpp` 对每个音频编码器，包括 `OBS_ENCODER_CAP_INTERNAL`，调用 `get_properties(NULL)`，再读名为 `bitrate` 的 INT 或 LIST。`moq_opus` 没有这个回调，所以启动时仍会打两行：

- `Failed to get properties for encoder 'MoQ Opus' (moq_opus)`
- `Could not enumerate moq_opus encoder bitrates`

这只发生在输出编码器的码率表扫描。源的 Opus 解码走 `moq::AudioConsumer`，不经过 `moq_opus`。编码器自己的默认值仍是 `get_defaults` 里的 128，扫描不会改它。

`ffmpeg_opus` 报的是 64 到 1024、步长 32。简单输出先把这些值记到 `ffmpeg_opus`，再让每个 `codec == "opus"` 的编码器覆盖自己报出的值。插件在 FFmpeg 之后加载。若 `moq_opus` 报出同一网格，`GetSimpleOpusEncoderForBitrate(128)` 会变成 `moq_opus`，简单输出就会去创建它。若只报 6，128 仍留在 `ffmpeg_opus`，但简单输出的下拉框会多一个 6 kbit/s，而且这个列表里没有编码器自己的默认 128。两种都改变 OBS 的选择，所以不设置 `get_properties`。告警还在，码率选择不改。

## 重建

这次没有改 Rust FFI、wire 或 `moq-ffi` 的 ABI。Android 如果仍钉在 `40f3b92d` 的原生库，不必为这个提交重编 JNI。要更新的是 OBS 插件本身，仍用 `sh/obs/macos-obs32.1.2.sh`。Ninja 加命令行工具也可以，只要传入 `-DOBS_MOQ_BUILDSPEC=buildspec.obs32.1.2.json`，并且不要升级 OBS 或 obs-deps。装进正在使用的 OBS 需要另一次维护许可。装上之后，还要用应用内 CA 完成一次握手并解出音画，才能说这条输入可用。

## 这次没有测的

- macOS 编译、插件加载、OBS 出画、VideoToolbox 实帧。
- 用应用内 CA 和订阅 token 对 `10.244.144.10` 的真实握手，以及解码后的音画。
- 手机 Vivid、硬编帧复用、单次 PCM、硬解到 Surface。
- 端到端延迟、弱网、断网重连、双机负载、证书轮换。
- 用本树重新跑 IETF22 QUIC 环回。附件里的 `.hang` 成功和 `.msf` 失败仍是 CLI 0.14.17 的记录。
- 回传音画同步、旧 watchdog 是否只看 SRT。
- 本环境没有 nix、libobs 和 uniffi-bindgen，所以没有跑 `just obs test` 那套链接 moq-ffi 的插件测试。
