#include <iostream>
#include "options.hpp"
#include "metadata.hpp"
#include "image_processor.hpp"

#ifdef IMG_BAT_WITH_LIBHEIF
#include <libheif/heif.h>
#endif

int main(int argc, char** argv) {
#ifdef IMG_BAT_WITH_LIBHEIF
  heif_init(nullptr);
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
