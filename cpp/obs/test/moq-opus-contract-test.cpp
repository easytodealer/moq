// SPDX-License-Identifier: GPL-2.0-or-later
// OBS 32.1.2 frontend/utility/audio-encoders.cpp 的码率枚举和简单输出选编码器。
// 这里按那份源码的调用顺序建模，不链接 OBS。
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                 \
	do {                                                                        \
		if (!(cond)) {                                                      \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			g_failures++;                                               \
		}                                                                   \
	} while (0)

struct Encoder {
	std::string id;
	std::string codec;
	bool properties = false;
	std::vector<int> bitrates;
	int default_kbps = 128;
};

std::vector<int> FfmpegGrid()
{
	std::vector<int> out;
	for (int kbps = 64; kbps <= 1024; kbps += 32)
		out.push_back(kbps);
	return out;
}

struct EnumResult {
	std::vector<std::string> warnings;
	std::map<std::string, std::vector<int>> listed;
};

EnumResult Enumerate(const std::vector<Encoder> &encoders)
{
	EnumResult result;
	for (const Encoder &encoder : encoders) {
		if (encoder.codec != "aac" && encoder.codec != "opus")
			continue;
		if (!encoder.properties) {
			result.warnings.push_back("Failed to get properties for encoder '" + encoder.id + "'");
			result.listed[encoder.id] = {};
		} else {
			result.listed[encoder.id] = encoder.bitrates;
		}
		if (result.listed[encoder.id].empty())
			result.warnings.push_back("Could not enumerate " + encoder.id + " encoder bitrates");
	}
	return result;
}

std::map<int, std::string> SimpleOpusMap(const EnumResult &listed, const std::vector<Encoder> &encoders)
{
	std::map<int, std::string> map;
	const auto fallback = listed.listed.count("ffmpeg_aac") ? listed.listed.at("ffmpeg_aac") : std::vector<int>{};
	for (int bitrate : fallback)
		map[bitrate] = "ffmpeg_opus";
	for (const Encoder &encoder : encoders) {
		if (encoder.codec != "opus")
			continue;
		const auto it = listed.listed.find(encoder.id);
		if (it == listed.listed.end())
			continue;
		for (int bitrate : it->second)
			map[bitrate] = encoder.id;
	}
	return map;
}

int Closest(int want, const std::map<int, std::string> &map)
{
	int prev = 0;
	int next = 10000;
	for (const auto &item : map) {
		if (next > item.first) {
			if (item.first == want)
				return want;
			if (item.first < next && item.first > want)
				next = item.first;
			if (item.first > prev && item.first < want)
				prev = item.first;
		}
	}
	if (next != 10000)
		return next;
	if (prev != 0)
		return prev;
	return 192;
}

bool SourceOmitsProperties()
{
	const char *paths[] = {"src/moq-audio-encoder.cpp", "cpp/obs/src/moq-audio-encoder.cpp"};
	std::ifstream in;
	for (const char *path : paths) {
		in.open(path);
		if (in)
			break;
	}
	CHECK(in);
	std::stringstream buffer;
	buffer << in.rdbuf();
	const std::string text = buffer.str();
	return text.find("get_properties") == std::string::npos && text.find("obs_properties_add_list") == std::string::npos;
}

} // namespace

int main()
{
	const std::vector<int> grid = FfmpegGrid();
	Encoder aac{"ffmpeg_aac", "aac", true, grid, 128};
	Encoder opus{"ffmpeg_opus", "opus", true, grid, 128};
	Encoder moq{"moq_opus", "opus", false, {}, 128};

	const EnumResult shipped = Enumerate({aac, opus, moq});
	CHECK(shipped.warnings.size() == 2);
	CHECK(shipped.warnings[0].find("Failed to get properties") != std::string::npos);
	CHECK(shipped.warnings[1].find("Could not enumerate moq_opus") != std::string::npos);
	const auto shipped_map = SimpleOpusMap(shipped, {aac, opus, moq});
	CHECK(shipped_map.at(128) == "ffmpeg_opus");
	CHECK(Closest(128, shipped_map) == 128);
	CHECK(shipped_map.count(6) == 0);
	CHECK(moq.default_kbps == 128);

	Encoder hijack = moq;
	hijack.properties = true;
	hijack.bitrates = grid;
	const auto hijacked = SimpleOpusMap(Enumerate({aac, opus, hijack}), {aac, opus, hijack});
	CHECK(hijacked.at(128) == "moq_opus");

	Encoder sentinel = moq;
	sentinel.properties = true;
	sentinel.bitrates = {6};
	const auto sentinel_map = SimpleOpusMap(Enumerate({aac, opus, sentinel}), {aac, opus, sentinel});
	CHECK(sentinel_map.at(128) == "ffmpeg_opus");
	CHECK(sentinel_map.at(6) == "moq_opus");
	CHECK(Closest(128, sentinel_map) == 128);
	CHECK(sentinel.bitrates.size() == 1 && sentinel.bitrates[0] != sentinel.default_kbps);

	CHECK(SourceOmitsProperties());

	if (g_failures) {
		std::fprintf(stderr, "%d checks failed\n", g_failures);
		return 1;
	}
	std::puts("obs 32.1.2 opus enumeration contract passed");
	return 0;
}
