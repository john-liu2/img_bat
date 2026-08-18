#include <iostream>
#include "options.hpp"
#include "metadata.hpp"
#include "image_processor.hpp"

#ifdef IMG_BAT_WITH_LIBHEIF
#include <libheif/heif.h>

static void print_libheif_diagnostics()
{
    std::cerr << "=== libheif diagnostics ===\n";
    // Runtime library version.
    std::cerr << "libheif version: " << heif_get_version() << "\n";

    // Plugin search paths.
    const char* const* plugin_dirs = heif_get_plugin_directories();
    if (plugin_dirs) {
        std::cerr << "libheif plugin directories:\n";

        if (!plugin_dirs[0]) {
            std::cerr << "  (none; plugins disabled)\n";
        } else {
            for (int i = 0; plugin_dirs[i] != nullptr; ++i) {
                std::cerr << "  " << plugin_dirs[i] << "\n";
            }
        }
        heif_free_plugin_directories(plugin_dirs);
    }

    // HEVC / HEIC decoders.
    constexpr int MAX_DECODERS = 20;
    const heif_decoder_descriptor* decoders[MAX_DECODERS];

    int decoder_count = heif_get_decoder_descriptors(
        heif_compression_HEVC,
        decoders,
        MAX_DECODERS
    );
    std::cerr << "HEVC decoders: " << decoder_count << "\n";

    for (int i = 0; i < decoder_count; ++i) {
        const char* id = heif_decoder_descriptor_get_id_name(decoders[i]);
        const char* name = heif_decoder_descriptor_get_name(decoders[i]);

        std::cerr << "  - "
                  << (id ? id : "(no id)")
                  << " = "
                  << (name ? name : "(no name)")
                  << "\n";
    }

    // HEVC / HEIC encoders.
    constexpr int MAX_ENCODERS = 20;
    const heif_encoder_descriptor* encoders[MAX_ENCODERS];

    int encoder_count = heif_get_encoder_descriptors(
        heif_compression_HEVC,
        nullptr,
        encoders,
        MAX_ENCODERS
    );
    std::cerr << "HEVC encoders: " << encoder_count << "\n";

    for (int i = 0; i < encoder_count; ++i) {
        const char* id = heif_encoder_descriptor_get_id_name(encoders[i]);
        const char* name = heif_encoder_descriptor_get_name(encoders[i]);

        std::cerr << "  - "
                  << (id ? id : "(no id)")
                  << " = "
                  << (name ? name : "(no name)")
                  << "\n";
    }

    // Simple yes/no checks.
    std::cerr << "HEVC decoder available: "
              << (heif_have_decoder_for_format(heif_compression_HEVC)
                      ? "yes"
                      : "NO")
              << "\n";

    std::cerr << "HEVC encoder available: "
              << (heif_have_encoder_for_format(heif_compression_HEVC)
                      ? "yes"
                      : "NO")
              << "\n";
    std::cerr << "=== end libheif diagnostics ===\n";
}
#endif

int main(int argc, char** argv) {
#ifdef IMG_BAT_WITH_LIBHEIF
  heif_init(nullptr);
  print_libheif_diagnostics();
#endif

  int result = 0;
  try {
    const Options opt = parse_args(argc, argv);
    XmpRuntime xmp_runtime(opt.strip_all || opt.strip_gps);

    result = run_batch_processor(opt);

  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << "\nUse --help for usage.\n";
    result = 2;
  }

#ifdef IMG_BAT_WITH_LIBHEIF
  heif_deinit();
#endif
  return result;
}
