#include "options.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool Options::has_image_transformations() const {
  return format.has_value() || resize.has_value() || grayscale || flip_h || flip_v ||
         rotate != 0 || border != 0 || brightness != 0.0 || contrast != 1.0;
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

void usage() {
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
            << "      --info              Print image metainfo\n"
            << "      --rotate DEG        90, 180, or 270\n"
            << "      --flip-h|--flip-v   Mirror image horizontally or vertically\n"
            << "      --grayscale         Convert to grayscale\n"
            << "      --brightness N      Add brightness (-255 to 255)\n"
            << "      --contrast N        Contrast multiplier (e.g. 1.15)\n"
            << "      --border PX         Add a white border\n"
            << "  -R, --recursive         Recurse into input directories\n"
            << "      --strip-gps         Remove GPS EXIF metadata\n"
            << "      --strip-all         Remove EXIF/IPTC/XMP metadata\n"
            << "      --overwrite         Replace existing output files\n"
            << "      --quiet             Suppress per-file output\n";
}

Options parse_args(int argc, char** argv) {
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
