#include "image_processor.hpp"
#include "heif_io.hpp"
#include "metadata.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <thread>

static bool is_image(const fs::path& path) {
  const auto ext = lower(path.extension().string());
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" ||
         ext == ".tif" || ext == ".tiff" || ext == ".bmp" || ext == ".gif" ||
         ext == ".heic" || ext == ".heif";
}

std::vector<fs::path> collect_files(const Options& opt) {
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

  std::string cs_str, chroma_str;
  if (img.channels() == 1) {
    cs_str = "Grayscale"; chroma_str = "Monochrome (4:0:0)";
  } else if (img.channels() == 3) {
    cs_str = "BGR / RGB"; chroma_str = "4:4:4";
  } else if (img.channels() == 4) {
    cs_str = "BGRA / RGBA"; chroma_str = "4:4:4:4";
  } else {
    cs_str = std::to_string(img.channels()) + "-channel"; chroma_str = "Custom";
  }

  std::cout << "File: " << path.string() << "\n"
            << "  Format: " << lower(path.extension().string().substr(path.extension().string().empty() ? 0 : 1)) << "\n"
            << "  Dimensions: " << img.cols << "x" << img.rows << "\n"
            << "  Channels: " << img.channels() << "\n"
            << "  Bit Depth: " << depth_str << "\n"
            << "  Colorspace: " << cs_str << "\n"
            << "  Chroma Format: " << chroma_str << "\n";
}

void print_info_for_file(const fs::path& path) {
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
  print_exiv2_info(path);
  std::cout << "----------------------------------------\n";
}

static fs::path output_path(const fs::path& input, const Options& opt) {
  std::string extension = opt.format ? *opt.format : input.extension().string().substr(1);
  if (extension == "jpeg") extension = "jpg";
  fs::path output = input;
  output.replace_extension("." + extension);
  return opt.output ? *opt.output / output.filename() : output;
}

void process_one(const fs::path& input, const Options& opt) {
  const fs::path output = output_path(input, opt);
  if (!opt.overwrite && output != input && fs::exists(output)) return;
  if (output.has_parent_path()) fs::create_directories(output.parent_path());

  const bool transform_requested = opt.has_image_transformations();
  const bool is_heif = lower(input.extension().string()) == ".heic" || lower(input.extension().string()) == ".heif";

  // Fast-Path Container Metadata Stripping (In-place or metadata-only)
if (output == input && !transform_requested && (opt.strip_all || opt.strip_gps)) {
    if (is_heif) {
      if (strip_heif_metadata_losslessly(input, opt.strip_all, opt.strip_gps)) return;
      // Fallback to decode/re-encode pipeline if binary BMFF structure parsing fails
    } else {
      strip_metadata_in_place(input, opt.strip_all, opt.strip_gps);
      return;
    }
  }

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
    const int heic_quality = opt.quality_specified ? opt.quality : 50;
    write_heif(output, image, heic_quality, metadata, opt.strip_all, opt.strip_gps);
#else
    throw std::runtime_error("HEIC support was not built; install libheif and rebuild");
#endif
  } else if (!cv::imwrite(output.string(), image, params)) {
    throw std::runtime_error("cannot write " + output.string());
  }

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

int run_batch_processor(const Options& opt) {
  const auto started = std::chrono::steady_clock::now();
  const auto files = collect_files(opt);
  if (files.empty()) throw std::runtime_error("no input images found");

  if (opt.info) {
    for (const auto& file : files) print_info_for_file(file);
    return 0;
  }

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
}
