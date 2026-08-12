#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#ifdef BAT_IMG_WITH_EXIV2
#include <exiv2/exiv2.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

struct Options {
  std::vector<fs::path> inputs;
  std::optional<fs::path> output;
  std::optional<std::string> format;
  std::optional<std::pair<int, int>> resize;
  int quality = 90;
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
  std::cout << "bat_img - cross-platform batch image processor\n\n"
            << "Usage: bat_img -i PATH [-i PATH ...] [options]\n\n"
            << "Options:\n"
            << "  -i, --input PATH       Input file or directory (repeatable)\n"
            << "  -o, --output DIR       Output directory; omit for in-place processing\n"
            << "  -r, --resize WxH       Resize; use 0 for aspect-ratio auto sizing\n"
            << "  -f, --format FORMAT    jpg, png, webp, tiff, bmp, gif, heic\n"
            << "  -q, --quality N        JPEG/WebP quality (1-100; default 90)\n"
            << "  -t, --threads N        Worker threads (default: logical CPUs)\n"
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
    else if (arg == "-q" || arg == "--quality") opt.quality = std::stoi(require_value(i, arg.c_str()));
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

static void strip_metadata(const fs::path& path, bool gps_only) {
#ifdef BAT_IMG_WITH_EXIV2
  auto image = Exiv2::ImageFactory::open(path.string());
  if (!image.get()) throw std::runtime_error("cannot open metadata: " + path.string());
  image->readMetadata();
  if (gps_only) {
    auto& exif = image->exifData();
    for (auto it = exif.begin(); it != exif.end();) {
      if (it->groupName().find("GPS") != std::string::npos) it = exif.erase(it); else ++it;
    }
  } else {
    image->clearExifData(); image->clearIptcData(); image->clearXmpData();
  }
  image->writeMetadata();
#else
  (void)path; (void)gps_only;
  throw std::runtime_error("metadata support was not built; install Exiv2 and rebuild");
#endif
}

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

  cv::Mat image = cv::imread(input.string(), cv::IMREAD_UNCHANGED);
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
  if (!cv::imwrite(output.string(), image, params)) throw std::runtime_error("cannot write " + output.string());
  if (opt.strip_all || opt.strip_gps) strip_metadata(output, opt.strip_gps && !opt.strip_all);
}

int main(int argc, char** argv) {
  try {
    const Options opt = parse_args(argc, argv);
    const auto files = collect_files(opt);
    if (files.empty()) throw std::runtime_error("no input images found");
    // Parallelize across files.  Keep OpenCV single-threaded per worker to
    // avoid an N workers × M OpenCV threads oversubscription.
    cv::setNumThreads(1);
    std::atomic_size_t next_file{0};
    std::atomic_uint succeeded{0};
    std::mutex output_mutex;
    const auto worker = [&] {
      while (true) {
        const size_t index = next_file.fetch_add(1);
        if (index >= files.size()) return;
        const auto& file = files[index];
        try {
          process_one(file, opt);
          ++succeeded;
          if (!opt.quiet) {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::cout << "Processed " << file << '\n';
          }
        } catch (const std::exception& error) {
          std::lock_guard<std::mutex> lock(output_mutex);
          std::cerr << "Error: " << file << ": " << error.what() << '\n';
        }
      }
    };
    const size_t worker_count = std::min(files.size(), static_cast<size_t>(opt.threads));
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) workers.emplace_back(worker);
    for (auto& worker_thread : workers) worker_thread.join();
    if (!opt.quiet) std::cout << "Done: " << succeeded << '/' << files.size() << " succeeded\n";
    return succeeded == files.size() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << "\nUse --help for usage.\n";
    return 2;
  }
}
