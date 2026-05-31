#include "common/logger.h"
#include <gtest/gtest.h>
#include <sstream>
#include <thread>
#include <vector>

// Sanity check that GTest is wired correctly
TEST(Sanity, OneEqualsOne) {
    EXPECT_EQ(1, 1);
}

// Verify logger outputs valid JSON with expected fields
TEST(Logger, OutputsJsonWithModuleAndEvent) {
    std::streambuf* old = std::cout.rdbuf();
    std::stringstream ss;
    std::cout.rdbuf(ss.rdbuf());

    logging::init("TEST_MOD");
    LOG_INFO("TEST_EVENT", {{"key1", "val1"}, {"key2", "val2"}});

    std::cout.rdbuf(old);

    std::string out = ss.str();
    EXPECT_NE(out.find("\"module\":\"TEST_MOD\""), std::string::npos);
    EXPECT_NE(out.find("\"event\":\"TEST_EVENT\""), std::string::npos);
    EXPECT_NE(out.find("\"level\":\"INFO\""), std::string::npos);
    EXPECT_NE(out.find("\"key1\":\"val1\""), std::string::npos);
    EXPECT_NE(out.find("\"key2\":\"val2\""), std::string::npos);
    EXPECT_NE(out.find("\"timestamp\""), std::string::npos);
    EXPECT_EQ(out.back(), '\n');
}

// Verify logger is thread-safe (no crash under contention)
TEST(Logger, ThreadSafeNoCrash) {
    logging::init("THREAD_TEST");

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < 50; ++i) {
                LOG_INFO("THREAD_EVENT",
                    {{"thread", std::to_string(t)}, {"seq", std::to_string(i)}});
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // If we get here without deadlock or crash, the test passes
    SUCCEED();
}
