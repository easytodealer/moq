// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include <sys/stat.h>

// OBS 源的应用内 TLS 与订阅 token。
//
// 场景 JSON 会随场景集合落盘，不能放 JWT。token 只在拨号时从调用者指定的
// 本地文件读入，交给 ClientConfig::tokens 一次，不写入源属性，也不进入日志。
// 这不是系统钥匙串：拿到该文件读权限的进程仍能读到 token。文件必须是
// 属主可读、且不能是符号链接。CA PEM 是公开证书，允许其他用户读取，
// 但只信任这一份，不打开系统根证书，也不允许 insecure。
namespace MoQSourceAuth {

inline constexpr const char *kProtocol = "moq-transport-22";

struct Input {
	std::string url;
	std::string broadcast;
	std::string tls_root;
	std::string token_file;
	std::string server_name;
};

struct Secret {
	std::string value;

	Secret() = default;
	explicit Secret(std::string text) : value(std::move(text)) {}
	Secret(const Secret &) = delete;
	Secret &operator=(const Secret &) = delete;
	Secret(Secret &&other) noexcept : value(std::move(other.value)) { other.wipe(); }
	Secret &operator=(Secret &&other) noexcept
	{
		if (this != &other) {
			wipe();
			value = std::move(other.value);
			other.wipe();
		}
		return *this;
	}
	~Secret() { wipe(); }

	void wipe()
	{
		for (char &c : value)
			c = '\0';
		value.clear();
	}

	std::string take()
	{
		std::string out = std::move(value);
		value.clear();
		return out;
	}
};

struct Dial {
	bool secure = false;
	std::string url;
	std::string broadcast;
	std::vector<std::string> versions;
	std::optional<bool> websocket_enabled;
	std::string tls_root;
	std::optional<bool> system_roots;
	bool insecure = false;
	std::optional<std::string> server_name;
	Secret token;
	std::string error;

	explicit operator bool() const { return error.empty(); }
};

inline void Trim(std::string &text)
{
	size_t begin = 0;
	while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
		begin++;
	size_t end = text.size();
	while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
		end--;
	text = text.substr(begin, end - begin);
}

inline int B64(char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '-' || c == '+')
		return 62;
	if (c == '_' || c == '/')
		return 63;
	return -1;
}

inline bool Base64Url(const std::string &in, std::string &out)
{
	out.clear();
	int value = 0;
	int bits = 0;
	for (char c : in) {
		if (c == '=')
			continue;
		const int digit = B64(c);
		if (digit < 0)
			return false;
		value = (value << 6) | digit;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<char>((value >> bits) & 0xff));
		}
	}
	return !out.empty();
}

inline void SkipWs(const std::string &s, size_t &i)
{
	while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
		i++;
}

inline bool ParseString(const std::string &s, size_t &i, std::string &out)
{
	SkipWs(s, i);
	if (i >= s.size() || s[i] != '"')
		return false;
	i++;
	out.clear();
	while (i < s.size()) {
		const char c = s[i++];
		if (c == '"')
			return true;
		if (c == '\\') {
			if (i >= s.size())
				return false;
			out.push_back(s[i++]);
			continue;
		}
		out.push_back(c);
	}
	return false;
}

// 容器有任何成员即为非空。null 视为空。解析失败返回 false。
inline bool ValueNonEmpty(const std::string &s, size_t &i, bool &nonempty)
{
	SkipWs(s, i);
	if (i >= s.size())
		return false;
	const char c = s[i];
	if (c == 'n' && s.compare(i, 4, "null") == 0) {
		i += 4;
		nonempty = false;
		return true;
	}
	if (c == '"') {
		std::string ignored;
		if (!ParseString(s, i, ignored))
			return false;
		nonempty = true;
		return true;
	}
	if (c != '[' && c != '{') {
		while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']')
			i++;
		nonempty = true;
		return true;
	}
	const char end = c == '[' ? ']' : '}';
	i++;
	SkipWs(s, i);
	if (i < s.size() && s[i] == end) {
		i++;
		nonempty = false;
		return true;
	}
	int depth = 1;
	bool in_string = false;
	bool escape = false;
	for (; i < s.size(); i++) {
		const char ch = s[i];
		if (in_string) {
			if (escape)
				escape = false;
			else if (ch == '\\')
				escape = true;
			else if (ch == '"')
				in_string = false;
			continue;
		}
		if (ch == '"') {
			in_string = true;
			continue;
		}
		if (ch == '[' || ch == '{')
			depth++;
		else if (ch == ']' || ch == '}') {
			depth--;
			if (depth == 0) {
				i++;
				nonempty = true;
				return true;
			}
		}
	}
	return false;
}

inline bool SkipValue(const std::string &s, size_t &i)
{
	bool unused = false;
	return ValueNonEmpty(s, i, unused);
}

// 只看未验签的载荷，用来拦住明显写了 publish 的文件。通过不等于 token 真实或只可订阅。
// 签名、过期和路径授权由中继校验。这里不改写 token。
inline bool SubscribeOnlyPayload(const std::string &json, std::string &error)
{
	size_t i = 0;
	SkipWs(json, i);
	if (i >= json.size() || json[i] != '{') {
		error = "subscribe token payload is not a JSON object";
		return false;
	}
	i++;
	bool saw_subscribe = false;
	bool subscribe_nonempty = false;
	bool publish_nonempty = false;
	SkipWs(json, i);
	if (i < json.size() && json[i] == '}') {
		error = "subscribe token payload grants nothing";
		return false;
	}
	while (i < json.size()) {
		std::string key;
		if (!ParseString(json, i, key)) {
			error = "subscribe token payload is not readable";
			return false;
		}
		SkipWs(json, i);
		if (i >= json.size() || json[i] != ':') {
			error = "subscribe token payload is not readable";
			return false;
		}
		i++;
		if (key == "publish" || key == "put") {
			bool nonempty = false;
			if (!ValueNonEmpty(json, i, nonempty)) {
				error = "subscribe token payload is not readable";
				return false;
			}
			publish_nonempty = publish_nonempty || nonempty;
		} else if (key == "subscribe" || key == "get") {
			bool nonempty = false;
			if (!ValueNonEmpty(json, i, nonempty)) {
				error = "subscribe token payload is not readable";
				return false;
			}
			saw_subscribe = true;
			subscribe_nonempty = subscribe_nonempty || nonempty;
		} else if (!SkipValue(json, i)) {
			error = "subscribe token payload is not readable";
			return false;
		}
		SkipWs(json, i);
		if (i < json.size() && json[i] == ',') {
			i++;
			continue;
		}
		if (i < json.size() && json[i] == '}')
			break;
		error = "subscribe token payload is not readable";
		return false;
	}
	if (publish_nonempty) {
		error = "unverified payload grants publish; this source will not send it. The relay still verifies the signature";
		return false;
	}
	if (!saw_subscribe || !subscribe_nonempty) {
		error = "unverified payload does not grant subscribe. The relay still verifies the signature";
		return false;
	}
	return true;
}

inline bool ClassifyToken(const std::string &token, std::string &error)
{
	if (token.empty()) {
		error = "subscribe token file is empty";
		return false;
	}
	const size_t first = token.find('.');
	const size_t second = first == std::string::npos ? std::string::npos : token.find('.', first + 1);
	if (first == std::string::npos || second == std::string::npos || token.find('.', second + 1) != std::string::npos ||
	    first == 0 || second == first + 1 || second + 1 >= token.size()) {
		error = "subscribe token file must contain one JWT";
		return false;
	}
	std::string payload;
	if (!Base64Url(token.substr(first + 1, second - first - 1), payload)) {
		error = "subscribe token payload is not base64url";
		return false;
	}
	return SubscribeOnlyPayload(payload, error);
}

inline bool QueryHasJwt(const std::string &url)
{
	const size_t query = url.find('?');
	if (query == std::string::npos)
		return false;
	size_t i = query + 1;
	while (i < url.size()) {
		const size_t amp = url.find('&', i);
		const std::string part = url.substr(i, amp == std::string::npos ? std::string::npos : amp - i);
		const size_t eq = part.find('=');
		const std::string name = part.substr(0, eq);
		if (name == "jwt" || name == "JWT")
			return true;
		if (amp == std::string::npos)
			break;
		i = amp + 1;
	}
	return false;
}

inline bool ReadFile(const std::string &path, bool secret, std::string &body, std::string &error, const char *what)
{
	if (path.empty()) {
		error = std::string(what) + " path is empty";
		return false;
	}
	if (path.size() > 4096) {
		error = std::string(what) + " path is too long";
		return false;
	}
	// 打开时就拒绝符号链接，随后只 fstat 这个 fd。路径被换掉不会换成另一个 inode。
	const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		if (errno == ELOOP)
			error = std::string(what) + " must not be a symlink";
		else
			error = std::string("cannot open ") + what;
		return false;
	}
	struct stat st;
	if (fstat(fd, &st) != 0) {
		close(fd);
		error = std::string("cannot stat ") + what;
		return false;
	}
	if (!S_ISREG(st.st_mode)) {
		close(fd);
		error = std::string(what) + " must be a regular file";
		return false;
	}
	if (secret && (st.st_uid != getuid() || (st.st_mode & 077) != 0)) {
		close(fd);
		error = std::string(what) + " must be owned by this user and not readable by group or others";
		return false;
	}
	if (st.st_size <= 0 || st.st_size > 16384) {
		close(fd);
		error = st.st_size <= 0 ? std::string(what) + " is empty" : std::string(what) + " is larger than 16 KiB";
		return false;
	}
	const auto expected = static_cast<size_t>(st.st_size);
	body.assign(expected, '\0');
	size_t got = 0;
	while (got < expected) {
		const ssize_t n = read(fd, body.data() + got, expected - got);
		if (n < 0) {
			close(fd);
			body.assign(body.size(), '\0');
			body.clear();
			error = std::string("cannot read ") + what;
			return false;
		}
		if (n == 0)
			break;
		got += static_cast<size_t>(n);
	}
	struct stat again;
	const bool changed = fstat(fd, &again) != 0 || again.st_size != st.st_size || again.st_ino != st.st_ino;
	close(fd);
	if (changed || got != expected) {
		body.assign(body.size(), '\0');
		body.clear();
		error = std::string(what) + " changed while reading";
		return false;
	}
	return true;
}

inline bool DerSequence(const std::string &der)
{
	if (der.size() < 2 || static_cast<unsigned char>(der[0]) != 0x30)
		return false;
	size_t hdr = 2;
	size_t len = static_cast<unsigned char>(der[1]);
	if (static_cast<unsigned char>(der[1]) & 0x80) {
		const size_t nbytes = static_cast<unsigned char>(der[1]) & 0x7f;
		if (nbytes == 0 || nbytes > 3 || der.size() < 2 + nbytes)
			return false;
		len = 0;
		for (size_t i = 0; i < nbytes; i++)
			len = (len << 8) | static_cast<unsigned char>(der[2 + i]);
		hdr = 2 + nbytes;
	}
	if (hdr + len != der.size() || len < 2)
		return false;
	return static_cast<unsigned char>(der[hdr]) == 0x30;
}

inline bool PemHasCertificate(const std::string &body)
{
	if (body.find("PRIVATE KEY") != std::string::npos)
		return false;
	const std::string begin = "-----BEGIN CERTIFICATE-----";
	const std::string end = "-----END CERTIFICATE-----";
	size_t cursor = 0;
	bool any = false;
	while (cursor < body.size()) {
		const size_t at = body.find(begin, cursor);
		if (at == std::string::npos)
			return any;
		const size_t stop = body.find(end, at + begin.size());
		if (stop == std::string::npos)
			return false;
		std::string b64;
		for (size_t i = at + begin.size(); i < stop; i++) {
			const unsigned char c = static_cast<unsigned char>(body[i]);
			if (std::isspace(c))
				continue;
			if (c == '=')
				continue;
			if (B64(static_cast<char>(c)) < 0)
				return false;
			b64.push_back(static_cast<char>(c));
		}
		std::string der;
		if (!Base64Url(b64, der) || !DerSequence(der))
			return false;
		any = true;
		cursor = stop + end.size();
	}
	return any;
}

inline std::string HostOf(const std::string &url, std::string &error)
{
	const size_t scheme = url.find("://");
	if (scheme == std::string::npos || scheme == 0) {
		error = "URL has no scheme";
		return {};
	}
	const size_t auth = scheme + 3;
	const size_t end = url.find_first_of("/?#", auth);
	std::string authority = url.substr(auth, end == std::string::npos ? std::string::npos : end - auth);
	if (authority.empty() || authority.find('@') != std::string::npos) {
		error = "URL must not carry userinfo, and needs a host";
		return {};
	}
	if (authority.front() == '[') {
		const size_t bracket = authority.find(']');
		if (bracket == std::string::npos) {
			error = "URL host is not a valid address";
			return {};
		}
		return authority.substr(1, bracket - 1);
	}
	const size_t colon = authority.rfind(':');
	if (colon != std::string::npos)
		authority.resize(colon);
	if (authority.empty()) {
		error = "URL needs a host";
		return {};
	}
	return authority;
}

inline std::string SchemeOf(const std::string &url)
{
	const size_t scheme = url.find("://");
	if (scheme == std::string::npos)
		return {};
	std::string out = url.substr(0, scheme);
	for (char &c : out)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

inline bool IsIPv4(const std::string &name)
{
	int dots = 0;
	int group = -1;
	for (char c : name) {
		if (c == '.') {
			if (group < 0 || group > 255)
				return false;
			dots++;
			group = -1;
			continue;
		}
		if (c < '0' || c > '9')
			return false;
		group = (group < 0 ? 0 : group * 10) + (c - '0');
		if (group > 255)
			return false;
	}
	return dots == 3 && group >= 0 && group <= 255;
}

inline bool IsDnsName(const std::string &name)
{
	if (name.empty() || name.size() > 253 || name.front() == '.' || name.back() == '.')
		return false;
	size_t label = 0;
	bool hyphen = false;
	for (char c : name) {
		if (c == '.') {
			if (label == 0 || hyphen)
				return false;
			label = 0;
			hyphen = false;
			continue;
		}
		const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
		if (!ok || label >= 63)
			return false;
		if (label == 0 && c == '-')
			return false;
		hyphen = c == '-';
		label++;
	}
	return label > 0 && !hyphen;
}

// 空覆盖名表示校验 URL 主机，包括 IP SAN。非空则只校验这一个名字，不额外再校验 URL 主机，
// 也不关闭校验。IP 覆盖名必须就是 URL 里的那个地址。DNS 覆盖名可以替换 IP 主机，证书必须签这个名字。
inline bool ServerNameAllowed(const std::string &url_host, const std::string &override_name, std::string &error)
{
	if (override_name.empty())
		return true;
	if (override_name.find('*') != std::string::npos || override_name.find("://") != std::string::npos ||
	    override_name.find('/') != std::string::npos || override_name.find('?') != std::string::npos ||
	    override_name.find('@') != std::string::npos) {
		error = "server name override is not a single hostname";
		return false;
	}
	for (unsigned char c : override_name) {
		if (std::isspace(c) || c < 0x20) {
			error = "server name override is not a single hostname";
			return false;
		}
	}
	if (IsIPv4(override_name)) {
		if (override_name != url_host) {
			error = "IP server name override must match the URL host";
			return false;
		}
		return true;
	}
	if (!IsDnsName(override_name)) {
		error = "server name override is not a hostname";
		return false;
	}
	return true;
}

// 旧源不填 CA 和 token 文件时保持库默认（测试里的 http 指纹引导仍可用）。
// 两者任一有值就进入严格订阅：必须同时提供，https，协议 22，无 WebSocket，
// 只信任该 PEM，token 只从文件进入 ClientConfig 一次。
inline Dial Prepare(const Input &in)
{
	Dial dial;
	dial.url = in.url;
	dial.broadcast = in.broadcast;
	const bool want_secure = !in.tls_root.empty() || !in.token_file.empty() || !in.server_name.empty();
	if (!want_secure)
		return dial;

	dial.secure = true;
	if (in.tls_root.empty() || in.token_file.empty()) {
		dial.error = "app-local subscribe needs both a CA PEM and a subscribe token file";
		return dial;
	}
	if (dial.broadcast.empty()) {
		dial.error = "broadcast name is empty";
		return dial;
	}
	if (SchemeOf(dial.url) != "https") {
		dial.error = "app-local subscribe requires an https URL";
		return dial;
	}
	if (QueryHasJwt(dial.url)) {
		dial.error = "URL must not carry jwt; put the subscribe token in the token file";
		return dial;
	}
	std::string host_error;
	const std::string host = HostOf(dial.url, host_error);
	if (host.empty()) {
		dial.error = host_error;
		return dial;
	}
	if (!ServerNameAllowed(host, in.server_name, dial.error))
		return dial;
	if (!in.server_name.empty())
		dial.server_name = in.server_name;

	std::string pem;
	if (!ReadFile(in.tls_root, false, pem, dial.error, "CA PEM"))
		return dial;
	if (!PemHasCertificate(pem)) {
		pem.assign(pem.size(), '\0');
		dial.error = "CA PEM does not contain a certificate";
		return dial;
	}
	pem.assign(pem.size(), '\0');

	std::string token;
	if (!ReadFile(in.token_file, true, token, dial.error, "subscribe token file"))
		return dial;
	Trim(token);
	if (token.empty()) {
		dial.error = "subscribe token file is empty";
		return dial;
	}
	if (!ClassifyToken(token, dial.error)) {
		token.assign(token.size(), '\0');
		return dial;
	}

	dial.tls_root = in.tls_root;
	dial.versions = {kProtocol};
	dial.websocket_enabled = false;
	dial.system_roots = false;
	dial.insecure = false;
	dial.token = Secret(std::move(token));
	return dial;
}

inline std::string Describe(const Dial &dial)
{
	std::string text = dial.secure ? "secure https " : "legacy ";
	text += dial.broadcast;
	if (dial.secure) {
		text += " protocol=";
		text += kProtocol;
		text += " websocket=off system_roots=off root=";
		text += dial.tls_root;
	}
	return text;
}

} // namespace MoQSourceAuth
