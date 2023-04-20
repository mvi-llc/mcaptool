#define MCAP_IMPLEMENTATION

#include <argparse/argparse.hpp>
#include <mcap/mcap.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "convert.hpp"
#include "split.hpp"

int main(int argc, char** argv) {
  spdlog::set_level(spdlog::level::debug);

  argparse::ArgumentParser program("mcaptool", "0.1.0");

  argparse::ArgumentParser splitCommand("split");
  splitCommand.add_description("Split a MCAP file into multiple files grouped by channels.");
  splitCommand.add_argument("input.mcap").help("Input MCAP file to split.");
  splitCommand.add_argument("output_dir").help("Output directory to write split MCAP files to.");

  argparse::ArgumentParser convertCommand("convert");
  convertCommand.add_description("Convert an MP4 video file to a MCAP file.");
  convertCommand.add_argument("input.mp4").help("Input MP4 file to convert.");
  convertCommand.add_argument("output.mcap").help("Output MCAP file to create.");

  program.add_subparser(splitCommand);
  program.add_subparser(convertCommand);

  try {
    program.parse_args(argc, argv);
  } catch (const std::runtime_error& err) {
    std::cout << err.what() << "\n";
    std::cout << program;
    return 0;
  }

  if (program.is_subcommand_used("split")) {
    const std::string inputFilename = splitCommand.get("input.mcap");
    const std::string outputDir = splitCommand.get("output_dir");
    return Split(inputFilename, outputDir) ? 0 : 1;
  } else if (program.is_subcommand_used("convert")) {
    const std::string inputFilename = convertCommand.get("input.mp4");
    const std::string outputFilename = convertCommand.get("output.mcap");
    return Convert(inputFilename, outputFilename) ? 0 : 1;
  } else {
    // Print help
    std::cout << program;
    return 0;
  }
}
