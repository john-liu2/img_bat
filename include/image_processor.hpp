#pragma once

#include <vector>
#include "options.hpp"

std::vector<fs::path> collect_files(const Options& opt);
void print_info_for_file(const fs::path& path);
void process_one(const fs::path& input, const Options& opt);
int run_batch_processor(const Options& opt);
