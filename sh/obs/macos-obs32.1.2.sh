#!/usr/bin/env bash
# 针对 OBS 32.1.2 / FFmpeg 61 / Qt 6.8.3 的 macOS arm64 构建入口。
# 不安装插件，不改 OBS、SRT、LaunchAgent，也不做 ABI 软链接。
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
spec_name="buildspec.obs32.1.2.json"
spec="$root/cpp/obs/$spec_name"
build_dir="$root/cpp/obs/build_macos_obs32.1.2"
# 装到构建目录，不写入正在使用的 OBS 插件目录。
dist="$build_dir/dist"

check_spec() {
	python3 - "$spec" <<'PY'
import json
import sys

path = sys.argv[1]
data = json.load(open(path, encoding="utf-8"))
deps = data["dependencies"]
expected = {
    "obs-studio": "32.1.2",
    "prebuilt": "2025-08-23",
    "qt6": "2025-08-23",
}
for name, version in expected.items():
    got = deps[name]["version"]
    if got != version:
        raise SystemExit(f"{name} version {got}, want {version}")
    digest = deps[name]["hashes"]["macos"]
    if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise SystemExit(f"{name} macos hash is not 64 hex chars")
print("spec", expected["obs-studio"], expected["prebuilt"], expected["qt6"])
PY
}

if [[ "${1:-}" == "--check" ]]; then
	check_spec
	exit 0
fi

check_spec

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
	echo "当前是 $(uname -s)/$(uname -m)。本脚本只在 macOS arm64 上配置并编译。" >&2
	echo "这里不能声称 Mac 构建或 OBS 加载已经通过。步骤见 cpp/obs/OBS-32.1.2.md。" >&2
	exit 2
fi

obs_app="${OBS_APP:-/Applications/OBS.app}"
obs_bin="$obs_app/Contents/MacOS/OBS"
plist="$obs_app/Contents/Info.plist"
if [[ ! -x "$obs_bin" || ! -f "$plist" ]]; then
	echo "找不到 $obs_bin。设置 OBS_APP 指向本机 OBS 32.1.2，不要改用其他版本。" >&2
	exit 1
fi

version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist")"
if [[ "$version" != "32.1.2" ]]; then
	echo "OBS 版本是 $version，要求 32.1.2。禁止为了装插件升级 OBS。" >&2
	exit 1
fi

links="$(otool -arch arm64 -L "$obs_bin")"
python3 - "$links" <<'PY'
import sys

text = sys.argv[1]
need = {
    "libavcodec": ("61.19.101", "61.0.0"),
    "libavutil": ("59.39.100", "59.0.0"),
    "QtCore": ("6.8.3", "6.0.0"),
}
found = {name: False for name in need}
for line in text.splitlines():
    for name, (current, compat) in need.items():
        if name not in line:
            continue
        if f"current version {current}" not in line or f"compatibility version {compat}" not in line:
            raise SystemExit(f"{name} 与 OBS 32.1.2 基线不一致: {line.strip()}")
        found[name] = True
    if "libavcodec" in line and "compatibility version 62" in line:
        raise SystemExit("OBS 二进制要求 libavcodec 62，这不是 32.1.2")
missing = [name for name, ok in found.items() if not ok]
if missing:
    raise SystemExit("otool 未看到 " + ", ".join(missing))
print("obs abi ok")
PY

for link in /opt/homebrew/lib/libavcodec.62.dylib /usr/local/lib/libavcodec.62.dylib; do
	if [[ -L "$link" ]]; then
		echo "拒绝：$link 是软链接。不要用伪造 ABI 冒充 FFmpeg 62。" >&2
		exit 1
	fi
done

cmake -S "$root/cpp/obs" -B "$build_dir" -G Xcode \
	-DOBS_MOQ_BUILDSPEC="$spec_name" \
	-DCMAKE_OSX_ARCHITECTURES=arm64 \
	-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
	-DCMAKE_INSTALL_PREFIX="$dist" \
	-DENABLE_FRONTEND_API=ON \
	-DENABLE_QT=ON \
	-DBUILD_PLUGIN=ON \
	-DMOQ_TARGET=aarch64-apple-darwin \
	-DMOQ_ARCHIVE=tar.gz
cmake --build "$build_dir" --config RelWithDebInfo
echo "构建输出在 $build_dir。安装前缀是 $dist，没有写入 OBS 插件目录。"
echo "加载 OBS 之前先读 cpp/obs/OBS-32.1.2.md 的运行前检查和撤销清单。"
