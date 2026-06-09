/* ─── Bonus: server-mediated FILE transfer tests ─── */

#include "PostMan.hpp"
#include "TestHarness.hpp"

#include <cstdlib>

/* Fixture: server in a background thread (see TestHarness.hpp) */
class FileTransferTest : public IrcServerTest
{
protected:
	int portBase() const override { return 17500; }

	void registerPair(TestClient &a, TestClient &b)
	{
		ASSERT_TRUE(a.connect(serverPort));
		ASSERT_TRUE(b.connect(serverPort));
		a.registerClient("testpass", "fsend", "fsend");
		b.registerClient("testpass", "frecv", "frecv");
		a.recvAll();
		b.recvAll();
	}

	static void pause(int ms = 150)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	}
};

/* ── minimal base64 for the test side ── */
static const char B64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64encode(const std::string &in)
{
	std::string out;
	size_t i = 0;
	while (i + 2 < in.size())
	{
		unsigned v = (static_cast<unsigned char>(in[i]) << 16)
				   | (static_cast<unsigned char>(in[i + 1]) << 8)
				   | static_cast<unsigned char>(in[i + 2]);
		out += B64[(v >> 18) & 63];
		out += B64[(v >> 12) & 63];
		out += B64[(v >> 6) & 63];
		out += B64[v & 63];
		i += 3;
	}
	if (i + 1 == in.size())
	{
		unsigned v = static_cast<unsigned char>(in[i]) << 16;
		out += B64[(v >> 18) & 63];
		out += B64[(v >> 12) & 63];
		out += "==";
	}
	else if (i + 2 == in.size())
	{
		unsigned v = (static_cast<unsigned char>(in[i]) << 16)
				   | (static_cast<unsigned char>(in[i + 1]) << 8);
		out += B64[(v >> 18) & 63];
		out += B64[(v >> 12) & 63];
		out += B64[(v >> 6) & 63];
		out += '=';
	}
	return out;
}

static int b64val(char c)
{
	const char *p = std::strchr(B64, c);
	return p ? static_cast<int>(p - B64) : -1;
}

static std::string b64decode(const std::string &in)
{
	std::string out;
	int quad[4];
	size_t n = 0;
	for (size_t i = 0; i < in.size(); ++i)
	{
		if (in[i] == '=')
			break;
		int v = b64val(in[i]);
		if (v < 0)
			continue;
		quad[n++] = v;
		if (n == 4)
		{
			out += static_cast<char>((quad[0] << 2) | (quad[1] >> 4));
			out += static_cast<char>(((quad[1] & 15) << 4) | (quad[2] >> 2));
			out += static_cast<char>(((quad[2] & 3) << 6) | quad[3]);
			n = 0;
		}
	}
	if (n == 3)
	{
		out += static_cast<char>((quad[0] << 2) | (quad[1] >> 4));
		out += static_cast<char>(((quad[1] & 15) << 4) | (quad[2] >> 2));
	}
	else if (n == 2)
		out += static_cast<char>((quad[0] << 2) | (quad[1] >> 4));
	return out;
}

/* Extract a field: first line containing `key`, return it. */
static std::string lineWith(const std::string &data, const std::string &key)
{
	std::istringstream iss(data);
	std::string line;
	while (std::getline(iss, line))
	{
		if (line.find(key) != std::string::npos)
			return line;
	}
	return "";
}

/* ════════════════════════════════════════════════════════════════════════ */

TEST_F(FileTransferTest, HappyPathReassemblesByteIdentical)
{
	TestClient a, b;
	registerPair(a, b);

	/* A binary-ish payload incl. NUL-free high-bit bytes (it travels b64) */
	std::string payload;
	for (int i = 0; i < 700; ++i)
		payload += static_cast<char>((i * 7 + 3) & 0xFF);

	a.sendCmd("FILE SEND frecv blob.bin "
			  + std::to_string(payload.size()));
	pause();
	std::string offer = lineWith(b.recvAll(), "FILE OFFER");
	ASSERT_NE(offer, "") << "recipient should get FILE OFFER";
	/* offer: ":fsend!... FILE OFFER <id> blob.bin <size>" */
	std::istringstream off(offer);
	std::string tok, id;
	off >> tok >> tok >> tok >> id;
	ASSERT_NE(id, "");

	b.sendCmd("FILE ACCEPT " + id);
	pause();
	EXPECT_NE(lineWith(a.recvAll(), "FILE OK"), "");

	/* Send in chunks of 300 raw bytes -> 400 b64 chars */
	for (size_t off2 = 0; off2 < payload.size(); off2 += 300)
		a.sendCmd("FILE DATA " + id + " "
				  + b64encode(payload.substr(off2, 300)));
	a.sendCmd("FILE END " + id);
	pause(400);

	std::string got = b.recvAll();
	EXPECT_NE(lineWith(got, "FILE END"), "");

	/* Reassemble all DATA chunks and compare byte-for-byte */
	std::string rebuilt;
	std::istringstream iss(got);
	std::string line;
	while (std::getline(iss, line))
	{
		std::string::size_type p = line.find("FILE DATA " + id + " ");
		if (p == std::string::npos)
			continue;
		std::string chunk = line.substr(p + 10 + id.size() + 1);
		while (!chunk.empty() && (chunk[chunk.size() - 1] == '\r'))
			chunk.erase(chunk.size() - 1);
		rebuilt += b64decode(chunk);
	}
	EXPECT_EQ(rebuilt.size(), payload.size());
	EXPECT_TRUE(rebuilt == payload) << "payload corrupted in relay";
}

TEST_F(FileTransferTest, RejectStopsTransfer)
{
	TestClient a, b;
	registerPair(a, b);

	a.sendCmd("FILE SEND frecv doc.txt 100");
	pause();
	std::string offer = lineWith(b.recvAll(), "FILE OFFER");
	std::istringstream off(offer);
	std::string tok, id;
	off >> tok >> tok >> tok >> id;

	b.sendCmd("FILE REJECT " + id);
	pause();
	EXPECT_NE(lineWith(a.recvAll(), "FILE NO"), "");

	/* DATA after reject: id is gone */
	a.sendCmd("FILE DATA " + id + " QUJD");
	pause();
	EXPECT_NE(lineWith(a.recvAll(), "no transfer"), "");
}

TEST_F(FileTransferTest, DataBeforeAcceptIsRefused)
{
	TestClient a, b;
	registerPair(a, b);

	a.sendCmd("FILE SEND frecv doc.txt 100");
	pause();
	std::string offer = lineWith(b.recvAll(), "FILE OFFER");
	std::istringstream off(offer);
	std::string tok, id;
	off >> tok >> tok >> tok >> id;

	a.recvAll();
	a.sendCmd("FILE DATA " + id + " QUJD");
	pause();
	EXPECT_NE(lineWith(a.recvAll(), "not accepted"), "");
}

TEST_F(FileTransferTest, MalformedChunkAborts)
{
	TestClient a, b;
	registerPair(a, b);

	a.sendCmd("FILE SEND frecv doc.txt 1000");
	pause();
	std::string offer = lineWith(b.recvAll(), "FILE OFFER");
	std::istringstream off(offer);
	std::string tok, id;
	off >> tok >> tok >> tok >> id;
	b.sendCmd("FILE ACCEPT " + id);
	pause();
	a.recvAll();

	a.sendCmd("FILE DATA " + id + " not*base64!");
	pause();
	EXPECT_NE(lineWith(a.recvAll(), "FILE ABRT"), "");
}

TEST_F(FileTransferTest, SizeOverrunAborts)
{
	TestClient a, b;
	registerPair(a, b);

	a.sendCmd("FILE SEND frecv tiny.txt 4");
	pause();
	std::string offer = lineWith(b.recvAll(), "FILE OFFER");
	std::istringstream off(offer);
	std::string tok, id;
	off >> tok >> tok >> tok >> id;
	b.sendCmd("FILE ACCEPT " + id);
	pause();
	a.recvAll();

	/* 9 bytes of data against a declared size of 4 */
	a.sendCmd("FILE DATA " + id + " " + b64encode("123456789"));
	pause();
	EXPECT_NE(lineWith(a.recvAll(), "size overrun"), "");
}

TEST_F(FileTransferTest, InvalidTargetsAndArgs)
{
	TestClient a, b;
	registerPair(a, b);

	a.sendCmd("FILE SEND ghost doc.txt 10");
	a.sendCmd("FILE SEND fsend doc.txt 10");          /* to self */
	a.sendCmd("FILE SEND frecv ../etc/passwd 10");    /* path escape */
	a.sendCmd("FILE SEND frecv doc.txt 0");           /* zero size */
	a.sendCmd("FILE SEND frecv doc.txt 99999999999"); /* > cap */
	pause(250);
	std::string out = a.recvAll();
	EXPECT_NE(lineWith(out, "no such nick"), "");
	EXPECT_NE(lineWith(out, "yourself"), "");
	EXPECT_NE(lineWith(out, "invalid filename"), "");
	EXPECT_NE(lineWith(out, "invalid size"), "");
	/* nothing must reach the would-be recipient */
	EXPECT_EQ(lineWith(b.recvAll(), "FILE OFFER"), "");
}

TEST_F(FileTransferTest, DisconnectAbortsAndNotifiesPeer)
{
	TestClient a, b;
	registerPair(a, b);

	a.sendCmd("FILE SEND frecv doc.txt 100");
	pause();
	std::string offer = lineWith(b.recvAll(), "FILE OFFER");
	std::istringstream off(offer);
	std::string tok, id;
	off >> tok >> tok >> tok >> id;
	b.sendCmd("FILE ACCEPT " + id);
	pause();
	a.recvAll();

	b.sendCmd("QUIT :gone");
	pause(300);
	EXPECT_NE(lineWith(a.recvAll(), "FILE ABRT"), "");
}

TEST_F(FileTransferTest, FileIsUnknownInMandatoryDispatchOnly)
{
	/* Sanity: with the full tier linked, FILE never reaches 421 */
	TestClient a;
	ASSERT_TRUE(a.connect(serverPort));
	a.registerClient("testpass", "fonly", "fonly");
	a.recvAll();
	a.sendCmd("FILE");
	pause();
	std::string out = a.recvAll();
	EXPECT_EQ(out.find(" 421 "), std::string::npos)
		<< "FILE should be consumed by the extension, not 421: " << out;
	EXPECT_NE(lineWith(out, "FILE usage"), "");
}
