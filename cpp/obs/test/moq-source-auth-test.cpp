// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/moq-source-auth.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
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

std::string B64(const std::string &in)
{
	static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string out;
	int value = 0;
	int bits = -6;
	for (unsigned char c : in) {
		value = (value << 8) + c;
		bits += 8;
		while (bits >= 0) {
			out.push_back(table[(value >> bits) & 0x3f]);
			bits -= 6;
		}
	}
	if (bits > -6)
		out.push_back(table[((value << 8) >> (bits + 8)) & 0x3f]);
	return out;
}

std::string Jwt(const std::string &payload)
{
	return B64("hdr") + "." + B64(payload) + "." + B64("sig");
}

std::string Dir()
{
	char path[] = "/tmp/moq-source-auth-XXXXXX";
	CHECK(mkdtemp(path) != nullptr);
	return path;
}

void WriteFile(const std::string &path, const std::string &body, mode_t mode)
{
	const int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, mode);
	CHECK(fd >= 0);
	CHECK(write(fd, body.data(), body.size()) == static_cast<ssize_t>(body.size()));
	CHECK(fchmod(fd, mode) == 0);
	close(fd);
}

std::string Cert()
{
	const std::string dir = Dir();
	const std::string pem = dir + "/ca.pem";
	const std::string cmd = "openssl req -x509 -newkey rsa:2048 -keyout " + dir + "/ca.key -out " + pem +
				" -days 1 -nodes -subj /CN=moq-test >/dev/null 2>&1";
	CHECK(std::system(cmd.c_str()) == 0);
	std::ifstream in(pem);
	std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	CHECK(body.find("BEGIN CERTIFICATE") != std::string::npos);
	return pem;
}

} // namespace

int main()
{
	using MoQSourceAuth::Dial;
	using MoQSourceAuth::Input;
	using MoQSourceAuth::Prepare;

	{
		Dial dial = Prepare(Input{"http://127.0.0.1:4443", "obs/test", "", "", ""});
		CHECK(dial);
		CHECK(!dial.secure);
		CHECK(dial.versions.empty());
		CHECK(!dial.websocket_enabled);
		CHECK(!dial.system_roots);
		CHECK(dial.url == "http://127.0.0.1:4443");
		CHECK(dial.broadcast == "obs/test");
		CHECK(dial.token.take().empty());
	}

	const std::string dir = Dir();
	const std::string root = Cert();
	const std::string token_path = dir + "/token";
	const std::string subscribe = Jwt(R"({"root":"live","subscribe":["**"]})");
	WriteFile(token_path, subscribe + "\n", 0600);

	{
		const std::string url = "https://10.244.144.10:4443/live";
		const std::string broadcast = "live2/vivo/moq.hang";
		Dial dial = Prepare(Input{url, broadcast, root, token_path, ""});
		CHECK(dial);
		CHECK(dial.secure);
		CHECK(dial.url == url);
		CHECK(dial.broadcast == broadcast);
		CHECK(dial.url.find(broadcast) == std::string::npos);
		CHECK(dial.versions.size() == 1);
		CHECK(dial.versions[0] == MoQSourceAuth::kProtocol);
		CHECK(dial.websocket_enabled && *dial.websocket_enabled == false);
		CHECK(dial.system_roots && *dial.system_roots == false);
		CHECK(dial.insecure == false);
		CHECK(!dial.server_name);
		const std::string described = MoQSourceAuth::Describe(dial);
		CHECK(described.find(subscribe) == std::string::npos);
		const std::string token = dial.token.take();
		CHECK(token == subscribe);
		CHECK(dial.token.take().empty());
		CHECK(MoQSourceAuth::Describe(dial).find(subscribe) == std::string::npos);
	}

	{
		Dial dial = Prepare(Input{"https://10.244.144.10:4443", "room", root, "", ""});
		CHECK(!dial);
		CHECK(dial.error.find("both") != std::string::npos);
	}

	{
		Dial dial = Prepare(Input{"http://10.244.144.10:4443", "room", root, token_path, ""});
		CHECK(!dial);
		CHECK(dial.error.find("https") != std::string::npos);
	}

	{
		Dial dial = Prepare(Input{"https://user:secret@10.244.144.10:4443", "room", root, token_path, ""});
		CHECK(!dial);
		CHECK(dial.error.find("userinfo") != std::string::npos);
	}

	{
		Dial dial = Prepare(Input{"https://10.244.144.10:4443?jwt=" + subscribe, "room", root, token_path, ""});
		CHECK(!dial);
		CHECK(dial.error.find("jwt") != std::string::npos);
		CHECK(dial.error.find(subscribe) == std::string::npos);
	}

	{
		const std::string publish = Jwt(R"({"publish":["**"],"subscribe":["**"]})");
		const std::string path = dir + "/publish";
		WriteFile(path, publish, 0600);
		Dial dial = Prepare(Input{"https://10.244.144.10:4443", "room", root, path, ""});
		CHECK(!dial);
		CHECK(dial.error.find("publish") != std::string::npos);
		CHECK(dial.error.find(publish) == std::string::npos);
	}

	{
		const std::string path = dir + "/open-token";
		WriteFile(path, subscribe, 0644);
		Dial dial = Prepare(Input{"https://10.244.144.10:4443", "room", root, path, ""});
		CHECK(!dial);
		CHECK(dial.error.find("group") != std::string::npos);
	}

	{
		const std::string link = dir + "/token-link";
		CHECK(symlink(token_path.c_str(), link.c_str()) == 0);
		Dial dial = Prepare(Input{"https://10.244.144.10:4443", "room", root, link, ""});
		CHECK(!dial);
		CHECK(dial.error.find("symlink") != std::string::npos);
	}

	{
		const std::string path = dir + "/not-a-cert.pem";
		WriteFile(path, "not a certificate\n", 0644);
		Dial dial = Prepare(Input{"https://10.244.144.10:4443", "room", path, token_path, ""});
		CHECK(!dial);
		CHECK(dial.error.find("certificate") != std::string::npos);
	}

	{
		Dial dial = Prepare(Input{"https://10.244.144.10:4443", "room", root, token_path, "*"});
		CHECK(!dial);
		CHECK(dial.error.find("hostname") != std::string::npos);
	}

	{
		Dial dial = Prepare(Input{"https://10.244.144.10:4443", "room", root, token_path, "relay.example"});
		CHECK(dial);
		CHECK(dial.server_name && *dial.server_name == "relay.example");
	}

	if (g_failures) {
		std::fprintf(stderr, "%d checks failed\n", g_failures);
		return 1;
	}
	std::puts("moq source auth checks passed");
	return 0;
}
