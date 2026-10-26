// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "tests/app_test.h"

#include "app/extensions.h"

#if !defined(_WIN32)
  #include "archive.h"
  #include "archive_entry.h"

  #include <atomic>
  #include <csignal>
  #include <filesystem>
  #include <fstream>
  #include <thread>

  #include <fcntl.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

using namespace app;

// An extension that is installed lands in a folder of its own inside
// the user's extensions folder, nowhere else

TEST(ExtensionPaths, ANameIsOneFolder)
{
  EXPECT_TRUE(is_valid_extension_name("nordtal-tools"));
  EXPECT_TRUE(is_valid_extension_name("my.extension"));
  EXPECT_FALSE(is_valid_extension_name(""));
  EXPECT_FALSE(is_valid_extension_name("."));
  EXPECT_FALSE(is_valid_extension_name(".."));
  EXPECT_FALSE(is_valid_extension_name("../data/extensions/x"));
  EXPECT_FALSE(is_valid_extension_name("a/b"));
  EXPECT_FALSE(is_valid_extension_name("a\\b"));
  EXPECT_FALSE(is_valid_extension_name("a..b"));
  EXPECT_FALSE(is_valid_extension_name("C:x"));
}

TEST(ExtensionPaths, AnArchiveEntryStaysInside)
{
  EXPECT_TRUE(is_safe_archive_entry("package.json"));
  EXPECT_TRUE(is_safe_archive_entry("scripts/a.lua"));
  EXPECT_TRUE(is_safe_archive_entry("scripts\\a.lua"));
  EXPECT_TRUE(is_safe_archive_entry("a..b/c"));
  EXPECT_FALSE(is_safe_archive_entry(""));
  EXPECT_FALSE(is_safe_archive_entry("../x.lua"));
  EXPECT_FALSE(is_safe_archive_entry("a/../../x.lua"));
  EXPECT_FALSE(is_safe_archive_entry("a\\..\\x.lua"));
  EXPECT_FALSE(is_safe_archive_entry("a/.."));
  EXPECT_FALSE(is_safe_archive_entry("/etc/x"));
  EXPECT_FALSE(is_safe_archive_entry("\\x"));
  EXPECT_FALSE(is_safe_archive_entry("C:/x"));
}

#if !defined(_WIN32)

// A zip with a file "a.lua" and, when withLink is true, a link "link"
// that points at the root folder
static std::string make_zip(bool withLink)
{
  std::vector<char> buf(64 * 1024);
  size_t used = 0;
  archive* a = archive_write_new();
  archive_write_set_format_zip(a);
  // The type of each entry in its local header, read from a pipe too
  archive_write_set_format_option(a, "zip", "experimental", "1");
  archive_write_open_memory(a, buf.data(), buf.size(), &used);
  archive_entry* e = archive_entry_new();
  archive_entry_set_pathname(e, "a.lua");
  archive_entry_set_filetype(e, AE_IFREG);
  archive_entry_set_perm(e, 0644);
  archive_entry_set_size(e, 2);
  archive_write_header(a, e);
  archive_write_data(a, "--", 2);
  if (withLink) {
    archive_entry_clear(e);
    archive_entry_set_pathname(e, "link");
    archive_entry_set_filetype(e, AE_IFLNK);
    archive_entry_set_perm(e, 0777);
    archive_entry_set_symlink(e, "/");
    archive_write_header(a, e);
  }
  archive_entry_free(e);
  archive_write_close(a);
  archive_write_free(a);
  return std::string(buf.data(), used);
}

// The archive is read once: a file that changes after the first read
// can't bring in an entry that was never checked
TEST(ExtensionPaths, AnArchiveIsCheckedAsItUnpacks)
{
  namespace fs = std::filesystem;
  std::signal(SIGPIPE, SIG_IGN);

  const fs::path dir = fs::canonical(fs::temp_directory_path()) /
                       ("extension-paths-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir / "dst");
  const fs::path fifo = dir / "ext.zip";
  ASSERT_EQ(0, ::mkfifo(fifo.c_str(), 0600));

  // The first open reads a safe archive, every later open one with a link
  std::atomic<bool> stop = false;
  std::thread writer([&] {
    for (int round = 0; !stop; ++round) {
      const int fd = ::open(fifo.c_str(), O_WRONLY);
      if (fd < 0)
        break;
      if (!stop) {
        const std::string zip = make_zip(round > 0);
        ssize_t _ = ::write(fd, zip.data(), zip.size());
        (void)_;
      }
      ::close(fd);
    }
  });

  ExtensionInfo info;
  info.dstPath = (dir / "dst").string();
  std::string error;
  try {
    extract_extension_archive(fifo.string(), info);
  }
  catch (const std::exception& e) {
    error = e.what();
  }

  stop = true;
  const int drain = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
  writer.join();
  ::close(drain);

  std::error_code ec;
  EXPECT_FALSE(fs::is_symlink(fs::symlink_status(dir / "dst" / "link", ec))) << error;
  EXPECT_TRUE(fs::is_regular_file(dir / "dst" / "a.lua")) << error;
  fs::remove_all(dir);
}

// An archive with a link is refused, and what it wrote before the
// link is removed again
TEST(ExtensionPaths, AnArchiveWithALinkLeavesNothing)
{
  namespace fs = std::filesystem;
  const fs::path dir = fs::canonical(fs::temp_directory_path()) /
                       ("extension-link-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir / "dst");
  {
    const std::string zip = make_zip(true);
    std::ofstream(dir / "ext.zip", std::ios::binary).write(zip.data(), zip.size());
  }

  ExtensionInfo info;
  info.dstPath = (dir / "dst").string();
  EXPECT_THROW(extract_extension_archive((dir / "ext.zip").string(), info), std::exception);
  EXPECT_TRUE(fs::is_empty(dir / "dst"));
  fs::remove_all(dir);
}

#endif
