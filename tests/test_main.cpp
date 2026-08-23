/* ─── Test Harness: bridges Google Test with PostMan visual reporting ─── */

#include <gtest/gtest.h>
#include "PostMan.hpp"

/*
 * PostManListener – a gtest TestEventListener that feeds every
 * test result into the PostMan TestReport singleton, so results
 * appear both in gtest's standard output AND in PostMan's styled
 * Unicode table printed at the very end.
 */
class PostManListener : public ::testing::EmptyTestEventListener
{
public:
	void OnTestSuiteStart(const ::testing::TestSuite &suite) override
	{
		TestReport::instance().beginSuite(suite.name());
	}

	void OnTestEnd(const ::testing::TestInfo &info) override
	{
		bool passed = info.result()->Passed();
		std::string label = info.name();
		TestReport::instance().record(label, passed);

		/* Real-time coloured output (PostMan style) */
		if (passed)
			std::cout << PM_PASS << PM_BOLD << "  [PASS] " << PM_RST
					  << info.test_suite_name() << "." << label << "\n";
		else
		{
			std::cerr << PM_FAIL << PM_BOLD << "  [FAIL] " << PM_RST
					  << info.test_suite_name() << "." << label << "\n";

			/* gtest's default printer is released in main() so PostMan owns
			** the console, and with it went the only thing that printed WHY
			** a test failed -- a red line with no expected/actual is not a
			** diagnosis. Replay the failing parts here so the table still
			** comes with the evidence behind it. */
			const ::testing::TestResult *res = info.result();
			for (int i = 0; i < res->total_part_count(); ++i)
			{
				const ::testing::TestPartResult &part = res->GetTestPartResult(i);
				if (!part.failed())
					continue;
				std::cerr << "         " << (part.file_name() ? part.file_name() : "?")
						  << ":" << part.line_number() << "\n"
						  << part.message() << "\n";
			}
		}
	}
};

int main(int argc, char **argv)
{
	/* test_runner does not link main.cpp, so the signal(SIGPIPE, SIG_IGN)
	 * that ircserv installs in production never runs here. Without it, a
	 * server-side send() to a socket the test has already close()d — while
	 * the server still has a large SendQ pending for it, as in the T6
	 * frozen-reader tests — kills the whole process with SIGPIPE instead of
	 * returning EPIPE. This aligns the test process's signal disposition
	 * with the shipped binary's. */
	signal(SIGPIPE, SIG_IGN);

	::testing::InitGoogleTest(&argc, argv);

	/* Attach PostMan listener (gtest owns the pointer) */
	::testing::TestEventListeners &listeners =
		::testing::UnitTest::GetInstance()->listeners();

	/* Suppress gtest's default console output — PostMan handles it */
	delete listeners.Release(listeners.default_result_printer());

	listeners.Append(new PostManListener());

	int result = RUN_ALL_TESTS();

	/* Print PostMan's styled summary table */
	TestReport::instance().print();

	return result;
}
