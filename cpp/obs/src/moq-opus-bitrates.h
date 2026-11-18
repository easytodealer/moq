// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// OBS 32.1.2 的 frontend/utility/audio-encoders.cpp 没有 get_bitrate 回调。
// 它调用 get_properties(NULL)，再读名为 bitrate 的 INT 或 LIST。
// ffmpeg_opus 的范围是 64..1024、步长 32。简单输出会把每个 Opus 编码器
// 报出的码率改接到该编码器。moq_opus 若报出同一网格，简单录音就会改用它。
// 启动枚举只放一个网格外的值，用来满足“列表非空”，不改 64 以上的选择。
// 真正编码仍读 settings 里的 bitrate，不读这个枚举项。
inline int MoqOpusStartupEnumerationKbps()
{
	return 6;
}

inline bool MoqOpusBitrateOnFfmpegGrid(int kbps)
{
	return kbps >= 64 && kbps <= 1024 && (kbps - 64) % 32 == 0;
}
