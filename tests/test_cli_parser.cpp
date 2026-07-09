/**
 * @file tests/test_cli_parser.cpp
 * @brief CLI 解析模块白盒单元测试 — gtest 框架
 *
 * 测试策略：
 *   1. 语句覆盖 (Statement Coverage) — 每一条可执行语句至少执行一次
 *   2. 分支覆盖 (Branch/Decision Coverage) — 每一个 bool 分支取真/假各至少一次
 *   3. 条件覆盖 (Condition Coverage) — 复合条件的每个原子条件独立取真/假
 *   4. 边界值分析 (Boundary Value Analysis) — 空字符串、数值边界、重复选项
 *   5. 错误路径覆盖 (Error-path Coverage) — 所有 return false 路径
 *
 * 覆盖模块：cbackup::parse_args(), cbackup::print_usage()
 */

#include <gtest/gtest.h>
#include "cli/cli_parser.h"

#include <cstring>
#include <string>
#include <vector>

using namespace cbackup;

// ---------------------------------------------------------------------------
// 测试辅助函数：将可变参数构造成 argc/argv
// ---------------------------------------------------------------------------
struct ArgvBuilder {
    std::vector<std::string> args_storage;
    std::vector<char*>        argv_storage;

    explicit ArgvBuilder(const std::string& prog = "cbackup") {
        args_storage.push_back(prog);
    }

    ArgvBuilder& add(const std::string& arg) {
        args_storage.push_back(arg);
        return *this;
    }

    int argc() const { return static_cast<int>(args_storage.size()); }

    char** argv() {
        argv_storage.clear();
        for (auto& s : args_storage)
            argv_storage.push_back(const_cast<char*>(s.c_str()));
        return argv_storage.data();
    }
};

// ===========================================================================
// 1. 错误路径测试 (Error-path Coverage)
// ===========================================================================

TEST(CliParser, NoArguments_ReturnsFalse) {
    // 边界：argc == 1（只有程序名）
    ArgvBuilder b;
    CliArgs args;
    EXPECT_FALSE(parse_args(b.argc(), b.argv(), args));
}

TEST(CliParser, UnknownSubcommand_ReturnsFalse) {
    // 边界：未知子命令
    ArgvBuilder b;
    b.add("invalid_cmd");
    CliArgs args;
    EXPECT_FALSE(parse_args(b.argc(), b.argv(), args));
}

TEST(CliParser, UnknownOption_ReturnsFalse) {
    // 边界：合法子命令但未知选项
    ArgvBuilder b;
    b.add("backup").add("--bogus");
    CliArgs args;
    EXPECT_FALSE(parse_args(b.argc(), b.argv(), args));
}

TEST(CliParser, MissingValue_ReturnsFalse) {
    // 边界：需要值的选项但没有提供值
    ArgvBuilder b;
    b.add("backup").add("--source");  // 缺少值
    CliArgs args;
    EXPECT_FALSE(parse_args(b.argc(), b.argv(), args));
}

TEST(CliParser, BackupMissingRequiredArgs_ReturnsFalse) {
    // 分支：backup 缺少 --source
    ArgvBuilder b;
    b.add("backup").add("--source").add("/src");  // 缺少 --dest
    CliArgs args;
    EXPECT_FALSE(parse_args(b.argc(), b.argv(), args));
}

TEST(CliParser, PackMissingDest_ReturnsFalse) {
    // 分支：pack 缺少 --dest
    ArgvBuilder b;
    b.add("pack").add("--source").add("/src");  // 缺 --dest
    CliArgs args;
    EXPECT_FALSE(parse_args(b.argc(), b.argv(), args));
}

TEST(CliParser, UnpackMissingFile_ReturnsFalse) {
    // 分支：unpack 缺少 --file
    ArgvBuilder b;
    b.add("unpack").add("--dest").add("/dst");
    CliArgs args;
    EXPECT_FALSE(parse_args(b.argc(), b.argv(), args));
}

TEST(CliParser, DaemonMissingWatch_ReturnsFalse) {
    // 分支：daemon 缺少 --watch
    ArgvBuilder b;
    b.add("daemon").add("--sync-to").add("/dst");
    CliArgs args;
    EXPECT_FALSE(parse_args(b.argc(), b.argv(), args));
}

TEST(CliParser, CronMissingSchedule_ReturnsFalse) {
    // 分支：cron 缺少 --schedule
    ArgvBuilder b;
    b.add("cron").add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    EXPECT_FALSE(parse_args(b.argc(), b.argv(), args));
}

// ===========================================================================
// 2. 子命令路由测试 (Branch Coverage for subcommand switch)
// ===========================================================================

TEST(CliParser, SubcommandBackup) {
    ArgvBuilder b;
    b.add("backup").add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.subcmd, SubCommand::BACKUP);
    EXPECT_EQ(args.source, "/src");
    EXPECT_EQ(args.dest, "/dst");
}

TEST(CliParser, SubcommandRestore) {
    ArgvBuilder b;
    b.add("restore").add("--source").add("/bak").add("--dest").add("/out");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.subcmd, SubCommand::RESTORE);
    EXPECT_EQ(args.source, "/bak");
    EXPECT_EQ(args.dest, "/out");
}

TEST(CliParser, SubcommandPack) {
    ArgvBuilder b;
    b.add("pack").add("--source").add("/src").add("--dest").add("/out.cbk");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.subcmd, SubCommand::PACK);
    EXPECT_EQ(args.source, "/src");
    EXPECT_EQ(args.dest, "/out.cbk");
}

TEST(CliParser, SubcommandUnpack) {
    ArgvBuilder b;
    b.add("unpack").add("--file").add("/in.cbk").add("--dest").add("/out");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.subcmd, SubCommand::UNPACK);
    EXPECT_EQ(args.archive_file, "/in.cbk");
    EXPECT_EQ(args.dest, "/out");
}

TEST(CliParser, SubcommandDaemon) {
    ArgvBuilder b;
    b.add("daemon").add("--watch").add("/watch").add("--sync-to").add("/sync");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.subcmd, SubCommand::DAEMON);
    EXPECT_EQ(args.watch_dir, "/watch");
    EXPECT_EQ(args.sync_dest, "/sync");
}

TEST(CliParser, SubcommandCron) {
    ArgvBuilder b;
    b.add("cron").add("--schedule").add("0 2 * * *")
     .add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.subcmd, SubCommand::CRON);
    EXPECT_EQ(args.cron_schedule, "0 2 * * *");
    EXPECT_EQ(args.keep, 5);  // default
}

// ===========================================================================
// 3. 压缩/加密选项测试 (EX-05 / EX-06)
// ===========================================================================

TEST(CliParser, CompressZlibFlag) {
    // -z 简写 → compress=true, compress_algo="zlib"
    ArgvBuilder b;
    b.add("pack").add("-z").add("--source").add("/src").add("--dest").add("/o.cbk");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_TRUE(args.compress);
    EXPECT_EQ(args.compress_algo, "zlib");
}

TEST(CliParser, CompressHuffman) {
    // --compress huffman 完整形式
    ArgvBuilder b;
    b.add("pack").add("--compress").add("huffman")
     .add("--source").add("/src").add("--dest").add("/o.cbk");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_TRUE(args.compress);
    EXPECT_EQ(args.compress_algo, "huffman");
}

TEST(CliParser, EncryptRc4) {
    ArgvBuilder b;
    b.add("pack").add("--encrypt").add("rc4").add("-p").add("secret")
     .add("--source").add("/src").add("--dest").add("/o.cbk");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.encrypt_algo, "rc4");
    EXPECT_EQ(args.password, "secret");
}

TEST(CliParser, EncryptAes) {
    // --password 完整形式
    ArgvBuilder b;
    b.add("pack").add("--encrypt").add("aes").add("--password").add("p@ss!")
     .add("--source").add("/src").add("--dest").add("/o.cbk");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.encrypt_algo, "aes");
    EXPECT_EQ(args.password, "p@ss!");
}

TEST(CliParser, EncryptWithoutPassword_AcceptedByParser) {
    // 条件：解析器只负责解析，不验证密码是否提供（由 main.cpp 验证）
    ArgvBuilder b;
    b.add("pack").add("--encrypt").add("rc4")
     .add("--source").add("/src").add("--dest").add("/o.cbk");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.encrypt_algo, "rc4");
    EXPECT_TRUE(args.password.empty());
}

// ===========================================================================
// 4. 6维过滤选项测试 (EX-03) — 每个维度一个测试用例
// ===========================================================================

TEST(CliParser, FilterByIncludeExclude) {
    // 维度 1+2: 名称 & 路径
    ArgvBuilder b;
    b.add("backup").add("--include").add("*.cpp").add("--include").add("*.h")
     .add("--exclude").add(".git/*").add("--exclude").add("*.tmp")
     .add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    ASSERT_EQ(args.include_patterns.size(), 2u);
    EXPECT_EQ(args.include_patterns[0], "*.cpp");
    EXPECT_EQ(args.include_patterns[1], "*.h");
    ASSERT_EQ(args.exclude_patterns.size(), 2u);
    EXPECT_EQ(args.exclude_patterns[0], ".git/*");
    EXPECT_EQ(args.exclude_patterns[1], "*.tmp");
}

TEST(CliParser, FilterByType) {
    // 维度 3: 类型
    ArgvBuilder b;
    b.add("backup").add("--type").add("regular,symlink,dir")
     .add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.type_filter, "regular,symlink,dir");
}

TEST(CliParser, FilterByMtime) {
    // 维度 4: 修改时间
    ArgvBuilder b;
    b.add("backup").add("--mtime").add("-7d")
     .add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.mtime_filter, "-7d");
}

TEST(CliParser, FilterBySize) {
    // 维度 5: 文件大小
    ArgvBuilder b;
    b.add("backup").add("--size").add("<500M")
     .add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.size_filter, "<500M");
}

TEST(CliParser, FilterByOwner) {
    // 维度 6: 属主 (--owner)
    ArgvBuilder b;
    b.add("backup").add("--owner").add("root")
     .add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.owner_filter, "root");
}

TEST(CliParser, FilterByUserAlias) {
    // 条件：--user 是 --owner 的别名
    ArgvBuilder b;
    b.add("backup").add("--user").add("1000")
     .add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.owner_filter, "1000");
}

TEST(CliParser, FilterAll6Dimensions) {
    // 条件组合：全部 6 个维度同时使用
    ArgvBuilder b;
    b.add("pack")
     .add("--include").add("*.cpp")
     .add("--exclude").add("build/*")
     .add("--type").add("regular,symlink")
     .add("--mtime").add("-30d")
     .add("--size").add("<100M")
     .add("--owner").add("root")
     .add("--source").add("/src")
     .add("--dest").add("/out.cbk");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.include_patterns.size(), 1u);
    EXPECT_EQ(args.exclude_patterns.size(), 1u);
    EXPECT_EQ(args.type_filter, "regular,symlink");
    EXPECT_EQ(args.mtime_filter, "-30d");
    EXPECT_EQ(args.size_filter, "<100M");
    EXPECT_EQ(args.owner_filter, "root");
}

// ===========================================================================
// 5. 布尔标志测试 (Boolean Flag Coverage)
// ===========================================================================

TEST(CliParser, FlagPreserveMetadata_m) {
    ArgvBuilder b;
    b.add("backup").add("-m").add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_TRUE(args.preserve_metadata);
}

TEST(CliParser, FlagPreserveMetadata_Long) {
    ArgvBuilder b;
    b.add("backup").add("--metadata").add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_TRUE(args.preserve_metadata);
}

TEST(CliParser, FlagVerbose_v) {
    ArgvBuilder b;
    b.add("backup").add("-v").add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_TRUE(args.verbose);
}

TEST(CliParser, FlagVerboseLong) {
    ArgvBuilder b;
    b.add("backup").add("--verbose").add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_TRUE(args.verbose);
}

TEST(CliParser, FlagsCombined) {
    // -v 和 --verbose 的效果等价
    ArgvBuilder b1;
    b1.add("backup").add("-v").add("--source").add("/src").add("--dest").add("/dst");
    CliArgs a1;
    parse_args(b1.argc(), b1.argv(), a1);

    ArgvBuilder b2;
    b2.add("backup").add("--verbose").add("--source").add("/src").add("--dest").add("/dst");
    CliArgs a2;
    parse_args(b2.argc(), b2.argv(), a2);

    EXPECT_EQ(a1.verbose, a2.verbose);
}

// ===========================================================================
// 6. 数值与边界测试 (Boundary Value Analysis)
// ===========================================================================

TEST(CliParser, KeepDefaultValue) {
    ArgvBuilder b;
    b.add("cron").add("--schedule").add("* * * * *")
     .add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.keep, 5);  // 默认值
}

TEST(CliParser, KeepZero) {
    ArgvBuilder b;
    b.add("cron").add("--schedule").add("* * * * *")
     .add("--keep").add("0")
     .add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.keep, 0);
}

TEST(CliParser, KeepLargeValue) {
    ArgvBuilder b;
    b.add("cron").add("--schedule").add("* * * * *")
     .add("--keep").add("999")
     .add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.keep, 999);
}

TEST(CliParser, DefaultSpecialFiles) {
    ArgvBuilder b;
    b.add("backup").add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_TRUE(args.special_files);  // 默认启用
}

// ===========================================================================
// 7. 选项顺序无关性测试 (Robustness)
// ===========================================================================

TEST(CliParser, OptionOrderDoesNotMatter) {
    ArgvBuilder b1;
    b1.add("pack").add("--dest").add("/o.cbk").add("--source").add("/src");
    CliArgs a1;
    parse_args(b1.argc(), b1.argv(), a1);

    ArgvBuilder b2;
    b2.add("pack").add("--source").add("/src").add("--dest").add("/o.cbk");
    CliArgs a2;
    parse_args(b2.argc(), b2.argv(), a2);

    EXPECT_EQ(a1.source, a2.source);
    EXPECT_EQ(a1.dest, a2.dest);
}

// ===========================================================================
// 8. 默认值测试 (Default Value Coverage)
// ===========================================================================

TEST(CliParser, DefaultValues) {
    ArgvBuilder b;
    b.add("backup").add("--source").add("/src").add("--dest").add("/dst");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_FALSE(args.compress);
    EXPECT_FALSE(args.preserve_metadata);
    EXPECT_TRUE(args.special_files);
    EXPECT_FALSE(args.verbose);
    EXPECT_TRUE(args.compress_algo.empty());
    EXPECT_TRUE(args.encrypt_algo.empty());
    EXPECT_TRUE(args.password.empty());
}

// ===========================================================================
// 9. 额外 daemon 选项测试
// ===========================================================================

TEST(CliParser, DaemonWithLog) {
    ArgvBuilder b;
    b.add("daemon").add("--watch").add("/w").add("--sync-to").add("/s")
     .add("--log").add("/var/log/cb.log").add("--verbose");
    CliArgs args;
    ASSERT_TRUE(parse_args(b.argc(), b.argv(), args));
    EXPECT_EQ(args.watch_dir, "/w");
    EXPECT_EQ(args.sync_dest, "/s");
    EXPECT_EQ(args.log_file, "/var/log/cb.log");
    EXPECT_TRUE(args.verbose);
}

// ===========================================================================
// 10. print_usage 覆盖 (不崩溃即通过)
// ===========================================================================

TEST(CliParser, PrintUsage_DoesNotCrash) {
    // 语句覆盖：确保 print_usage 正常执行
    EXPECT_NO_THROW(print_usage("cbackup"));
    EXPECT_NO_THROW(print_usage(nullptr));
}
