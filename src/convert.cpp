#include "convert.hpp"

#include <mcap/mcap.hpp>
#include <spdlog/spdlog.h>

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

bool Convert(const std::string& inputFilename, const std::string& outputFilename) {
  const auto codecInfo = GetVideoCodecInfo(inputFilename);
  if (!codecInfo) {
    return false;
  }

  spdlog::debug("Input is {}x{} {}; codecs=\"{}\"", codecInfo->codedWidth, codecInfo->codedHeight,
                codecInfo->mime, codecInfo->codec);

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

  // Create a schema for `foxglove.CompressedImage`
  mcap::Schema schema{"foxglove.CompressedImage", "protobuf",
                      ProtobufFdSet(foxglove::CompressedImage::descriptor())};
  writer.addSchema(schema);

  // Create a topic for the "video" topic
  mcap::KeyValueMap videoMetadata{
    {"keyframe_index", "video/keyframes"},
    {"video:coded_width", std::to_string(codecInfo->codedWidth)},
    {"video:coded_height", std::to_string(codecInfo->codedHeight)},
    {"video:codec", codecInfo->codec},
    {"video:mime", codecInfo->mime},
  };
  if (!codecInfo->description.empty()) {
    videoMetadata["video:description"] = BytesToHex(codecInfo->description);
  }
  mcap::Channel videoChannel{"video", "protobuf", schema.id, videoMetadata};
  writer.addChannel(videoChannel);

  // Create a topic fo the "video/keyframes" topic
  mcap::Channel keyframeChannel{"video/keyframes", "", 0, {}};
  writer.addChannel(keyframeChannel);

  const std::string mime = codecInfo->mime;
  const std::string mimeKeyframe = mime + "; keyframe";
  uint32_t frameNumber = 0;
  std::vector<std::pair<uint32_t, uint64_t>> keyframes;

  // Write video data to the "video" topic
  const bool result = ExtractVideoFrames(inputFilename, [&](const VideoFrame& frame) {
    foxglove::CompressedImage image;
    image.mutable_timestamp()->set_seconds(frame.timestamp / 1000000000);
    image.mutable_timestamp()->set_nanos(frame.timestamp % 1000000000);
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
