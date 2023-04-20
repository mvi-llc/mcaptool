#include "split.hpp"

#include <mcap/mcap.hpp>

#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct OutputMcap {
  std::string filename;
  mcap::Channel channel;
  mcap::Schema schema;
  std::unique_ptr<mcap::McapWriter> writer;
  size_t messageCount;

  OutputMcap(const std::string& name)
      : filename(name)
      , messageCount(0) {}
};

bool Split(const std::string& inputFilename, const std::string& outputDir) {
  // Open the input file
  mcap::McapReader reader;
  auto status = reader.open(inputFilename);
  if (!status.ok()) {
    std::cerr << "Failed to open input file: " << status.message << "\n";
    return false;
  }

  const auto& profile = reader.header()->profile;

  // Create the output directory (mkdir -p) if it doesn't exist
  if (!std::filesystem::exists(outputDir)) {
    if (!std::filesystem::create_directories(outputDir)) {
      std::cerr << "Failed to create output directory: " << outputDir << "\n";
      return false;
    }
  }

  // Get the list of all channels in the input file
  status = reader.readSummary(mcap::ReadSummaryMethod::AllowFallbackScan);
  if (!status.ok()) {
    std::cerr << "Failed to read MCAP summary: " << status.message << "\n";
    return false;
  }
  if (!reader.statistics().has_value()) {
    std::cerr << "Failed to retrieve MCAP statistics after summary parsing\n";
    return false;
  }
  const auto& stats = *reader.statistics();

  // FIXME: Support channels with schemaId=0

  // FIXME: Support multiple channels publishing to the same topic as long as
  // they all share the same message encoding and schemaId
  std::unordered_map<mcap::ChannelId, OutputMcap> outputMcaps;
  std::unordered_set<std::string> outputFilenames;

  // Create a map of output MCAP files, one per channel
  for (const auto& [channelId, channelPtr] : reader.channels()) {
    const auto& channel = *channelPtr;
    const auto& schema = *reader.schema(channel.schemaId);

    // Sanitize the topic name for use as a filename. Strip any leading '/' and
    // replace all non-alphanumeric characters with underscores
    std::string filename = channel.topic;
    while (!filename.empty() && filename[0] == '/') {
      filename = filename.substr(1);
    }
    if (filename.empty()) {
      std::cerr << "Failed to sanitize topic name for use as a filename: \"" << channel.topic
                << "\"\n";
    }
    if (filename == "index") {
      filename = "index_";
    }
    std::replace_if(
      filename.begin(), filename.end(),
      [](char c) {
        return !std::isalnum(c);
      },
      '_');
    if (outputFilenames.count(filename) > 0) {
      std::cerr << "Failed to create output file: duplicate filename \"" << filename << "\"\n";
      return false;
    }
    outputFilenames.emplace(filename);

    // Create the output file
    const auto outputFilename = outputDir + "/" + filename + ".mcap";
    OutputMcap outputMcap{outputFilename};
    outputMcap.channel = channel;
    outputMcap.schema = schema;

    mcap::McapWriterOptions writerOpts{profile};

    // Check if the schemaName contains the word "compressed" (case-insensitive)
    // and disable compression if so
    const char* compressed = "compressed";
    if (std::search(schema.name.begin(), schema.name.end(), compressed, compressed + 10,
                    [](char a, char b) {
                      return std::tolower(a) == std::tolower(b);
                    }) != schema.name.end()) {
      writerOpts.compression = mcap::Compression::None;
    }

    // Open this output file
    outputMcap.writer = std::make_unique<mcap::McapWriter>();
    status = outputMcap.writer->open(outputFilename, writerOpts);
    if (!status.ok()) {
      std::cerr << "Failed to open output file: " << status.message << "\n";
      return false;
    }

    // schema.id is overwritten by McapWriter::addSchema()
    outputMcap.writer->addSchema(outputMcap.schema);
    outputMcap.channel.schemaId = outputMcap.schema.id;

    // channel.id is overwritten by McapWriter::addChannel()
    outputMcap.writer->addChannel(outputMcap.channel);
    outputMcaps.emplace(channelId, std::move(outputMcap));
  }

  // Read all messages from the input file and write them to the output files
  for (const auto& msgView : reader.readMessages()) {
    // Get the output MCAP file for this channel
    auto& outputMcap = outputMcaps.at(msgView.message.channelId);

    // Copy the message and set the channel ID to the output channel ID
    mcap::Message msg{msgView.message};
    msg.channelId = outputMcap.channel.id;

    // Write the message to the output file
    status = outputMcap.writer->write(msg);
    if (!status.ok()) {
      std::cerr << "Failed to write message to \"" << outputMcap.filename
                << "\": " << status.message << "\n";
      return false;
    }

    outputMcap.messageCount++;
  }

  // Create the index.mcap file containing schemas and channels but no messages
  const auto indexFilename = outputDir + "/index.mcap";
  mcap::McapWriter indexWriter;
  status = indexWriter.open(indexFilename, mcap::McapWriterOptions{"index"});
  if (!status.ok()) {
    std::cerr << "Failed to open index file: " << status.message << "\n";
    return false;
  }

  // Write a metadata record to the index file with start and end timestamps
  mcap::Metadata metadata;
  metadata.name = "mcapindex";
  metadata.metadata["startTime"] = std::to_string(stats.messageStartTime);
  metadata.metadata["endTime"] = std::to_string(stats.messageEndTime);
  status = indexWriter.write(metadata);
  if (!status.ok()) {
    std::cerr << "Failed to write metadata to index file: " << status.message << "\n";
    return false;
  }

  // Write schemas and channels to the index file
  for (const auto& [channelId, outputMcap] : outputMcaps) {
    auto schema = outputMcap.schema;
    indexWriter.addSchema(schema);

    auto channel = outputMcap.channel;
    channel.schemaId = schema.id;
    channel.metadata["mcapindex:filename"] = outputMcap.filename;
    channel.metadata["mcapindex:messageCount"] = std::to_string(outputMcap.messageCount);
    indexWriter.addChannel(channel);
  }

  // TODO: Write all other Metadata and Attachment records to the index file

  indexWriter.close();

  // Close all output files
  for (auto& [channelId, outputMcap] : outputMcaps) {
    outputMcap.writer->close();
    outputMcap.writer.reset();
  }

  return true;
}
