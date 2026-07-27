/* ─── Unit tests: vendor/libcpp/c98 building blocks ─── */

#include <gtest/gtest.h>
#include "PostMan.hpp"

#include "libcpp98/line_buffer.hpp"
#include "libcpp98/buffered_socket.hpp"
#include "libcpp98/csv_writer.hpp"
#include "libcpp98/reactor.hpp"

#include <cstdio>
#include <fstream>
#include <sys/epoll.h>
#include <unistd.h>

/* ════════════════════════════════════════════════════════════════════════
 * LineBuffer
 * ════════════════════════════════════════════════════════════════════ */

TEST(LineBuffer98, FragmentedCRLF)
{
	libcpp98::LineBuffer lb;
	std::string out;
	lb.append("HEL");
	EXPECT_FALSE(lb.next(out));
	lb.append("LO\r\nWO");
	ASSERT_TRUE(lb.next(out));
	EXPECT_EQ(out, "HELLO");
	EXPECT_FALSE(lb.next(out));
	lb.append("RLD\n");
	ASSERT_TRUE(lb.next(out));
	EXPECT_EQ(out, "WORLD");
}

TEST(LineBuffer98, EmptyLinesAreReturned)
{
	libcpp98::LineBuffer lb;
	std::string out;
	lb.append("\r\n\n");
	ASSERT_TRUE(lb.next(out));
	EXPECT_EQ(out, "");
	ASSERT_TRUE(lb.next(out));
	EXPECT_EQ(out, "");
	EXPECT_FALSE(lb.next(out));
}

TEST(LineBuffer98, ForceExtractAtCap)
{
	libcpp98::LineBuffer lb(8);
	std::string out;
	lb.append("ABCDEFGHIJKL"); /* 12 bytes, no terminator, cap 8 */
	ASSERT_TRUE(lb.next(out));
	EXPECT_EQ(out, "ABCDEFGH");
	EXPECT_EQ(lb.size(), 0u) << "remainder dropped (flood guard)";
}

TEST(LineBuffer98, NoCapMeansUnbounded)
{
	libcpp98::LineBuffer lb(0);
	std::string out;
	lb.append(std::string(10000, 'x'));
	EXPECT_FALSE(lb.next(out));
	EXPECT_EQ(lb.size(), 10000u);
}

TEST(LineBuffer98, NeverReturnsMoreThanTheCap)
{
	/* A terminator arriving *beyond* the cap took the "found a newline"
	 * branch, which returned the whole over-long line -- so maxLine was
	 * only enforced while no terminator existed, making it advisory rather
	 * than an invariant. Reachable in ft_irc: recv() reads at most 512
	 * bytes per call, so a 600-byte line simply spans two reads. */
	libcpp98::LineBuffer lb(8);
	std::string out;
	lb.append("ABCDEFGHIJKL\n");
	ASSERT_TRUE(lb.next(out));
	EXPECT_LE(out.size(), 8u) << "the cap must bound every returned line";
}

TEST(LineBuffer98, OverlongRemainderIsNotDeliveredAsAFreshLine)
{
	/* After force-extracting at the cap, the REST of that same over-long
	 * line must be discarded up to its terminator. Delivering it as a new
	 * line hands the tail of a flood to the protocol parser as a command --
	 * for ft_irc that means a client can smuggle a command past the line
	 * limit by prefixing it with 512 bytes of padding. */
	libcpp98::LineBuffer lb(8);
	std::string out;
	lb.append("ABCDEFGHIJKL");          /* over-long, terminator not yet in */
	ASSERT_TRUE(lb.next(out));
	EXPECT_EQ(out, "ABCDEFGH");
	EXPECT_FALSE(lb.next(out));

	lb.append("MNOP\nGOOD\n");          /* "MNOP" is still the old line's tail */
	ASSERT_TRUE(lb.next(out));
	EXPECT_EQ(out, "GOOD")
		<< "the tail of the over-long line leaked through as its own line";
	EXPECT_FALSE(lb.next(out));
}

TEST(LineBuffer98, OverlongLineDoesNotSwallowTheNextRealLine)
{
	/* The whole over-long line arrives at once, terminator included, with a
	 * legitimate line right behind it. The good line must survive. */
	libcpp98::LineBuffer lb(8);
	std::string out;
	lb.append("ABCDEFGHIJKL\nGOOD\n");
	ASSERT_TRUE(lb.next(out));
	EXPECT_LE(out.size(), 8u);
	ASSERT_TRUE(lb.next(out));
	EXPECT_EQ(out, "GOOD") << "the following line was lost with the truncation";
	EXPECT_FALSE(lb.next(out));
}

/* ════════════════════════════════════════════════════════════════════════
 * BufferedSocket
 * ════════════════════════════════════════════════════════════════════ */

TEST(BufferedSocket98, QueueAppendsCRLFAndConsumes)
{
	libcpp98::BufferedSocket io(512, 0);
	EXPECT_TRUE(io.queue("PING :x"));
	EXPECT_EQ(io.outData(), "PING :x\r\n");
	io.consume(4);
	EXPECT_EQ(io.outData(), " :x\r\n");
	io.consume(5);
	EXPECT_FALSE(io.hasPending());
}

TEST(BufferedSocket98, SendQOverflowIsStickyAndDrops)
{
	libcpp98::BufferedSocket io(512, 30);
	EXPECT_TRUE(io.queue("0123456789"));          /* 12 bytes used */
	EXPECT_TRUE(io.queue("0123456789"));          /* 24 bytes used */
	EXPECT_FALSE(io.queue("0123456789"));         /* would be 36 > 30 */
	EXPECT_TRUE(io.overflowed());
	EXPECT_EQ(io.outSize(), 24u) << "overflowing line dropped";
	io.consume(24);
	EXPECT_TRUE(io.overflowed()) << "overflow latches";
}

/* ════════════════════════════════════════════════════════════════════════
 * CsvWriter
 * ════════════════════════════════════════════════════════════════════ */

TEST(CsvWriter98, EscapeRFC4180)
{
	using libcpp98::CsvWriter;
	EXPECT_EQ(CsvWriter::escape("plain"), "plain");
	EXPECT_EQ(CsvWriter::escape("a,b"), "\"a,b\"");
	EXPECT_EQ(CsvWriter::escape("say \"hi\""), "\"say \"\"hi\"\"\"");
	EXPECT_EQ(CsvWriter::escape("line\nbreak"), "\"line\nbreak\"");
}

TEST(CsvWriter98, HeaderOnceAcrossReopens)
{
	std::string path = "/tmp/ftirc_csvtest.csv";
	std::remove(path.c_str());

	{
		libcpp98::CsvWriter w;
		ASSERT_TRUE(w.open(path));
		EXPECT_TRUE(w.isNewFile());
		std::vector<std::string> row;
		row.push_back("h1");
		row.push_back("h,2");
		w.row(row);
	}
	{
		libcpp98::CsvWriter w;
		ASSERT_TRUE(w.open(path));
		EXPECT_FALSE(w.isNewFile()) << "existing file must not be 'new'";
		std::vector<std::string> row;
		row.push_back("v1");
		row.push_back("v2");
		w.row(row);
	}

	std::ifstream in(path.c_str());
	std::string l1, l2;
	std::getline(in, l1);
	std::getline(in, l2);
	EXPECT_EQ(l1, "h1,\"h,2\"");
	EXPECT_EQ(l2, "v1,v2");
	std::remove(path.c_str());
}

/* ════════════════════════════════════════════════════════════════════════
 * Reactor
 * ════════════════════════════════════════════════════════════════════ */

TEST(Reactor98, CtlOpsOnRealFds)
{
	libcpp98::Reactor r;
	EXPECT_FALSE(r.ok());
	ASSERT_TRUE(r.open());
	EXPECT_TRUE(r.ok());
	EXPECT_GE(r.fd(), 0);

	int pipefd[2];
	ASSERT_EQ(pipe(pipefd), 0);

	EXPECT_TRUE(r.add(pipefd[0], EPOLLIN));
	EXPECT_FALSE(r.add(pipefd[0], EPOLLIN)) << "double add must fail (EEXIST)";
	EXPECT_TRUE(r.modify(pipefd[0], EPOLLIN | EPOLLOUT));
	EXPECT_TRUE(r.remove(pipefd[0]));
	EXPECT_FALSE(r.remove(pipefd[0])) << "double remove must fail (ENOENT)";

	/* an event actually arrives through the wrapped instance */
	ASSERT_TRUE(r.add(pipefd[0], EPOLLIN));
	ASSERT_EQ(write(pipefd[1], "x", 1), 1);
	struct epoll_event ev;
	int n = epoll_wait(r.fd(), &ev, 1, 1000);
	EXPECT_EQ(n, 1);
	EXPECT_EQ(ev.data.fd, pipefd[0]);

	close(pipefd[0]);
	close(pipefd[1]);
}
