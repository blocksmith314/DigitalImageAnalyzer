#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "platformfile.h"

namespace {

std::filesystem::path tempPath() {
  static std::mt19937 rng(std::random_device{}());
  return std::filesystem::temp_directory_path() /
         ("dia_mapped_" + std::to_string(rng()) + ".bin");
}

}  // namespace

TEST(MappedFileTest, MapsExistingFile) {
  const std::filesystem::path path = tempPath();
  const uint8_t payload[] = {1, 2, 3, 4, 5, 6};
  {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(payload), sizeof(payload));
  }

  MappedFile file;
  std::string error;
  ASSERT_TRUE(file.open(path.string(), error)) << error;
  EXPECT_EQ(file.size(), sizeof(payload));
  ASSERT_NE(file.data(), nullptr);
  EXPECT_EQ(std::memcmp(file.data(), payload, sizeof(payload)), 0);

  file.ReleaseResidentPages();  // must not crash and must keep the data valid
  EXPECT_EQ(std::memcmp(file.data(), payload, sizeof(payload)), 0);

  std::filesystem::remove(path);
}

TEST(MappedFileTest, ReportsMissingFile) {
  MappedFile file;
  std::string error;
  EXPECT_FALSE(file.open("/definitely/not/here.bin", error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(file.size(), 0u);
}

TEST(MappedFileTest, MoveTransfersOwnership) {
  const std::filesystem::path path = tempPath();
  {
    std::ofstream out(path, std::ios::binary);
    out << "abcdef";
  }

  MappedFile source;
  std::string error;
  ASSERT_TRUE(source.open(path.string(), error)) << error;

  MappedFile moved(std::move(source));
  EXPECT_EQ(source.size(), 0u);
  EXPECT_EQ(moved.size(), 6u);
  ASSERT_NE(moved.data(), nullptr);
  EXPECT_EQ(std::memcmp(moved.data(), "abcdef", 6), 0);

  MappedFile assigned;
  assigned = std::move(moved);
  EXPECT_EQ(moved.size(), 0u);
  EXPECT_EQ(assigned.size(), 6u);

  assigned.close();
  EXPECT_EQ(assigned.size(), 0u);
  EXPECT_EQ(assigned.data(), nullptr);

  std::filesystem::remove(path);
}
