#include <gtest/gtest.h>
#include "filter/filter.h"

using namespace cbackup;

TEST(Filter, NoFilter_AllPass) {
    FilterConfig cfg;
    Filter f(cfg);
    EXPECT_TRUE(f.should_include("main.cpp"));
    EXPECT_TRUE(f.should_include("build/output.o"));
    EXPECT_TRUE(f.should_include("docs/readme.md"));
}

TEST(Filter, ExcludePattern) {
    FilterConfig cfg;
    cfg.exclude_patterns = {"*.tmp", "*.o"};
    Filter f(cfg);
    EXPECT_FALSE(f.should_include("temp.tmp"));
    EXPECT_FALSE(f.should_include("obj/main.o"));
    EXPECT_TRUE(f.should_include("main.cpp"));
    EXPECT_TRUE(f.should_include("README.md"));
}

TEST(Filter, IncludePattern) {
    FilterConfig cfg;
    cfg.include_patterns = {"*.cpp", "*.h"};
    Filter f(cfg);
    EXPECT_TRUE(f.should_include("main.cpp"));
    EXPECT_TRUE(f.should_include("include/foo.h"));
    EXPECT_FALSE(f.should_include("CMakeLists.txt"));
    EXPECT_FALSE(f.should_include("build.sh"));
}

TEST(Filter, IncludeAndExclude) {
    FilterConfig cfg;
    cfg.include_patterns = {"*.cpp"};
    cfg.exclude_patterns = {"test_*.cpp"};
    Filter f(cfg);
    EXPECT_TRUE(f.should_include("main.cpp"));
    EXPECT_FALSE(f.should_include("test_backup.cpp"));
    EXPECT_FALSE(f.should_include("README.md"));
}

TEST(Filter, ExcludeDirectory) {
    FilterConfig cfg;
    cfg.exclude_patterns = {"build/*"};
    Filter f(cfg);
    EXPECT_FALSE(f.should_include("build/output"));
    EXPECT_TRUE(f.should_include("src/main.cpp"));
}
