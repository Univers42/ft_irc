/* ─── Unit tests: vendor/libcpp/c98 building blocks ─── */

#include <gtest/gtest.h>
#include "PostMan.hpp"

#include "libcpp98/line_buffer.hpp"
#include "libcpp98/buffered_socket.hpp"
#include "libcpp98/csv_writer.hpp"
#include "libcpp98/reactor.hpp"
#include "libcpp98/traffic_stats.hpp"
#include "libcpp98/expiring_registry.hpp"

#include <cstdio>
#include <string>
#include <vector>
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

/* ════════════════════════════════════════════════════════════════════════
 * TrafficStats
 * ════════════════════════════════════════════════════════════════════ */

TEST(TrafficStats98, CountsPerKeyAndInTotal)
{
	libcpp98::TrafficStats s;
	s.open(4);
	s.open(7);
	s.countIn(4, 10);
	s.countIn(4, 20);
	s.countOut(7, 5);

	ASSERT_TRUE(s.get(4) != NULL);
	EXPECT_EQ(s.get(4)->linesIn, 2UL);
	EXPECT_EQ(s.get(4)->bytesIn, 30UL);
	EXPECT_EQ(s.get(4)->linesOut, 0UL);

	ASSERT_TRUE(s.get(7) != NULL);
	EXPECT_EQ(s.get(7)->linesOut, 1UL);
	EXPECT_EQ(s.get(7)->bytesOut, 5UL);

	EXPECT_EQ(s.totals().linesIn, 2UL);
	EXPECT_EQ(s.totals().bytesIn, 30UL);
	EXPECT_EQ(s.totals().linesOut, 1UL);
	EXPECT_EQ(s.sessionCount(), 2UL);
	EXPECT_EQ(s.liveCount(), 2u);
}

TEST(TrafficStats98, ClosingAKeyDropsItsCountersButNotItsContributionToTheTotal)
{
	libcpp98::TrafficStats s;
	s.open(3);
	s.countIn(3, 100);
	EXPECT_TRUE(s.close(3));

	EXPECT_TRUE(s.get(3) == NULL) << "the retired key is gone";
	EXPECT_EQ(s.liveCount(), 0u);
	/* The point of keeping both: "lines served since startup" must not
	 * fall every time somebody disconnects. */
	EXPECT_EQ(s.totals().linesIn, 1UL);
	EXPECT_EQ(s.totals().bytesIn, 100UL);
	EXPECT_EQ(s.sessionCount(), 1UL) << "a closed session still happened";

	EXPECT_FALSE(s.close(3)) << "closing twice reports the second as absent";
}

TEST(TrafficStats98, ReopeningAReusedDescriptorStartsFromZero)
{
	libcpp98::TrafficStats s;
	s.open(5);
	s.countIn(5, 40);
	s.close(5);
	s.open(5);  /* the kernel handed the same fd number to a new peer */

	ASSERT_TRUE(s.get(5) != NULL);
	EXPECT_EQ(s.get(5)->bytesIn, 0UL) << "the new peer must not inherit the old tally";
	EXPECT_EQ(s.totals().bytesIn, 40UL) << "but the lifetime total keeps it";
	EXPECT_EQ(s.sessionCount(), 2UL);
}

TEST(TrafficStats98, CountingAnUnopenedKeyStillRecordsTheBytes)
{
	libcpp98::TrafficStats s;
	s.countIn(9, 7);  /* no open() first */
	ASSERT_TRUE(s.get(9) != NULL);
	EXPECT_EQ(s.get(9)->bytesIn, 7UL) << "bytes are never silently dropped";
	EXPECT_EQ(s.sessionCount(), 0UL) << "though the session was never announced";
}

/* ════════════════════════════════════════════════════════════════════════
 * ExpiringRegistry
 * ════════════════════════════════════════════════════════════════════ */

namespace {
struct Job {
	std::string name;
	Job() : name() {}
	explicit Job(const std::string &n) : name(n) {}
};
}  // namespace

TEST(ExpiringRegistry98, IdsAreMonotonicAndNeverReused)
{
	libcpp98::ExpiringRegistry<Job> reg;
	const long a = reg.add(Job("a"), 1000);
	const long b = reg.add(Job("b"), 1000);
	EXPECT_EQ(a, 1L) << "id 0 is never allocated, so it is free as a sentinel";
	EXPECT_EQ(b, 2L);

	EXPECT_TRUE(reg.erase(a));
	const long c = reg.add(Job("c"), 1000);
	EXPECT_EQ(c, 3L) << "a freed id must not come back: a peer replaying a "
					 << "stale id would otherwise reach somebody else's entry";
	EXPECT_TRUE(reg.find(a) == NULL);
}

TEST(ExpiringRegistry98, FindAndTouchReportAbsence)
{
	libcpp98::ExpiringRegistry<Job> reg;
	const long id = reg.add(Job("x"), 100);

	ASSERT_TRUE(reg.find(id) != NULL);
	EXPECT_EQ(reg.find(id)->name, "x");
	reg.find(id)->name = "mutated";
	EXPECT_EQ(reg.find(id)->name, "mutated") << "find() hands back a live reference";

	EXPECT_TRUE(reg.find(999) == NULL);
	EXPECT_FALSE(reg.touch(999, 100)) << "touching a vanished entry is reported, not ignored";
	EXPECT_FALSE(reg.erase(999));
	EXPECT_EQ(reg.size(), 1u);
}

TEST(ExpiringRegistry98, CollectExpiredReportsAndTouchReprieves)
{
	libcpp98::ExpiringRegistry<Job> reg;
	const long old1 = reg.add(Job("old1"), 100);
	const long old2 = reg.add(Job("old2"), 100);
	const long fresh = reg.add(Job("fresh"), 100);

	EXPECT_TRUE(reg.touch(fresh, 160));

	std::vector<long> expired;
	reg.collectExpired(170, 60, expired);  /* idle strictly more than 60 */

	ASSERT_EQ(expired.size(), 2u);
	EXPECT_EQ(expired[0], old1) << "ascending id order";
	EXPECT_EQ(expired[1], old2);
	EXPECT_EQ(reg.size(), 3u) << "collectExpired reports only; it erases nothing";

	/* Boundary: strictly greater, so exactly-at-the-timeout survives. */
	std::vector<long> atBoundary;
	reg.collectExpired(160, 60, atBoundary);
	EXPECT_EQ(atBoundary.size(), 0u) << "idle == timeout is not yet expired";
}

TEST(ExpiringRegistry98, TheSweepSurvivesErasingEveryEntryItReported)
{
	/* This is the hazard the class exists to remove: the caller acts on the
	 * reported ids with no live iterator into the map. */
	libcpp98::ExpiringRegistry<Job> reg;
	for (int i = 0; i < 16; ++i) reg.add(Job("j"), 100);

	std::vector<long> expired;
	reg.collectExpired(1000, 60, expired);
	ASSERT_EQ(expired.size(), 16u);

	for (std::size_t i = 0; i < expired.size(); ++i) EXPECT_TRUE(reg.erase(expired[i]));
	EXPECT_TRUE(reg.empty());

	/* collectExpired appends rather than clearing, so a second sweep into
	 * the same vector must leave what was already there. */
	reg.collectExpired(1000, 60, expired);
	EXPECT_EQ(expired.size(), 16u);
}
