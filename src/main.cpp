#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#ifdef IMG_BAT_WITH_EXIV2
#include <exiv2/exiv2.hpp>
#endif

#ifdef IMG_BAT_WITH_LIBHEIF
#include <libheif/heif.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
struct Metadata;

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
};

static std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

static bool is_image(const fs::path& path) {
  const auto ext = lower(path.extension().string());
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" ||
         ext == ".tif" || ext == ".tiff" || ext == ".bmp" || ext == ".gif" ||
         ext == ".heic" || ext == ".heif";
}

#ifdef IMG_BAT_WITH_LIBHEIF
static std::string heif_colorspace_to_string(heif_colorspace cs) {
  switch (cs) {
    case heif_colorspace_YCbCr: return "YCbCr";
    case heif_colorspace_RGB: return "RGB";
    case heif_colorspace_monochrome: return "Monochrome";
    default: return "Unknown/Undefined";
  }
}

static std::string heif_chroma_to_string(heif_chroma chroma) {
  switch (chroma) {
    case heif_chroma_monochrome: return "Monochrome";
    case heif_chroma_420: return "4:2:0";
    case heif_chroma_422: return "4:2:2";
    case heif_chroma_444: return "4:4:4";
    case heif_chroma_interleaved_RGB: return "Interleaved RGB";
    case heif_chroma_interleaved_RGBA: return "Interleaved RGBA";
    default: return "Unknown/Undefined";
  }
}

static void write_heif_metadata(heif_context* context, const heif_image_handle* handle,
                                const Metadata& metadata, bool strip_all, bool strip_gps);

static void check_heif(heif_error error, const std::string& action) {
  if (error.code != heif_error_Ok) throw std::runtime_error(action + ": " + error.message);
}

static cv::Mat read_heif(const fs::path& path) {
  heif_context* context = heif_context_alloc();
  heif_image_handle* handle = nullptr;
  heif_image* decoded = nullptr;
  try {
    check_heif(heif_context_read_from_file(context, path.string().c_str(), nullptr), "cannot read HEIC");
    check_heif(heif_context_get_primary_image_handle(context, &handle), "cannot open HEIC image");
    const bool alpha = heif_image_handle_has_alpha_channel(handle) != 0;
    const auto chroma = alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB;
    check_heif(heif_decode_image(handle, &decoded, heif_colorspace_RGB, chroma, nullptr), "cannot decode HEIC");
    int stride = 0;
    const uint8_t* pixels = heif_image_get_plane_readonly(decoded, heif_channel_interleaved, &stride);
    if (!pixels) throw std::runtime_error("cannot access decoded HEIC pixels");
    const int type = alpha ? CV_8UC4 : CV_8UC3;
    cv::Mat rgb(heif_image_get_height(decoded, heif_channel_interleaved),
                heif_image_get_width(decoded, heif_channel_interleaved), type,
                const_cast<uint8_t*>(pixels), stride);
    cv::Mat result;
    cv::cvtColor(rgb, result, alpha ? cv::COLOR_RGBA2BGRA : cv::COLOR_RGB2BGR);
    heif_image_release(decoded);
    heif_image_handle_release(handle);
    heif_context_free(context);
    return result;
  } catch (...) {
    if (decoded) heif_image_release(decoded);
    if (handle) heif_image_handle_release(handle);
    heif_context_free(context);
    throw;
  }
}

static void print_heif_info(const fs::path& path) {
  heif_context* context = heif_context_alloc();
  heif_image_handle* handle = nullptr;
  heif_image* decoded = nullptr;
  try {
    check_heif(heif_context_read_from_file(context, path.string().c_str(), nullptr), "cannot read HEIC");
    check_heif(heif_context_get_primary_image_handle(context, &handle), "cannot open HEIC image handle");

    int width = heif_image_handle_get_width(handle);
    int height = heif_image_handle_get_height(handle);
    bool has_alpha = heif_image_handle_has_alpha_channel(handle) != 0;
    int bit_depth = heif_image_handle_get_luma_bits_per_pixel(handle);

    std::string colorspace_str = "Unknown";
    std::string chroma_str = "Unknown";

    if (heif_decode_image(handle, &decoded, heif_colorspace_undefined, heif_chroma_undefined, nullptr).code == heif_error_Ok) {
      heif_colorspace cs = heif_image_get_colorspace(decoded);
      heif_chroma chroma = heif_image_get_chroma_format(decoded);
      colorspace_str = heif_colorspace_to_string(cs);
      chroma_str = heif_chroma_to_string(chroma);
      heif_image_release(decoded);
      decoded = nullptr;
    }

    std::cout << "File: " << path.string() << "\n"
              << "  Format: HEIC / HEIF\n"
              << "  Dimensions: " << width << "x" << height << "\n"
              << "  Bit Depth: " << bit_depth << " bits/pixel\n"
              << "  Alpha Channel: " << (has_alpha ? "Yes" : "No") << "\n"
              << "  Colorspace: " << colorspace_str << "\n"
              << "  Chroma Format: " << chroma_str << "\n";

    heif_image_handle_release(handle);
    heif_context_free(context);
  } catch (const std::exception& e) {
    if (decoded) heif_image_release(decoded);
    if (handle) heif_image_handle_release(handle);
    if (context) heif_context_free(context);
    std::cout << "File: " << path.string() << "\n"
              << "  Error reading HEIC metainfo: " << e.what() << "\n";
  }
}

static void write_heif(const fs::path& path, const cv::Mat& input, int quality,
                       const Metadata& metadata, bool strip_all, bool strip_gps) {
  cv::Mat bgr;
  if (input.depth() != CV_8U) input.convertTo(bgr, CV_8U);
  else bgr = input;
  cv::Mat rgb;
  if (bgr.channels() == 1) cv::cvtColor(bgr, rgb, cv::COLOR_GRAY2RGB);
  else if (bgr.channels() == 3) cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  else if (bgr.channels() == 4) cv::cvtColor(bgr, rgb, cv::COLOR_BGRA2RGBA);
  else throw std::runtime_error("HEIC output requires a 1-, 3-, or 4-channel image");

  heif_context* context = heif_context_alloc();
  heif_image* image = nullptr;
  heif_encoder* encoder = nullptr;
  heif_image_handle* output_handle = nullptr;
  try {
    const bool alpha = rgb.channels() == 4;
    if (alpha) {
      check_heif(heif_image_create(rgb.cols, rgb.rows, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, &image), "cannot create HEIC image");
      check_heif(heif_image_add_plane(image, heif_channel_interleaved, rgb.cols, rgb.rows, 8), "cannot allocate HEIC pixels");
      int stride = 0;
      uint8_t* pixels = heif_image_get_plane(image, heif_channel_interleaved, &stride);
      for (int row = 0; row < rgb.rows; ++row)
        std::copy(rgb.ptr<uint8_t>(row), rgb.ptr<uint8_t>(row) + rgb.cols * rgb.elemSize(), pixels + row * stride);
    } else {
      // HEIC photos normally use YCbCr 4:2:0. Encoding interleaved RGB lets
      // the codec choose 4:4:4, which substantially inflates photo files.
      cv::Mat ycrcb;
      cv::cvtColor(rgb, ycrcb, cv::COLOR_RGB2YCrCb);
      std::vector<cv::Mat> channels;
      cv::split(ycrcb, channels);
      cv::Mat cb, cr;
      const cv::Size chroma_size((rgb.cols + 1) / 2, (rgb.rows + 1) / 2);
      cv::resize(channels[2], cb, chroma_size, 0, 0, cv::INTER_AREA);
      cv::resize(channels[1], cr, chroma_size, 0, 0, cv::INTER_AREA);
      check_heif(heif_image_create(rgb.cols, rgb.rows, heif_colorspace_YCbCr, heif_chroma_420, &image), "cannot create HEIC image");
      check_heif(heif_image_add_plane(image, heif_channel_Y, rgb.cols, rgb.rows, 8), "cannot allocate HEIC luma");
      check_heif(heif_image_add_plane(image, heif_channel_Cb, chroma_size.width, chroma_size.height, 8), "cannot allocate HEIC Cb");
      check_heif(heif_image_add_plane(image, heif_channel_Cr, chroma_size.width, chroma_size.height, 8), "cannot allocate HEIC Cr");
      const auto copy_plane = [&](const cv::Mat& source, heif_channel channel) {
        int stride = 0;
        uint8_t* pixels = heif_image_get_plane(image, channel, &stride);
        for (int row = 0; row < source.rows; ++row)
          std::copy(source.ptr<uint8_t>(row), source.ptr<uint8_t>(row) + source.cols, pixels + row * stride);
      };
      copy_plane(channels[0], heif_channel_Y);
      copy_plane(cb, heif_channel_Cb);
      copy_plane(cr, heif_channel_Cr);
    }
    check_heif(heif_context_get_encoder_for_format(context, heif_compression_HEVC, &encoder), "HEIC encoder unavailable");
    check_heif(heif_encoder_set_lossy_quality(encoder, quality), "cannot set HEIC quality");
    check_heif(heif_context_encode_image(context, image, encoder, nullptr, &output_handle), "cannot encode HEIC");
    write_heif_metadata(context, output_handle, metadata, strip_all, strip_gps);
    check_heif(heif_context_write_to_file(context, path.string().c_str()), "cannot write HEIC");
    heif_image_handle_release(output_handle);
    heif_encoder_release(encoder);
    heif_image_release(image);
    heif_context_free(context);
  } catch (...) {
    if (output_handle) heif_image_handle_release(output_handle);
    if (encoder) heif_encoder_release(encoder);
    if (image) heif_image_release(image);
    heif_context_free(context);
    throw;
  }
}
#endif

static void print_generic_info(const fs::path& path) {
  cv::Mat img = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
  if (img.empty()) {
    std::cout << "File: " << path.string() << "\n"
              << "  Error: Could not decode image\n";
    return;
  }

  std::string depth_str;
  switch (img.depth()) {
    case CV_8U:  depth_str = "8-bit unsigned"; break;
    case CV_8S:  depth_str = "8-bit signed"; break;
    case CV_16U: depth_str = "16-bit unsigned"; break;
    case CV_16S: depth_str = "16-bit signed"; break;
    case CV_32S: depth_str = "32-bit signed"; break;
    case CV_32F: depth_str = "32-bit float"; break;
    case CV_64F: depth_str = "64-bit float"; break;
    default:     depth_str = "Unknown"; break;
  }

  std::string cs_str;
  std::string chroma_str;
  if (img.channels() == 1) {
    cs_str = "Grayscale";
    chroma_str = "Monochrome (4:0:0)";
  } else if (img.channels() == 3) {
    cs_str = "BGR / RGB";
    chroma_str = "4:4:4";
  } else if (img.channels() == 4) {
    cs_str = "BGRA / RGBA";
    chroma_str = "4:4:4:4";
  } else {
    cs_str = std::to_string(img.channels()) + "-channel";
    chroma_str = "Custom";
  }

  std::cout << "File: " << path.string() << "\n"
            << "  Format: " << lower(path.extension().string().substr(path.extension().string().empty() ? 0 : 1)) << "\n"
            << "  Dimensions: " << img.cols << "x" << img.rows << "\n"
            << "  Channels: " << img.channels() << "\n"
            << "  Bit Depth: " << depth_str << "\n"
            << "  Colorspace: " << cs_str << "\n"
            << "  Chroma Format: " << chroma_str << "\n";
}

#ifdef IMG_BAT_WITH_EXIV2
static void print_exiv2_info(const fs::path& path) {
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
}
#endif

static void print_info_for_file(const fs::path& path) {
  const auto ext = lower(path.extension().string());
  if (ext == ".heic" || ext == ".heif") {
#ifdef IMG_BAT_WITH_LIBHEIF
    print_heif_info(path);
#else
    print_generic_info(path);
#endif
  } else {
    print_generic_info(path);
  }
#ifdef IMG_BAT_WITH_EXIV2
  print_exiv2_info(path);
#endif
  std::cout << "----------------------------------------\n";
}

static std::pair<int, int> parse_resize(const std::string& spec) {
  const auto separator = spec.find('x');
  if (separator == std::string::npos) throw std::runtime_error("resize must be WIDTHxHEIGHT");
  const int width = std::stoi(spec.substr(0, separator));
  const int height = std::stoi(spec.substr(separator + 1));
  if (width < 0 || height < 0 || (width == 0 && height == 0)) {
    throw std::runtime_error("resize dimensions must be positive (one may be zero)");
  }
  return {width, height};
}

static void usage() {
  std::cout << "img_bat - cross-platform C++ batch image processor\n\n"
            << "Usage: img_bat -i PATH [-i PATH ...] [options]\n\n"
            << "Options:\n"
            << "  -i, --input PATH       Input file or directory (repeatable)\n"
            << "  -o, --output DIR       Output directory; omit for in-place processing\n"
            << "  -r, --resize WxH       Resize; use 0 for aspect-ratio auto sizing\n"
            << "  -f, --format FORMAT    heic, jpg, png, webp, tiff, bmp, gif\n"
            << "  -q, --quality N        JPEG/WebP quality (1-100; default 90)\n"
            << "  -t, --threads N        Worker threads (default: logical CPUs)\n"
            << "  -v, --version          Show version and exit\n"
            << "      --info              Print image metainfo (colorspace, chroma, dimensions, etc.)\n"
            << "      --rotate DEG        90, 180, or 270\n"
            << "      --flip-h|--flip-v   Mirror image horizontally or vertically\n"
            << "      --grayscale         Convert to grayscale\n"
            << "      --brightness N      Add brightness (-255 to 255)\n"
            << "      --contrast N        Contrast multiplier (for example 1.15)\n"
            << "      --border PX         Add a white border\n"
            << "  -R, --recursive         Recurse into input directories\n"
            << "      --strip-gps         Remove GPS EXIF metadata (requires Exiv2)\n"
            << "      --strip-all         Remove EXIF/IPTC/XMP metadata (requires Exiv2)\n"
            << "      --overwrite         Replace existing output files\n"
            << "      --quiet             Suppress per-file output\n";
}

static Options parse_args(int argc, char** argv) {
  Options opt;
  auto require_value = [&](int& index, const char* flag) -> std::string {
    if (++index >= argc) throw std::runtime_error(std::string(flag) + " requires a value");
    return argv[index];
  };
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-i" || arg == "--input") opt.inputs.emplace_back(require_value(i, arg.c_str()));
    else if (arg == "-o" || arg == "--output") opt.output = require_value(i, arg.c_str());
    else if (arg == "-r" || arg == "--resize") opt.resize = parse_resize(require_value(i, arg.c_str()));
    else if (arg == "-f" || arg == "--format") opt.format = lower(require_value(i, arg.c_str()));
    else if (arg == "-q" || arg == "--quality") {
      opt.quality = std::stoi(require_value(i, arg.c_str()));
      opt.quality_specified = true;
    }
    else if (arg == "-t" || arg == "--threads") opt.threads = std::stoi(require_value(i, arg.c_str()));
    else if (arg == "--rotate") opt.rotate = std::stoi(require_value(i, arg.c_str()));
    else if (arg == "--border") opt.border = std::stoi(require_value(i, arg.c_str()));
    else if (arg == "--brightness") opt.brightness = std::stod(require_value(i, arg.c_str()));
    else if (arg == "--contrast") opt.contrast = std::stod(require_value(i, arg.c_str()));
    else if (arg == "-R" || arg == "--recursive") opt.recursive = true;
    else if (arg == "--flip-h") opt.flip_h = true;
    else if (arg == "--flip-v") opt.flip_v = true;
    else if (arg == "--grayscale") opt.grayscale = true;
    else if (arg == "--strip-gps") opt.strip_gps = true;
    else if (arg == "--strip-all") opt.strip_all = true;
    else if (arg == "--overwrite") opt.overwrite = true;
    else if (arg == "--quiet") opt.quiet = true;
    else if (arg == "--info") opt.info = true;
    else if (arg == "-v" || arg == "--version") {
      std::cout << "img_bat " << IMG_BAT_VERSION << '\n';
      std::exit(0);
    }
    else if (arg == "-h" || arg == "--help") { usage(); std::exit(0); }
    else throw std::runtime_error("unknown option: " + arg);
  }
  if (opt.inputs.empty()) throw std::runtime_error("at least one --input is required");
  if (opt.quality < 1 || opt.quality > 100) throw std::runtime_error("quality must be 1-100");
  if (opt.threads < 1) throw std::runtime_error("threads must be at least 1");
  if (opt.rotate != 0 && opt.rotate != 90 && opt.rotate != 180 && opt.rotate != 270)
    throw std::runtime_error("rotate must be 90, 180, or 270");
  return opt;
}

static std::vector<fs::path> collect_files(const Options& opt) {
  std::vector<fs::path> paths;
  for (const auto& input : opt.inputs) {
    if (fs::is_regular_file(input) && is_image(input)) paths.push_back(input);
    else if (fs::is_directory(input)) {
      if (opt.recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(input))
          if (entry.is_regular_file() && is_image(entry.path())) paths.push_back(entry.path());
      } else {
        for (const auto& entry : fs::directory_iterator(input))
          if (entry.is_regular_file() && is_image(entry.path())) paths.push_back(entry.path());
      }
    } else {
      std::cerr << "Warning: skipping " << input << " (not an image file or directory)\n";
    }
  }
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  return paths;
}

struct Metadata {
#ifdef IMG_BAT_WITH_EXIV2
  Exiv2::ExifData exif;
  Exiv2::IptcData iptc;
  Exiv2::XmpData xmp;
#endif
};

#ifdef IMG_BAT_WITH_LIBHEIF
static void write_heif_metadata(heif_context* context, const heif_image_handle* handle,
                                const Metadata& metadata, bool strip_all, bool strip_gps) {
#ifdef IMG_BAT_WITH_EXIV2
  if (strip_all) return;
  Exiv2::ExifData exif = metadata.exif;
  if (strip_gps) {
    for (auto it = exif.begin(); it != exif.end();) {
      if (it->groupName().find("GPS") != std::string::npos) it = exif.erase(it); else ++it;
    }
  }
  if (!exif.empty()) {
    Exiv2::Blob encoded_exif;
    Exiv2::ExifParser::encode(encoded_exif, Exiv2::littleEndian, exif);
    if (!encoded_exif.empty())
      check_heif(heif_context_add_exif_metadata(context, handle, encoded_exif.data(), static_cast<int>(encoded_exif.size())),
                 "cannot preserve HEIC EXIF metadata");
  }
  if (!metadata.xmp.empty()) {
    std::string encoded_xmp;
    if (Exiv2::XmpParser::encode(encoded_xmp, metadata.xmp) == 0 && !encoded_xmp.empty())
      check_heif(heif_context_add_XMP_metadata(context, handle, encoded_xmp.data(), static_cast<int>(encoded_xmp.size())),
                 "cannot preserve HEIC XMP metadata");
  }
#else
  (void)context; (void)handle; (void)metadata; (void)strip_all; (void)strip_gps;
#endif
}
#endif

#ifdef IMG_BAT_WITH_EXIV2
// Exiv2's XMP toolkit has process-wide state.  It must be initialized before
// worker threads start, and metadata I/O must not run concurrently.
static std::mutex metadata_mutex;
static std::mutex xmp_namespace_mutex;

static void lock_xmp_namespaces(void*, bool lock) {
  if (lock) xmp_namespace_mutex.lock(); else xmp_namespace_mutex.unlock();
}

class XmpRuntime {
 public:
  explicit XmpRuntime(bool enabled) : enabled_(enabled) {
    if (enabled_ && !Exiv2::XmpParser::initialize(lock_xmp_namespaces, nullptr))
      throw std::runtime_error("cannot initialize the Exiv2 XMP toolkit");
  }
  ~XmpRuntime() {
    if (enabled_) Exiv2::XmpParser::terminate();
  }

 private:
  bool enabled_;
};
#else
class XmpRuntime {
 public:
  explicit XmpRuntime(bool) {}
};
#endif

static Metadata read_metadata(const fs::path& path) {
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

static void restore_and_strip_metadata(const fs::path& path, const Metadata& metadata, bool strip_all, bool strip_gps) {
#ifdef IMG_BAT_WITH_EXIV2
  std::lock_guard<std::mutex> lock(metadata_mutex);
  auto image = Exiv2::ImageFactory::open(path.string());
  if (!image.get()) throw std::runtime_error("cannot open metadata: " + path.string());
  if (strip_all) {
    image->clearExifData(); image->clearIptcData(); image->clearXmpData();
  } else {
    // Restore the captured metadata explicitly because OpenCV drops it while
    // encoding.  Remove GPS from the copy before attaching it to the output;
    // mutating image->exifData() after setExifData() can discard all tags in
    // some Exiv2 image backends.
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

static void strip_metadata_in_place(const fs::path& path, bool strip_all, bool strip_gps) {
#ifdef IMG_BAT_WITH_EXIV2
  std::lock_guard<std::mutex> lock(metadata_mutex);
  auto image = Exiv2::ImageFactory::open(path.string());
  if (!image.get()) throw std::runtime_error("cannot open metadata: " + path.string());
  image->readMetadata();
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
  (void)path; (void)strip_all; (void)strip_gps;
  throw std::runtime_error("metadata support was not built; install Exiv2 and rebuild");
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

// Fast HEIC GPS removal. This updates only the Exif item and its iloc length;
// the compressed HEVC payload remains byte-for-byte untouched.
static bool strip_gps_heif_losslessly(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
  if (data.empty()) throw std::runtime_error("cannot read HEIC file");
  const auto top_level = bmff_children(data, 0, data.size());
  const auto& meta = bmff_box(top_level, "meta");
  const auto children = bmff_children(data, meta.start + meta.header + 4, meta.end);
  const auto& iinf = bmff_box(children, "iinf");
  const auto& iloc = bmff_box(children, "iloc");

  uint32_t exif_id = 0;
  const auto infos = bmff_children(data, iinf.start + iinf.header + 6, iinf.end);
  for (const auto& infe : infos) {
    if (infe.type != "infe" || infe.start + infe.header + 12 > infe.end) continue;
    const size_t p = infe.start + infe.header;
    const uint8_t version = data[p];
    if (version < 2) continue;
    const uint32_t id = static_cast<uint32_t>(read_be(data, p + 4, version == 2 ? 2 : 4));
    const size_t type_offset = p + 4 + (version == 2 ? 2 : 4) + 2;
    if (type_offset + 4 <= infe.end && std::string(reinterpret_cast<const char*>(data.data() + type_offset), 4) == "Exif") {
      exif_id = id;
      break;
    }
  }
  if (exif_id == 0) return true;  // No EXIF item means there is no EXIF GPS to remove.

  size_t p = iloc.start + iloc.header;
  const uint8_t version = data[p]; p += 4;
  const uint8_t sizes = data[p++];
  const size_t offset_size = sizes >> 4, length_size = sizes & 0x0f;
  const uint8_t sizes2 = data[p++];
  const size_t base_offset_size = sizes2 >> 4, index_size = sizes2 & 0x0f;
  const uint32_t item_count = static_cast<uint32_t>(read_be(data, p, version < 2 ? 2 : 4)); p += version < 2 ? 2 : 4;
  size_t exif_data_offset = 0, exif_length_offset = 0, exif_length = 0;
  for (uint32_t item = 0; item < item_count; ++item) {
    const uint32_t id = static_cast<uint32_t>(read_be(data, p, version < 2 ? 2 : 4)); p += version < 2 ? 2 : 4;
    uint16_t method = 0;
    if (version == 1 || version == 2) { method = static_cast<uint16_t>(read_be(data, p, 2) & 0x0fff); p += 2; }
    p += 2;  // data_reference_index
    const uint64_t base = read_be(data, p, base_offset_size); p += base_offset_size;
    const uint16_t extent_count = static_cast<uint16_t>(read_be(data, p, 2)); p += 2;
    for (uint16_t extent = 0; extent < extent_count; ++extent) {
      if ((version == 1 || version == 2) && index_size) p += index_size;
      const uint64_t relative = read_be(data, p, offset_size); p += offset_size;
      const size_t length_offset = p;
      const uint64_t length = read_be(data, p, length_size); p += length_size;
      if (id == exif_id && extent == 0 && method == 0 && extent_count == 1) {
        exif_data_offset = static_cast<size_t>(base + relative);
        exif_length_offset = length_offset;
        exif_length = static_cast<size_t>(length);
      }
    }
  }
  if (exif_length < 8 || exif_data_offset + exif_length > data.size())
    throw std::runtime_error("unsupported HEIC EXIF item layout");

  // A HEIF Exif item begins with a 4-byte big-endian offset to the TIFF
  // header. Apple files commonly use a non-zero offset and include an "Exif"
  // identifier between the offset field and TIFF header.
  const size_t tiff_offset = static_cast<size_t>(read_be(data, exif_data_offset, 4));
  const size_t tiff_start = exif_data_offset + 4 + tiff_offset;
  if (tiff_start >= exif_data_offset + exif_length)
    throw std::runtime_error("invalid TIFF offset in HEIC EXIF item");

  Exiv2::Blob encoded;
  {
    std::lock_guard<std::mutex> lock(metadata_mutex);
    Exiv2::ExifData exif;
    Exiv2::ExifParser::decode(exif, data.data() + tiff_start, exif_data_offset + exif_length - tiff_start);
    for (auto it = exif.begin(); it != exif.end();) {
      if (it->groupName().find("GPS") != std::string::npos) it = exif.erase(it); else ++it;
    }
    Exiv2::ExifParser::encode(encoded, Exiv2::littleEndian, exif);
  }
  std::vector<uint8_t> replacement(data.begin() + exif_data_offset, data.begin() + tiff_start);
  replacement.insert(replacement.end(), encoded.begin(), encoded.end());
  if (replacement.size() > exif_length) return false;
  std::copy(replacement.begin(), replacement.end(), data.begin() + exif_data_offset);
  write_be(data, exif_length_offset, length_size, replacement.size());

  const fs::path temporary = path.string() + ".img-bat-tmp";
  { std::ofstream output(temporary, std::ios::binary | std::ios::trunc); output.write(reinterpret_cast<const char*>(data.data()), data.size()); }
  fs::rename(temporary, path);
  return true;
}
#endif

static fs::path output_path(const fs::path& input, const Options& opt) {
  std::string extension = opt.format ? *opt.format : input.extension().string().substr(1);
  if (extension == "jpeg") extension = "jpg";
  fs::path output = input;
  output.replace_extension("." + extension);
  return opt.output ? *opt.output / output.filename() : output;
}

static void process_one(const fs::path& input, const Options& opt) {
  const fs::path output = output_path(input, opt);
  if (!opt.overwrite && output != input && fs::exists(output)) return;
  if (output.has_parent_path()) fs::create_directories(output.parent_path());

  const bool image_transform_requested = opt.format || opt.resize || opt.grayscale || opt.flip_h || opt.flip_v ||
                                       opt.rotate != 0 || opt.border != 0 || opt.brightness != 0.0 || opt.contrast != 1.0;
  const bool is_heif = lower(input.extension().string()) == ".heic" || lower(input.extension().string()) == ".heif";
  if (output == input && is_heif && !image_transform_requested && opt.strip_gps && !opt.strip_all) {
#if defined(IMG_BAT_WITH_EXIV2) && defined(IMG_BAT_WITH_LIBHEIF)
    if (strip_gps_heif_losslessly(input)) return;
#endif
  }
  if (output == input && !is_heif && !image_transform_requested && (opt.strip_all || opt.strip_gps)) {
    strip_metadata_in_place(input, opt.strip_all, opt.strip_gps);
    return;
  }

  // OpenCV/libheif encoders do not copy metadata.  Only load it when a
  // stripping operation needs to distinguish GPS from other EXIF tags.
  const Metadata metadata = (opt.strip_all || opt.strip_gps || is_heif || opt.format == "heic" || opt.format == "heif")
                                ? read_metadata(input) : Metadata{};
  const auto input_ext = lower(input.extension().string());
  cv::Mat image;
  if (input_ext == ".heic" || input_ext == ".heif") {
#ifdef IMG_BAT_WITH_LIBHEIF
    image = read_heif(input);
#else
    throw std::runtime_error("HEIC support was not built; install libheif and rebuild");
#endif
  } else {
    image = cv::imread(input.string(), cv::IMREAD_UNCHANGED);
  }
  if (image.empty()) throw std::runtime_error("cannot decode " + input.string());
  if (opt.grayscale) cv::cvtColor(image, image, cv::COLOR_BGR2GRAY);
  if (opt.resize) {
    auto [width, height] = *opt.resize;
    if (width == 0) width = static_cast<int>(image.cols * (height / static_cast<double>(image.rows)));
    if (height == 0) height = static_cast<int>(image.rows * (width / static_cast<double>(image.cols)));
    cv::resize(image, image, {width, height}, 0, 0, cv::INTER_LANCZOS4);
  }
  if (opt.brightness != 0.0 || opt.contrast != 1.0) image.convertTo(image, -1, opt.contrast, opt.brightness);
  if (opt.rotate == 90) cv::rotate(image, image, cv::ROTATE_90_CLOCKWISE);
  if (opt.rotate == 180) cv::rotate(image, image, cv::ROTATE_180);
  if (opt.rotate == 270) cv::rotate(image, image, cv::ROTATE_90_COUNTERCLOCKWISE);
  if (opt.flip_h) cv::flip(image, image, 1);
  if (opt.flip_v) cv::flip(image, image, 0);
  if (opt.border > 0) cv::copyMakeBorder(image, image, opt.border, opt.border, opt.border, opt.border,
                                         cv::BORDER_CONSTANT, opt.border_color);

  std::vector<int> params;
  const auto ext = lower(output.extension().string());
  if (ext == ".jpg" || ext == ".jpeg") params = {cv::IMWRITE_JPEG_QUALITY, opt.quality};
  if (ext == ".webp") params = {cv::IMWRITE_WEBP_QUALITY, opt.quality};
  if (ext == ".heic" || ext == ".heif") {
#ifdef IMG_BAT_WITH_LIBHEIF
    // HEIC encoders interpret quality differently from JPEG. A quality of 50
    // is a typical photographic HEIC setting; retain an explicit -q override.
    const int heic_quality = opt.quality_specified ? opt.quality : 50;
    write_heif(output, image, heic_quality, metadata, opt.strip_all, opt.strip_gps);
#else
    throw std::runtime_error("HEIC support was not built; install libheif and rebuild");
#endif
  } else if (!cv::imwrite(output.string(), image, params)) {
    throw std::runtime_error("cannot write " + output.string());
  }
  // HEIC metadata is attached through libheif, because Exiv2 cannot write
  // BMFF images. Other formats use Exiv2 after OpenCV encoding.
  if ((opt.strip_all || opt.strip_gps) && ext != ".heic" && ext != ".heif")
    restore_and_strip_metadata(output, metadata, opt.strip_all, opt.strip_gps);
}

static void print_progress(size_t completed, size_t total) {
  constexpr size_t bar_width = 30;
  const size_t filled = total == 0 ? 0 : completed * bar_width / total;
  const size_t percent = total == 0 ? 100 : completed * 100 / total;
  std::cout << "\rProgress [" << std::string(filled, '#')
            << std::string(bar_width - filled, '-') << "] "
            << completed << '/' << total << " (" << percent << "%)" << std::flush;
}

int main(int argc, char** argv) {
  try {
    const Options opt = parse_args(argc, argv);
    XmpRuntime xmp_runtime(opt.strip_all || opt.strip_gps);
    const auto started = std::chrono::steady_clock::now();
    const auto files = collect_files(opt);
    if (files.empty()) throw std::runtime_error("no input images found");
    if (opt.info) {
      for (const auto& file : files) {
        print_info_for_file(file);
      }
      return 0;
    }
    // Parallelize across files.  Keep OpenCV single-threaded per worker to
    // avoid an N workers × M OpenCV threads oversubscription.
    cv::setNumThreads(1);
    std::atomic_size_t next_file{0};
    std::atomic_uint succeeded{0};
    std::atomic_size_t completed{0};
    std::mutex output_mutex;
    const size_t worker_count = std::min(files.size(), static_cast<size_t>(opt.threads));
    if (!opt.quiet) {
      std::cout << "Processing " << files.size() << " image" << (files.size() == 1 ? "" : "s")
                << " using " << worker_count << " worker " << (worker_count == 1 ? "thread" : "threads") << "\n";
      print_progress(0, files.size());
    }
    const auto worker = [&] {
      while (true) {
        const size_t index = next_file.fetch_add(1);
        if (index >= files.size()) return;
        const auto& file = files[index];
        try {
          process_one(file, opt);
          ++succeeded;
        } catch (const std::exception& error) {
          if (!opt.quiet) {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::cerr << "\rError: " << file << ": " << error.what() << '\n';
          }
        }
        const size_t done = ++completed;
        if (!opt.quiet) {
          std::lock_guard<std::mutex> lock(output_mutex);
          print_progress(done, files.size());
        }
      }
    };
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) workers.emplace_back(worker);
    for (auto& worker_thread : workers) worker_thread.join();
    if (!opt.quiet) {
      const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      std::cout << "\nDone: " << succeeded << '/' << files.size() << " succeeded"
                << " (elapsed: " << std::fixed << std::setprecision(2) << elapsed << " s)\n";
    }
    return succeeded == files.size() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << "\nUse --help for usage.\n";
    return 2;
  }
}
