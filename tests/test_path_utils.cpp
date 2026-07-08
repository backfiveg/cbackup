#include <gtest/gtest.h>
#include "utils/path_utils.h"
#include <sys/stat.h>
#include <cstdlib>

using namespace cbackup::path_utils;

TEST(PathUtils, Normalize) {
    EXPECT_EQ(normalize("/a/b/../c"), "/a/c");
    EXPECT_EQ(normalize("/a/./b"), "/a/b");
    EXPECT_EQ(normalize("a/b/c"), "a/b/c");
    EXPECT_EQ(normalize(""), ".");
    EXPECT_EQ(normalize("/"), "/");
}

TEST(PathUtils, Join) {
    EXPECT_EQ(join("/a/b", "c"), "/a/b/c");
    EXPECT_EQ(join("/a/b/", "c"), "/a/b/c");
    EXPECT_EQ(join("", "c"), "c");
}

TEST(PathUtils, ParentDir) {
    EXPECT_EQ(parent_dir("/a/b/c"), "/a/b");
    EXPECT_EQ(parent_dir("/a"), "/");
    EXPECT_EQ(parent_dir("file.txt"), ".");
}

TEST(PathUtils, RelativeTo) {
    EXPECT_EQ(relative_to("/src", "/src/a/b.txt"), "a/b.txt");
    EXPECT_EQ(relative_to("/src/", "/src/a/b.txt"), "a/b.txt");
}

TEST(PathUtils, ValidatePath) {
    EXPECT_TRUE(validate_path("/home/user/test.txt"));
    EXPECT_FALSE(validate_path(""));
    std::string bad_path = std::string("/tmp/") + '\0' + "bad";
    EXPECT_FALSE(validate_path(bad_path));
}

TEST(PathUtils, MkdirP) {
    std::string tmp = "/tmp/cbackup_test_mkdirp_" + std::to_string(getpid());
    std::string deep = tmp + "/a/b/c";
    EXPECT_TRUE(mkdir_p(deep));
    struct stat st;
    EXPECT_EQ(stat(deep.c_str(), &st), 0);
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    // Cleanup
    system(("rm -rf " + tmp).c_str());
}
