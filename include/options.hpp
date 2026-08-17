#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <opencv2/core.hpp>

namespace fs = std::filesystem;

#ifndef IMG_BAT_VERSION
#define IMG_BAT_VERSION "unknown"
#endif

struct Options {
  std::vector<fs::path> inputs;
  std::optional<fs::path> output;
  std::optional<std::string> format;
  std::optional<std::pair<int, int>> resize;
  int quality = 90;
  bool quality_specified = false;
  int threads = static_cast<int>(std::thread::hardware_concurrency());
  int rotate = 0;
  int border = 0;
  cv::Scalar border_color{255, 255, 255};
  double brightness = 0.0;
  double contrast = 1.0;
  bool grayscale = false;
  bool flip_h = false;
  bool flip_v = false;
  bool recursive = false;
  bool overwrite = false;
  bool quiet = false;
  bool strip_all = false;
  bool strip_gps = false;
  bool info = false;

  bool has_image_transformations() const;
};

std::string lower(std::string value);
void usage();
Options parse_args(int argc, char** argv);
