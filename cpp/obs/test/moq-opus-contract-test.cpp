// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/moq-opus-bitrates.h"

#include <cstdio>

int main()
{
	const int listed = MoqOpusStartupEnumerationKbps();
	if (MoqOpusBitrateOnFfmpegGrid(listed)) {
		std::fprintf(stderr, "enumeration bitrate %d sits on the ffmpeg_opus grid\n", listed);
		return 1;
	}
	if (MoqOpusBitrateOnFfmpegGrid(64) == false || MoqOpusBitrateOnFfmpegGrid(128) == false ||
	    MoqOpusBitrateOnFfmpegGrid(1024) == false) {
		std::fprintf(stderr, "ffmpeg grid helper missed a real step\n");
		return 1;
	}
	if (MoqOpusBitrateOnFfmpegGrid(48) || MoqOpusBitrateOnFfmpegGrid(510)) {
		std::fprintf(stderr, "ffmpeg grid helper accepted a value outside 64..1024 step 32\n");
		return 1;
	}
	std::puts("moq opus enumeration contract passed");
	return 0;
}
