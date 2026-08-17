#include "metadata.hpp"
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

#ifdef IMG_BAT_WITH_EXIV2
static std::mutex metadata_mutex;
static std::mutex xmp_namespace_mutex;

static void lock_xmp_namespaces(void*, bool lock) {
  if (lock) xmp_namespace_mutex.lock(); else xmp_namespace_mutex.unlock();
}

XmpRuntime::XmpRuntime(bool enabled) : enabled_(enabled) {
  if (enabled_ && !Exiv2::XmpParser::initialize(lock_xmp_namespaces, nullptr))
    throw std::runtime_error("cannot initialize the Exiv2 XMP toolkit");
}

XmpRuntime::~XmpRuntime() {
  if (enabled_) Exiv2::XmpParser::terminate();
}
#else
XmpRuntime::XmpRuntime(bool enabled) : enabled_(enabled) {}
XmpRuntime::~XmpRuntime() = default;
#endif

Metadata read_metadata(const fs::path& path) {
  Metadata metadata;
#ifdef IMG_BAT_WITH_EXIV2
  std::lock_guard<std::mutex> lock(metadata_mutex);
  auto image = Exiv2::ImageFactory::open(path.string());
  if (!image.get()) return metadata;
  image->readMetadata();
  metadata.exif = image->exifData();
  metadata.iptc = image->iptcData();
  metadata.xmp = image->xmpData();
#else
  (void)path;
#endif
  return metadata;
}

void restore_and_strip_metadata(const fs::path& path, const Metadata& metadata, bool strip_all, bool strip_gps) {
#ifdef IMG_BAT_WITH_EXIV2
  std::lock_guard<std::mutex> lock(metadata_mutex);
  auto image = Exiv2::ImageFactory::open(path.string());
  if (!image.get()) throw std::runtime_error("cannot open metadata: " + path.string());
  if (strip_all) {
    image->clearExifData(); image->clearIptcData(); image->clearXmpData();
  } else {
    Exiv2::ExifData exif = metadata.exif;
    if (strip_gps) {
      for (auto it = exif.begin(); it != exif.end();) {
        if (it->groupName().find("GPS") != std::string::npos) it = exif.erase(it); else ++it;
      }
    }
    image->setExifData(exif);
    image->setIptcData(metadata.iptc);
    image->setXmpData(metadata.xmp);
  }
  image->writeMetadata();
#else
  (void)path; (void)metadata;
  if (strip_all || strip_gps)
    throw std::runtime_error("metadata support was not built; install Exiv2 and rebuild");
#endif
}

void strip_metadata_in_place(const fs::path& path, bool strip_all, bool strip_gps) {
#ifdef IMG_BAT_WITH_EXIV2
std::lock_guard<std::mutex> lock(metadata_mutex);
  auto image = Exiv2::ImageFactory::open(path.string());
  if (!image.get()) throw std::runtime_error("cannot open metadata: " + path.string());
  image->readMetadata();

  const std::string mime = image->mimeType();
  if (mime == "image/heic" || mime == "image/heif" || mime == "image/avif") {
    throw std::runtime_error("Exiv2 does not support writing BMFF metadata for " + path.string());
  }

  if (strip_all) {
    image->clearExifData(); image->clearIptcData(); image->clearXmpData();
  } else if (strip_gps) {
    auto& exif = image->exifData();
    for (auto it = exif.begin(); it != exif.end();) {
      if (it->groupName().find("GPS") != std::string::npos) it = exif.erase(it); else ++it;
    }
  }
  image->writeMetadata();
#else
  throw std::runtime_error("metadata support was not built; install Exiv2 and rebuild");
#endif
}

void print_exiv2_info(const fs::path& path) {
#ifdef IMG_BAT_WITH_EXIV2
  try {
    auto image = Exiv2::ImageFactory::open(path.string());
    if (image.get()) {
      image->readMetadata();
      const auto& exif = image->exifData();
      const auto& iptc = image->iptcData();
      const auto& xmp = image->xmpData();

      std::cout << "  Metadata Counts: EXIF (" << exif.count()
                << "), IPTC (" << iptc.count()
                << "), XMP (" << xmp.count() << ")\n";

      auto print_tag = [&](const char* key, const char* label) {
        auto it = exif.findKey(Exiv2::ExifKey(key));
        if (it != exif.end()) {
          std::cout << "  " << label << ": " << it->value().toString() << "\n";
        }
      };

      print_tag("Exif.Image.Make", "Camera Make");
      print_tag("Exif.Image.Model", "Camera Model");
      print_tag("Exif.Photo.DateTimeOriginal", "Date/Time Original");
      print_tag("Exif.Photo.ExposureTime", "Exposure Time");
      print_tag("Exif.Photo.FNumber", "F-Number");
      print_tag("Exif.Photo.ISOSpeedRatings", "ISO Speed");
      print_tag("Exif.Photo.FocalLength", "Focal Length");

      bool has_gps = false;
      for (auto it = exif.begin(); it != exif.end(); ++it) {
        if (it->groupName().find("GPS") != std::string::npos) {
          has_gps = true;
          break;
        }
      }
      std::cout << "  GPS Data: " << (has_gps ? "Present" : "None") << "\n";
    }
  } catch (...) {}
#else
  (void)path;
#endif
}

#if defined(IMG_BAT_WITH_EXIV2) && defined(IMG_BAT_WITH_LIBHEIF)
static uint64_t read_be(const std::vector<uint8_t>& data, size_t offset, size_t width) {
  if (width > 8 || offset + width > data.size()) throw std::runtime_error("invalid BMFF metadata box");
  uint64_t value = 0;
  for (size_t i = 0; i < width; ++i) value = (value << 8) | data[offset + i];
  return value;
}

static void write_be(std::vector<uint8_t>& data, size_t offset, size_t width, uint64_t value) {
  if (width > 8 || offset + width > data.size() || (width < 8 && value >= (uint64_t{1} << (width * 8))))
    throw std::runtime_error("BMFF metadata value does not fit its field");
  for (size_t i = 0; i < width; ++i) data[offset + width - 1 - i] = static_cast<uint8_t>(value >> (i * 8));
}

struct BmffBox { size_t start; size_t header; size_t end; std::string type; };

static std::vector<BmffBox> bmff_children(const std::vector<uint8_t>& data, size_t begin, size_t end) {
  std::vector<BmffBox> boxes;
  while (begin + 8 <= end) {
    uint64_t size = read_be(data, begin, 4);
    size_t header = 8;
    if (size == 1) { size = read_be(data, begin + 8, 8); header = 16; }
    else if (size == 0) size = end - begin;
    if (size < header || size > end - begin) throw std::runtime_error("invalid BMFF box size");
    boxes.push_back({begin, header, begin + static_cast<size_t>(size),
                     std::string(reinterpret_cast<const char*>(data.data() + begin + 4), 4)});
    begin += static_cast<size_t>(size);
  }
  if (begin != end)
    throw std::runtime_error("invalid BMFF box alignment at " + std::to_string(begin) + " of " + std::to_string(end));
  return boxes;
}

static const BmffBox& bmff_box(const std::vector<BmffBox>& boxes, const char* type) {
  for (const auto& box : boxes) if (box.type == type) return box;
  throw std::runtime_error(std::string("BMFF box not found: ") + type);
}

bool strip_heif_metadata_losslessly(const fs::path& path, bool strip_all, bool strip_gps) {
  std::ifstream input(path, std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
  if (data.empty()) throw std::runtime_error("cannot read HEIC file");

  try {
    const auto top_level = bmff_children(data, 0, data.size());
    const auto& meta = bmff_box(top_level, "meta");
    const auto children = bmff_children(data, meta.start + meta.header + 4, meta.end);
    const auto& iinf = bmff_box(children, "iinf");
    const auto& iloc = bmff_box(children, "iloc");

    struct MetaItem { uint32_t id; std::string type; };
    std::vector<MetaItem> meta_items;

    const auto infos = bmff_children(data, iinf.start + iinf.header + 6, iinf.end);
    for (const auto& infe : infos) {
      if (infe.type != "infe" || infe.start + infe.header + 12 > infe.end) continue;
      const size_t p = infe.start + infe.header;
      const uint8_t version = data[p];
      if (version < 2) continue;
      const uint32_t id = static_cast<uint32_t>(read_be(data, p + 4, version == 2 ? 2 : 4));
      const size_t type_offset = p + 4 + (version == 2 ? 2 : 4) + 2;
      if (type_offset + 4 <= infe.end) {
        std::string item_type(reinterpret_cast<const char*>(data.data() + type_offset), 4);
        meta_items.push_back({id, item_type});
      }
    }

    if (meta_items.empty()) return true;

    size_t p = iloc.start + iloc.header;
    const uint8_t version = data[p]; p += 4;
    const uint8_t sizes = data[p++];
    const size_t offset_size = sizes >> 4, length_size = sizes & 0x0f;
    const uint8_t sizes2 = data[p++];
    const size_t base_offset_size = sizes2 >> 4, index_size = sizes2 & 0x0f;
    const uint32_t item_count = static_cast<uint32_t>(read_be(data, p, version < 2 ? 2 : 4)); p += version < 2 ? 2 : 4;

    bool modified = false;

    for (uint32_t item = 0; item < item_count; ++item) {
      const uint32_t id = static_cast<uint32_t>(read_be(data, p, version < 2 ? 2 : 4)); p += version < 2 ? 2 : 4;
      uint16_t method = 0;
      if (version == 1 || version == 2) { method = static_cast<uint16_t>(read_be(data, p, 2) & 0x0fff); p += 2; }
      p += 2;
      const uint64_t base = read_be(data, p, base_offset_size); p += base_offset_size;
      const uint16_t extent_count = static_cast<uint16_t>(read_be(data, p, 2)); p += 2;

      for (uint16_t extent = 0; extent < extent_count; ++extent) {
        if ((version == 1 || version == 2) && index_size) p += index_size;
        const uint64_t relative = read_be(data, p, offset_size); p += offset_size;
        const size_t length_offset = p;
        const uint64_t length = read_be(data, p, length_size); p += length_size;

        std::string item_type;
        for (const auto& mi : meta_items) {
          if (mi.id == id) { item_type = mi.type; break; }
        }

        if (item_type == "Exif" && extent == 0 && method == 0 && extent_count == 1) {
          const size_t exif_data_offset = static_cast<size_t>(base + relative);
          const size_t exif_length = static_cast<size_t>(length);
          if (exif_length >= 8 && exif_data_offset + exif_length <= data.size()) {
            const size_t tiff_offset = static_cast<size_t>(read_be(data, exif_data_offset, 4));
            const size_t tiff_start = exif_data_offset + 4 + tiff_offset;
            if (tiff_start < exif_data_offset + exif_length) {
              Exiv2::Blob encoded;
              {
                std::lock_guard<std::mutex> lock(metadata_mutex);
                Exiv2::ExifData exif;
                if (!strip_all) {
                  Exiv2::ExifParser::decode(exif, data.data() + tiff_start, exif_data_offset + exif_length - tiff_start);
                  if (strip_gps) {
                    for (auto it = exif.begin(); it != exif.end();) {
                      if (it->groupName().find("GPS") != std::string::npos) it = exif.erase(it); else ++it;
                    }
                  }
                }
                Exiv2::ExifParser::encode(encoded, Exiv2::littleEndian, exif);
              }
              std::vector<uint8_t> replacement(data.begin() + exif_data_offset, data.begin() + tiff_start);
              replacement.insert(replacement.end(), encoded.begin(), encoded.end());
              if (replacement.size() <= exif_length) {
                std::copy(replacement.begin(), replacement.end(), data.begin() + exif_data_offset);
                write_be(data, length_offset, length_size, replacement.size());
                modified = true;
              }
            }
          }
        } else if (strip_all && (item_type == "xml " || item_type == "mime") && extent == 0) {
          write_be(data, length_offset, length_size, 0);
          modified = true;
        }
      }
    }

    if (!modified) return false;

    const fs::path temporary = path.string() + ".img-bat-tmp";
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      output.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    fs::rename(temporary, path);
    return true;
  } catch (...) {
    return false;
  }
}
#else
bool strip_heif_metadata_losslessly(const fs::path&, bool, bool) { return false; }
#endif
