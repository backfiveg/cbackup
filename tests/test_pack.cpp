#include <gtest/gtest.h>
#include "pack/packer.h"
#include "utils/path_utils.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace cbackup;
namespace fs = std::filesystem;

class PackTest : public ::testing::Test {
 protected:
    std::string base_;
    std::string src_dir_;
    std::string archive_;
    std::string unpack_dir_;

    void SetUp() override {
        base_       = "/tmp/cbpack_" + std::to_string(getpid());
        src_dir_    = base_ + "/src";
        archive_    = base_ + "/test.cbk";
        unpack_dir_ = base_ + "/out";
        path_utils::mkdir_p(src_dir_ + "/sub");

        write_file(src_dir_ + "/a.txt", "content A");
        write_file(src_dir_ + "/sub/b.txt", "content B");
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(base_, ec);
    }

    static void write_file(const std::string& path, const std::string& content) {
        std::ofstream f(path);
        f << content;
    }

    static std::string read_file_str(const std::string& path) {
        std::ifstream f(path);
        return std::string(std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>());
    }
};

TEST_F(PackTest, PackAndUnpack) {
    PackOptions popts;
    popts.source = src_dir_;
    popts.dest   = archive_;

    auto pstats = pack(popts);
    EXPECT_EQ(pstats.exit_code, 0);
    EXPECT_TRUE(path_utils::exists(archive_));

    UnpackOptions uopts;
    uopts.archive = archive_;
    uopts.dest    = unpack_dir_;

    auto ustats = unpack(uopts);
    EXPECT_EQ(ustats.exit_code, 0);

    EXPECT_EQ(read_file_str(unpack_dir_ + "/a.txt"), "content A");
    EXPECT_EQ(read_file_str(unpack_dir_ + "/sub/b.txt"), "content B");
}

TEST_F(PackTest, PackCompressedAndUnpack) {
    PackOptions popts;
    popts.source   = src_dir_;
    popts.dest     = archive_;
    popts.compress = true;

    auto pstats = pack(popts);
    EXPECT_EQ(pstats.exit_code, 0);

    UnpackOptions uopts;
    uopts.archive = archive_;
    uopts.dest    = unpack_dir_;

    auto ustats = unpack(uopts);
    EXPECT_EQ(ustats.exit_code, 0);

    EXPECT_EQ(read_file_str(unpack_dir_ + "/a.txt"), "content A");
    EXPECT_EQ(read_file_str(unpack_dir_ + "/sub/b.txt"), "content B");
}

TEST_F(PackTest, UnpackInvalidArchiveFails) {
    write_file(archive_, "this is not a valid archive!!!");

    UnpackOptions uopts;
    uopts.archive = archive_;
    uopts.dest    = unpack_dir_;

    auto ustats = unpack(uopts);
    EXPECT_NE(ustats.exit_code, 0);
}

TEST_F(PackTest, UnpackNonexistentFails) {
    UnpackOptions uopts;
    uopts.archive = "/nonexistent/file.cbk";
    uopts.dest    = unpack_dir_;

    auto ustats = unpack(uopts);
    EXPECT_NE(ustats.exit_code, 0);
}

TEST_F(PackTest, PackHuffmanAndUnpack) {
    PackOptions popts;
    popts.source       = src_dir_;
    popts.dest         = archive_;
    popts.compress_algo = compress::Algorithm::HUFFMAN;

    auto pstats = pack(popts);
    EXPECT_EQ(pstats.exit_code, 0);

    UnpackOptions uopts;
    uopts.archive = archive_;
    uopts.dest    = unpack_dir_;
    auto ustats = unpack(uopts);
    EXPECT_EQ(ustats.exit_code, 0);

    EXPECT_EQ(read_file_str(unpack_dir_ + "/a.txt"), "content A");
    EXPECT_EQ(read_file_str(unpack_dir_ + "/sub/b.txt"), "content B");
}

TEST_F(PackTest, PackEncryptedRc4AndUnpack) {
    PackOptions popts;
    popts.source     = src_dir_;
    popts.dest       = archive_;
    popts.cipher_algo = crypto::Algorithm::RC4;
    popts.password   = "secret123";

    auto pstats = pack(popts);
    EXPECT_EQ(pstats.exit_code, 0);

    UnpackOptions uopts;
    uopts.archive  = archive_;
    uopts.dest     = unpack_dir_;
    uopts.password = "secret123";
    auto ustats = unpack(uopts);
    EXPECT_EQ(ustats.exit_code, 0);

    EXPECT_EQ(read_file_str(unpack_dir_ + "/a.txt"), "content A");
}

TEST_F(PackTest, PackHuffmanPlusAesAndUnpack) {
    PackOptions popts;
    popts.source        = src_dir_;
    popts.dest          = archive_;
    popts.compress_algo = compress::Algorithm::HUFFMAN;
    popts.cipher_algo   = crypto::Algorithm::AES128;
    popts.password      = "topsecret";

    auto pstats = pack(popts);
    EXPECT_EQ(pstats.exit_code, 0);

    UnpackOptions uopts;
    uopts.archive  = archive_;
    uopts.dest     = unpack_dir_;
    uopts.password = "topsecret";
    auto ustats = unpack(uopts);
    EXPECT_EQ(ustats.exit_code, 0);

    EXPECT_EQ(read_file_str(unpack_dir_ + "/a.txt"), "content A");
    EXPECT_EQ(read_file_str(unpack_dir_ + "/sub/b.txt"), "content B");
}

TEST_F(PackTest, EncryptedArchiveNeedsPassword) {
    PackOptions popts;
    popts.source     = src_dir_;
    popts.dest       = archive_;
    popts.cipher_algo = crypto::Algorithm::RC4;
    popts.password   = "pw";
    ASSERT_EQ(pack(popts).exit_code, 0);

    // Unpack without password should fail.
    UnpackOptions uopts;
    uopts.archive = archive_;
    uopts.dest    = unpack_dir_;
    EXPECT_NE(unpack(uopts).exit_code, 0);
}

TEST_F(PackTest, PackHardlink) {
    // Create a hard link in source
    std::string orig = src_dir_ + "/a.txt";
    std::string hlink = src_dir_ + "/a_hardlink.txt";
    ASSERT_EQ(link(orig.c_str(), hlink.c_str()), 0);

    PackOptions popts;
    popts.source = src_dir_;
    popts.dest   = archive_;

    auto pstats = pack(popts);
    EXPECT_EQ(pstats.exit_code, 0);

    UnpackOptions uopts;
    uopts.archive = archive_;
    uopts.dest    = unpack_dir_;
    auto ustats = unpack(uopts);
    EXPECT_EQ(ustats.exit_code, 0);

    // Both files should have same content
    EXPECT_EQ(read_file_str(unpack_dir_ + "/a.txt"),
              read_file_str(unpack_dir_ + "/a_hardlink.txt"));
}
