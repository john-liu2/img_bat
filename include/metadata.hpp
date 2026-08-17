#pragma once

#include <filesystem>
#include <string>

#ifdef IMG_BAT_WITH_EXIV2
#include <exiv2/exiv2.hpp>
#endif

namespace fs = std::filesystem;

struct Metadata {
#ifdef IMG_BAT_WITH_EXIV2
  Exiv2::ExifData exif;
  Exiv2::IptcData iptc;
  Exiv2::XmpData xmp;
#endif
};

class XmpRuntime {
 public:
  explicit XmpRuntime(bool enabled);
  ~XmpRuntime();
 private:
  bool enabled_;
};

Metadata read_metadata(const fs::path& path);
void restore_and_strip_metadata(const fs::path& path, const Metadata& metadata, bool strip_all, bool strip_gps);
void strip_metadata_in_place(const fs::path& path, bool strip_all, bool strip_gps);
bool strip_heif_metadata_losslessly(const fs::path& path, bool strip_all, bool strip_gps);
void print_exiv2_info(const fs::path& path);
