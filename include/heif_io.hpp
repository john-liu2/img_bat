#pragma once

#include <filesystem>
#include <opencv2/core.hpp>
#include "metadata.hpp"

namespace fs = std::filesystem;

#ifdef IMG_BAT_WITH_LIBHEIF
#include <libheif/heif.h>

void check_heif(heif_error error, const std::string& action);
cv::Mat read_heif(const fs::path& path);
void print_heif_info(const fs::path& path);
void write_heif(const fs::path& path, const cv::Mat& input, int quality,
                const Metadata& metadata, bool strip_all, bool strip_gps);
#endif
