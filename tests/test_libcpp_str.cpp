/* ─── Unit tests: vendor/libcpp str utilities ───
 *
 * libcpp has no test suite of its own; ft_irc is its only real consumer, so
 * this is where its string layer is exercised. Everything here is compiled
 * into ircserv under -std=c++98, so these also guard the C++98-cleanliness
 * of that tier by simply existing in the build.
 */

#include <gtest/gtest.h>
#include "PostMan.hpp"

#include "libcpp/str/format.hpp"
#include "libcpp/str/case.hpp"

#include <cerrno>
#include <climits>
#include <string>
#include <vector>

/* ════════════════════════════════════════════════════════════════════════
 * split — fixed-array form
 * ════════════════════════════════════════════════════════════════════ */

TEST(LibcppSplit, ArrayFormSplitsWithRoomToSpare)
{
	std::string out[8];
	int n = libcpp::str::split("a,b,c", ',', out, 8);
	ASSERT_EQ(n, 3);
	EXPECT_EQ(out[0], "a");
	EXPECT_EQ(out[1], "b");
	EXPECT_EQ(out[2], "c");
}

TEST(LibcppSplit, ArrayFormBoundsFieldCountWithoutLosingData)
{
	/* Was a silent data-loss bug: hitting `max` stopped the loop and the
	 * remainder was never written, so "c" and "d" vanished with no error.
	 * A bound may shorten the field list; it must not discard input. */
	std::string out[2];
	int n = libcpp::str::split("a,b,c,d", ',', out, 2);
	ASSERT_EQ(n, 2);
	EXPECT_EQ(out[0], "a");
	EXPECT_EQ(out[1], "b,c,d") << "the tail was dropped instead of kept";
}

TEST(LibcppSplit, ArrayFormHandlesDegenerateBounds)
{
	std::string out[4];
	EXPECT_EQ(libcpp::str::split("a,b", ',', out, 0), 0);
	int n = libcpp::str::split("a,b,c", ',', out, 1);
	ASSERT_EQ(n, 1);
	EXPECT_EQ(out[0], "a,b,c") << "a single slot holds the whole input";
}

/* ════════════════════════════════════════════════════════════════════════
 * split — vector forms
 * ════════════════════════════════════════════════════════════════════ */

TEST(LibcppSplit, VectorFormKeepsEveryField)
{
	std::vector<std::string> p = libcpp::str::split("a,b,c", ',');
	ASSERT_EQ(p.size(), 3u);
	EXPECT_EQ(p[0], "a");
	EXPECT_EQ(p[2], "c");
}

TEST(LibcppSplit, VectorFormPreservesEmptyFields)
{
	/* n delimiters => n + 1 fields, empties included. */
	std::vector<std::string> p = libcpp::str::split("a,,b", ',');
	ASSERT_EQ(p.size(), 3u);
	EXPECT_EQ(p[1], "");

	std::vector<std::string> q = libcpp::str::split("a,", ',');
	ASSERT_EQ(q.size(), 2u);
	EXPECT_EQ(q[1], "");

	std::vector<std::string> r = libcpp::str::split("", ',');
	ASSERT_EQ(r.size(), 1u) << "no delimiter still yields one (empty) field";
	EXPECT_EQ(r[0], "");
}

TEST(LibcppSplit, NonemptyFormDropsBlanksOnly)
{
	std::vector<std::string> p = libcpp::str::split_nonempty(",#a,,#b,", ',');
	ASSERT_EQ(p.size(), 2u);
	EXPECT_EQ(p[0], "#a");
	EXPECT_EQ(p[1], "#b");

	EXPECT_TRUE(libcpp::str::split_nonempty("", ',').empty());
	EXPECT_TRUE(libcpp::str::split_nonempty(",,,", ',').empty());

	std::vector<std::string> one = libcpp::str::split_nonempty("solo", ',');
	ASSERT_EQ(one.size(), 1u);
	EXPECT_EQ(one[0], "solo");
}

TEST(LibcppSplit, NonemptyFormMatchesTheGetlineIdiomItReplaces)
{
	/* ft_irc previously used istringstream + getline(_,_,','), filtering
	 * empties by hand at five call sites. Same inputs, same answers. */
	static const char *kCases[] = { "#a,#b", "#a,,#b", ",#a", "#a,", "", "," };
	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
	{
		std::istringstream iss(kCases[i]);
		std::string tok;
		std::vector<std::string> expected;
		while (std::getline(iss, tok, ','))
		{
			if (!tok.empty())
				expected.push_back(tok);
		}
		EXPECT_EQ(libcpp::str::split_nonempty(kCases[i], ','), expected)
			<< "diverged from the getline idiom on '" << kCases[i] << "'";
	}
}

/* ════════════════════════════════════════════════════════════════════════
 * parse_long / parse_ulong
 * ════════════════════════════════════════════════════════════════════ */

TEST(LibcppParse, AcceptsPlainIntegers)
{
	long v = -1;
	ASSERT_TRUE(libcpp::str::parse_long("42", v));
	EXPECT_EQ(v, 42L);
	ASSERT_TRUE(libcpp::str::parse_long("-7", v));
	EXPECT_EQ(v, -7L);
	ASSERT_TRUE(libcpp::str::parse_long("0", v));
	EXPECT_EQ(v, 0L);
}

TEST(LibcppParse, RejectsAnythingThatIsNotWhollyANumber)
{
	long v = 999;
	EXPECT_FALSE(libcpp::str::parse_long("", v));
	EXPECT_FALSE(libcpp::str::parse_long("12abc", v)) << "trailing garbage";
	EXPECT_FALSE(libcpp::str::parse_long("abc", v));
	EXPECT_FALSE(libcpp::str::parse_long(" 12", v)) << "strtol skips leading space";
	EXPECT_FALSE(libcpp::str::parse_long("12 ", v));
	EXPECT_FALSE(libcpp::str::parse_long("+12", v)) << "strtol accepts a leading +";
	EXPECT_FALSE(libcpp::str::parse_long("-", v)) << "a lone sign is not a number";
	EXPECT_FALSE(libcpp::str::parse_long("1.5", v));
	EXPECT_FALSE(libcpp::str::parse_long("0x10", v));
	EXPECT_EQ(v, 999) << "out must be untouched on every failure";
}

TEST(LibcppParse, RejectsOverflow)
{
	long v = 0;
	EXPECT_FALSE(libcpp::str::parse_long("99999999999999999999999", v));
	unsigned long u = 0;
	EXPECT_FALSE(libcpp::str::parse_ulong("99999999999999999999999", u));
}

TEST(LibcppParse, EnforcesTheRequestedRange)
{
	long v = 0;
	EXPECT_TRUE(libcpp::str::parse_long("500", 1, 65535, v));
	EXPECT_EQ(v, 500L);
	EXPECT_FALSE(libcpp::str::parse_long("0", 1, 65535, v));
	EXPECT_FALSE(libcpp::str::parse_long("65536", 1, 65535, v));
	EXPECT_TRUE(libcpp::str::parse_long("1", 1, 65535, v));
	EXPECT_TRUE(libcpp::str::parse_long("65535", 1, 65535, v));
	EXPECT_EQ(v, 65535L) << "bounds are inclusive";
}

TEST(LibcppParse, UnsignedRefusesNegativesRatherThanWrapping)
{
	/* The reason this wrapper exists: bare strtoul("-1") succeeds and yields
	 * ULONG_MAX, so a size or count check downstream sees a huge value
	 * instead of an error. */
	unsigned long u = 12345;
	EXPECT_FALSE(libcpp::str::parse_ulong("-1", u));
	EXPECT_FALSE(libcpp::str::parse_ulong("-0", u));
	EXPECT_EQ(u, 12345UL) << "out untouched";

	ASSERT_TRUE(libcpp::str::parse_ulong("18446744073709551615", u));
	EXPECT_EQ(u, ULONG_MAX);
}

TEST(LibcppParse, LeavesErrnoAsItFoundIt)
{
	/* ft_irc's subject audit is hostile to stray errno state; a parse helper
	 * must not leak ERANGE (or clear a caller's errno) as a side effect. */
	errno = EACCES;
	long v = 0;
	EXPECT_FALSE(libcpp::str::parse_long("99999999999999999999999", v));
	EXPECT_EQ(errno, EACCES) << "errno clobbered by a failing parse";

	errno = EACCES;
	EXPECT_TRUE(libcpp::str::parse_long("5", v));
	EXPECT_EQ(errno, EACCES) << "errno clobbered by a succeeding parse";
	errno = 0;
}

/* ════════════════════════════════════════════════════════════════════════
 * ASCII case operations
 * ════════════════════════════════════════════════════════════════════ */

TEST(LibcppAsciiCase, FoldsOnlyAsciiLetters)
{
	EXPECT_EQ(libcpp::str::ascii_to_lower("ABC-123_[]\\^"), "abc-123_[]\\^");
	EXPECT_EQ(libcpp::str::ascii_to_upper("abc-123_[]\\^"), "ABC-123_[]\\^");
	EXPECT_EQ(libcpp::str::ascii_to_lower(""), "");
}

TEST(LibcppAsciiCase, LeavesNonAsciiBytesAlone)
{
	/* U+00C9 (É) is C3 89 in UTF-8. An ASCII fold must not touch either
	 * byte -- lowercasing the 0x89 continuation byte would corrupt it. */
	const std::string upper = "\xC3\x89";
	EXPECT_EQ(libcpp::str::ascii_to_lower(upper), upper);
	EXPECT_EQ(libcpp::str::ascii_to_upper(upper), upper);
}

TEST(LibcppAsciiCase, ComparesCaseInsensitivelyOverAsciiOnly)
{
	EXPECT_TRUE(libcpp::str::eq_ascii_nocase("Bob", "bob"));
	EXPECT_TRUE(libcpp::str::eq_ascii_nocase("BOB", "bob"));
	EXPECT_TRUE(libcpp::str::eq_ascii_nocase("", ""));
	EXPECT_FALSE(libcpp::str::eq_ascii_nocase("bob", "bobby")) << "length first";
	EXPECT_FALSE(libcpp::str::eq_ascii_nocase("bob", "bub"));
	/* IRC nicks legally contain these; they must not be folded into letters */
	EXPECT_TRUE(libcpp::str::eq_ascii_nocase("[Nick]", "[nick]"));
	EXPECT_FALSE(libcpp::str::eq_ascii_nocase("[nick]", "{nick}"));
}

TEST(LibcppAsciiCase, DiffersFromTheUnicodeAwareComparisonOnPurpose)
{
	/* É vs é. The Unicode-aware eq_nocase folds them together; the ASCII one
	 * must not, because protocols that specify ascii casemapping (IRC) treat
	 * them as distinct names. Collapsing them would let one user answer to
	 * another's nick. */
	const std::string upper = "\xC3\x89";  /* É */
	const std::string lower = "\xC3\xA9";  /* é */
	EXPECT_FALSE(libcpp::str::eq_ascii_nocase(upper, lower))
		<< "ascii fold must keep non-ASCII letters distinct";
	EXPECT_NE(libcpp::str::eq_nocase(upper, lower),
			  libcpp::str::eq_ascii_nocase(upper, lower))
		<< "the two comparisons are supposed to disagree here";
}
