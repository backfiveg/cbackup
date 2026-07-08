#include <gtest/gtest.h>
#include "backup/backup.h"
#include "utils/path_utils.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cbackup;
namespace fs = std::filesystem;

class BackupTest : public ::testing::Test {
 protected:
    std::string src_dir_;
    std::string dst_dir_;
    std::string base_;

    void SetUp() override {
        base_    = "/tmp/cbtest_" + std::to_string(getpid());
        src_dir_ = base_ + "/src";
        dst_dir_ = base_ + "/dst";
        path_utils::mkdir_p(src_dir_ + "/subdir");

        write_file(src_dir_ + "/hello.txt", "Hello, World!");
        write_file(src_dir_ + "/subdir/nested.cpp", "#include <iostream>");
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(base_, ec);
    }

    static void write_file(const std::string& path, const std::string& content) {
        std::ofstream f(path);
        f << content;
    }

    static std::string read_file(const std::string& path) {
        std::ifstream f(path);
        return std::string(std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>());
    }
};

TEST_F(BackupTest, BasicBackup) {
    BackupOptions opts;
    opts.source = src_dir_;
    opts.dest   = dst_dir_;

    auto stats = backup(opts);
    EXPECT_EQ(stats.exit_code, 0);
    EXPECT_GE(stats.files_ok, 2u);

    EXPECT_EQ(read_file(dst_dir_ + "/hello.txt"), "Hello, World!");
    EXPECT_EQ(read_file(dst_dir_ + "/subdir/nested.cpp"), "#include <iostream>");
}

TEST_F(BackupTest, BackupWithFilter) {
    BackupOptions opts;
    opts.source = src_dir_;
    opts.dest   = dst_dir_;
    opts.filter.include_patterns = {"*.cpp"};

    auto stats = backup(opts);
    EXPECT_EQ(stats.exit_code, 0);

    EXPECT_TRUE(path_utils::exists(dst_dir_ + "/subdir/nested.cpp"));
    EXPECT_FALSE(path_utils::exists(dst_dir_ + "/hello.txt"));
}

TEST_F(BackupTest, InvalidSourceFails) {
    BackupOptions opts;
    opts.source = "/nonexistent/path_xyz";
    opts.dest   = dst_dir_;

    auto stats = backup(opts);
    EXPECT_NE(stats.exit_code, 0);
}

TEST_F(BackupTest, RestoreFromBackup) {
    BackupOptions bopts;
    bopts.source = src_dir_;
    bopts.dest   = dst_dir_;
    backup(bopts);

    std::string restore_dir = base_ + "/restore";
    RestoreOptions ropts;
    ropts.source = dst_dir_;
    ropts.dest   = restore_dir;

    auto stats = cbackup::restore_dir(ropts);
    EXPECT_EQ(stats.exit_code, 0);
    EXPECT_GE(stats.files_restored, 2u);

    EXPECT_EQ(read_file(restore_dir + "/hello.txt"), "Hello, World!");
    // restore_dir is cleaned up in TearDown via base_ removal
}
