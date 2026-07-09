#include <gtest/gtest.h>
#include "filter/filter.h"

#include <sys/stat.h>
#include <ctime>

using namespace cbackup;

// Build a fake struct stat for the extended-dimension tests.
static struct stat make_stat(mode_t mode, off_t size, uid_t uid, time_t mtime) {
    struct stat st{};
    st.st_mode = mode;
    st.st_size = size;
    st.st_uid = uid;
    st.st_mtime = mtime;
    return st;
}

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

// ---- Extended 6-dimension filter (EX-03) ----

TEST(FilterEx, ByType) {
    FilterConfig cfg;
    cfg.type_mask = TYPE_REGULAR;  // only regular files
    Filter f(cfg);
    auto reg = make_stat(S_IFREG | 0644, 100, 0, 0);
    auto lnk = make_stat(S_IFLNK | 0777, 10, 0, 0);
    EXPECT_TRUE(f.should_include("a.txt", reg));
    EXPECT_FALSE(f.should_include("a.link", lnk));
}

TEST(FilterEx, BySizeLess) {
    FilterConfig cfg;
    cfg.has_size = true;
    cfg.size_less = true;
    cfg.size_threshold = 500;
    Filter f(cfg);
    EXPECT_TRUE(f.should_include("small", make_stat(S_IFREG | 0644, 100, 0, 0)));
    EXPECT_FALSE(f.should_include("big", make_stat(S_IFREG | 0644, 900, 0, 0)));
}

TEST(FilterEx, ByUser) {
    FilterConfig cfg;
    cfg.has_uid = true;
    cfg.uid = 1000;
    Filter f(cfg);
    EXPECT_TRUE(f.should_include("mine", make_stat(S_IFREG | 0644, 1, 1000, 0)));
    EXPECT_FALSE(f.should_include("theirs", make_stat(S_IFREG | 0644, 1, 0, 0)));
}

TEST(FilterEx, ByMtimeNewer) {
    FilterConfig cfg;
    cfg.has_mtime = true;
    cfg.mtime_newer = true;
    time_t now = time(nullptr);
    cfg.mtime_threshold = now - 7 * 24 * 3600;  // within last 7 days
    Filter f(cfg);
    EXPECT_TRUE(f.should_include("recent",
                make_stat(S_IFREG | 0644, 1, 0, now - 3600)));
    EXPECT_FALSE(f.should_include("old",
                make_stat(S_IFREG | 0644, 1, 0, now - 30 * 24 * 3600)));
}

TEST(FilterEx, CombinedDimensions) {
    FilterConfig cfg;
    cfg.include_patterns = {"*.log"};
    cfg.type_mask = TYPE_REGULAR;
    cfg.has_size = true; cfg.size_less = true; cfg.size_threshold = 1024;
    Filter f(cfg);
    EXPECT_TRUE(f.should_include("app.log", make_stat(S_IFREG | 0644, 500, 0, 0)));
    EXPECT_FALSE(f.should_include("app.log", make_stat(S_IFREG | 0644, 4096, 0, 0)));
    EXPECT_FALSE(f.should_include("app.txt", make_stat(S_IFREG | 0644, 500, 0, 0)));
}

// ---- CLI spec parsers ----

TEST(FilterParse, TypeMask) {
    uint32_t mask = 0;
    EXPECT_TRUE(parse_type_mask("regular,symlink", mask));
    EXPECT_TRUE(mask & TYPE_REGULAR);
    EXPECT_TRUE(mask & TYPE_SYMLINK);
    EXPECT_FALSE(parse_type_mask("bogus", mask));
}

TEST(FilterParse, SizeSpec) {
    bool less = false; uint64_t bytes = 0;
    EXPECT_TRUE(parse_size_filter("<500M", less, bytes));
    EXPECT_TRUE(less);
    EXPECT_EQ(bytes, 500ULL * 1024 * 1024);

    EXPECT_TRUE(parse_size_filter(">100K", less, bytes));
    EXPECT_FALSE(less);
    EXPECT_EQ(bytes, 100ULL * 1024);

    EXPECT_FALSE(parse_size_filter("500M", less, bytes));  // missing operator
}

TEST(FilterParse, MtimeSpec) {
    bool newer = false; time_t epoch = 0;
    time_t now = time(nullptr);
    EXPECT_TRUE(parse_mtime_filter("-7d", newer, epoch));
    EXPECT_TRUE(newer);
    EXPECT_NEAR(static_cast<double>(epoch),
                static_cast<double>(now - 7 * 24 * 3600), 5.0);

    EXPECT_TRUE(parse_mtime_filter("+30d", newer, epoch));
    EXPECT_FALSE(newer);
}
