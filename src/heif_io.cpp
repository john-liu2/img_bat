#include "heif_io.hpp"

#ifdef IMG_BAT_WITH_LIBHEIF
#include <libheif/heif.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <vector>

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

void check_heif(heif_error error, const std::string& action) {
  if (error.code != heif_error_Ok) {
    throw std::runtime_error(
        action + ": code=" + std::to_string(static_cast<int>(error.code)) +
        ", subcode=" + std::to_string(static_cast<int>(error.subcode)) +
        ", message=" + (error.message ? error.message : "<null>"));
  }
}

cv::Mat read_heif(const fs::path& path) {
  // 1. Open and read the raw binary file using std::ifstream (handles Windows wchar_t paths)
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error("cannot open HEIC file: " + path.string());
  }

  const std::streamsize size = file.tellg();
  if (size <= 0) {
    throw std::runtime_error("HEIC file is empty: " + path.string());
  }

  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
    throw std::runtime_error("failed to read HEIC file content: " + path.string());
  }

  // 2. Initialize libheif
  heif_init(nullptr);

  heif_context* ctx = heif_context_alloc();
  if (!ctx) {
    heif_deinit();
    throw std::runtime_error("failed to allocate libheif context");
  }

  // 3. Decode from memory buffer
  heif_error err = heif_context_read_from_memory_without_copy(ctx, buffer.data(), buffer.size(), nullptr);
  if (err.code != heif_error_Ok) {
    heif_context_free(ctx);
    heif_deinit();
    throw std::runtime_error("cannot read HEIC from memory: code=" + std::to_string(err.code) +
                             ", subcode=" + std::to_string(err.subcode) +
                             ", message=" + std::string(err.message));
  }

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  if (err.code != heif_error_Ok) {
    heif_context_free(ctx);
    heif_deinit();
    throw std::runtime_error("cannot get primary HEIC handle: code=" + std::to_string(err.code) +
                             ", subcode=" + std::to_string(err.subcode) +
                             ", message=" + std::string(err.message));
  }

  heif_decoding_options* options = heif_decoding_options_alloc();
  if (options) {
    options->strict_decoding = 0;
  }

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_24bit, options);

  if (options) heif_decoding_options_free(options);
  heif_image_handle_release(handle);
  heif_context_free(ctx);

  if (err.code != heif_error_Ok) {
    heif_deinit();
    throw std::runtime_error("cannot decode HEIC: code=" + std::to_string(err.code) +
                             ", subcode=" + std::to_string(err.subcode) +
                             ", message=" + std::string(err.message));
  }

  int width = heif_image_get_width(img, heif_channel_interleaved);
  int height = heif_image_get_height(img, heif_channel_interleaved);
  int stride = 0;
  const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);

  cv::Mat rgb(height, width, CV_8UC3);
  for (int y = 0; y < height; ++y) {
    std::memcpy(rgb.ptr(y), data + y * stride, width * 3);
  }
  heif_image_release(img);
  heif_deinit();

  cv::Mat bgr;
  cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
  return bgr;
}

void print_heif_info(const fs::path& path) {
  heif_context* context = heif_context_alloc();
  heif_image_handle* handle = nullptr;
  try {
    check_heif(heif_context_read_from_file(context, path.string().c_str(), nullptr), "cannot read HEIC");
    check_heif(heif_context_get_primary_image_handle(context, &handle), "cannot open HEIC image handle");

    const int width = heif_image_handle_get_width(handle);
    const int height = heif_image_handle_get_height(handle);
    const bool has_alpha = heif_image_handle_has_alpha_channel(handle) != 0;
    const int bit_depth = heif_image_handle_get_luma_bits_per_pixel(handle);

    std::string colorspace_str = "Unknown";
    std::string chroma_str = "Unknown";
    heif_colorspace preferred_colorspace = heif_colorspace_undefined;
    heif_chroma preferred_chroma = heif_chroma_undefined;

    heif_error preferred_error = heif_image_handle_get_preferred_decoding_colorspace(
        handle, &preferred_colorspace, &preferred_chroma);

    if (preferred_error.code == heif_error_Ok) {
      colorspace_str = heif_colorspace_to_string(preferred_colorspace);
      chroma_str = heif_chroma_to_string(preferred_chroma);
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
    if (handle) heif_image_handle_release(handle);
    if (context) heif_context_free(context);
    std::cout << "File: " << path.string() << "\n"
              << "  Error reading HEIC metainfo: " << e.what() << "\n";
  }
}

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

void write_heif(const fs::path& path, const cv::Mat& input, int quality,
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
