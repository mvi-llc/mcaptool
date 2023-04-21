#include "convert.hpp"

#include <mcap/mcap.hpp>
#include <spdlog/spdlog.h>

#include <sstream>

#include "proto/foxglove/CompressedImage.pb.h"
#include "protobuf.hpp"
#include "video.hpp"

static std::string BytesToHex(const mcap::ByteArray& bytes) {
  std::string result;
  result.reserve(bytes.size() * 2);
  for (const auto b : bytes) {
    result += fmt::format("{:02x}", uint8_t(b));
  }
  return result;
}

// NOTE: This does not do any escaping on keys or values
static std::string ToJson(const mcap::KeyValueMap& map) {
  std::stringstream result;
  result << "{";
  bool first = true;
  for (const auto& [key, value] : map) {
    if (!first) {
      result << ",";
    }
    first = false;
    result << fmt::format("\"{}\":\"{}\"", key, value);
  }
  result << "}";
  return result.str();
}

bool Convert(const std::string& inputFilename, const std::string& outputFilename) {
  const auto config = GetVideoDecoderConfig(inputFilename);
  if (!config) {
    return false;
  }

  spdlog::debug("Input is {}x{} {}; codecs=\"{}\"", config->codedWidth, config->codedHeight,
                config->mime, config->codec);

  // Open the output file
  mcap::McapWriter writer;
  mcap::McapWriterOptions writerOpts{""};
  writerOpts.compression = mcap::Compression::None;
  writerOpts.noChunkCRC = true;
  const auto status = writer.open(outputFilename, writerOpts);
  if (!status.ok()) {
    std::cerr << "Failed to open output file: " << status.message << "\n";
    return false;
  }

  const std::string topicName = "video";
  const std::string keyframeTopicName = "video/keyframes";

  // Create a schema for `foxglove.CompressedImage`
  mcap::Schema schema{"foxglove.CompressedImage", "protobuf",
                      ProtobufFdSet(foxglove::CompressedImage::descriptor())};
  writer.addSchema(schema);

  // Create a topic for the "video" topic
  mcap::KeyValueMap videoMetadata{
    {"keyframe_index", keyframeTopicName},
    {"video:coded_width", std::to_string(config->codedWidth)},
    {"video:coded_height", std::to_string(config->codedHeight)},
    {"video:codec", config->codec},
    {"video:mime", config->mime},
  };
  mcap::KeyValueMap keyframeMetadata{
    {"keyframe_index", keyframeTopicName},
    {"coded_width", std::to_string(config->codedWidth)},
    {"coded_height", std::to_string(config->codedHeight)},
    {"codec", config->codec},
  };
  if (!config->description.empty()) {
    videoMetadata["video:description"] = BytesToHex(config->description);
    keyframeMetadata["description"] = BytesToHex(config->description);
  }
  mcap::Channel videoChannel{topicName, "protobuf", schema.id, videoMetadata};
  writer.addChannel(videoChannel);

  // Create a topic fo the "video/keyframes" topic
  mcap::Channel keyframeChannel{keyframeTopicName, "", 0, {}};
  writer.addChannel(keyframeChannel);

  const std::string mime = config->mime;
  const std::string mimeKeyframe = mime + ";" + ToJson(keyframeMetadata);
  uint32_t frameNumber = 0;
  std::vector<std::pair<uint32_t, uint64_t>> keyframes;

  // Write video data to the "video" topic
  const bool result = ExtractVideoFrames(inputFilename, [&](const VideoFrame& frame) {
    foxglove::CompressedImage image;
    image.mutable_timestamp()->set_seconds(int64_t(frame.timestamp / 1000000000));
    image.mutable_timestamp()->set_nanos(int32_t(frame.timestamp % 1000000000));
    image.set_frame_id("video");
    image.set_format(frame.isKeyframe ? mimeKeyframe : mime);
    image.set_data(frame.data, frame.size);

    if (frame.isKeyframe) {
      keyframes.emplace_back(frameNumber, frame.timestamp);
    }

    mcap::Message msg;
    msg.channelId = videoChannel.id;
    msg.sequence = frameNumber;
    msg.logTime = frame.timestamp;
    msg.publishTime = frame.timestamp;
    msg.dataSize = frame.size;
    msg.data = frame.data;
    const auto writeStatus = writer.write(msg);
    if (!writeStatus.ok()) {
      spdlog::error("Failed to write video frame {} ({} bytes): {}", frameNumber, frame.size,
                    writeStatus.message);
    }

    frameNumber++;
  });

  if (!result) {
    spdlog::error("Failed to extract video frames from \"{}\"", inputFilename);
  }

  // Close the current chunk to ensure keyframes are written to a separate chunk
  writer.closeLastChunk();

  // Write empty keyframe messages to the "video/keyframes" topic
  for (const auto& [sequence, timestamp] : keyframes) {
    mcap::Message msg;
    msg.channelId = keyframeChannel.id;
    msg.sequence = sequence;
    msg.logTime = timestamp;
    msg.publishTime = timestamp;
    msg.dataSize = 0;
    msg.data = nullptr;
    const auto writeStatus = writer.write(msg);
    if (!writeStatus.ok()) {
      spdlog::error("Failed to write keyframe message {}: {}", sequence, writeStatus.message);
    }
  }

  writer.close();
  spdlog::debug("Wrote {} video frames ({} keyframes) to \"{}\"", frameNumber, keyframes.size(),
                outputFilename);

  return true;
}
