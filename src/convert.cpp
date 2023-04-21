#include "convert.hpp"

#include <mcap/mcap.hpp>
#include <spdlog/spdlog.h>

#include <sstream>

#include "proto/foxglove/CameraCalibration.pb.h"
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

// Converts a key-value map to "key1=value1;key2=value2" format
static std::string MimeKeyValues(const mcap::KeyValueMap& map) {
  std::stringstream result;
  bool first = true;
  for (const auto& [key, value] : map) {
    if (!first) {
      result << ";";
    }
    result << key << "=" << value;
    first = false;
  }
  return result.str();
}

static foxglove::CameraCalibration CreateDummyCalibration(uint32_t width, uint32_t height) {
  constexpr double EXAMPLE_FOCAL_LENGTH_MM = 1.88;  // From the Intel RealSense D435 datasheet
  constexpr double EXAMPLE_SENSOR_WIDTH_MM = 3.855;

  foxglove::CameraCalibration calibration;
  calibration.mutable_timestamp()->set_seconds(0);
  calibration.mutable_timestamp()->set_nanos(0);
  calibration.set_frame_id("video");
  calibration.set_width(width);
  calibration.set_height(height);
  calibration.set_distortion_model("plumb_bob");
  calibration.add_d(0);
  calibration.add_d(0);
  calibration.add_d(0);
  calibration.add_d(0);
  calibration.add_d(0);

  const double sensor_height_mm = (EXAMPLE_SENSOR_WIDTH_MM * height) / width;
  const double fx = width * (EXAMPLE_FOCAL_LENGTH_MM / EXAMPLE_SENSOR_WIDTH_MM);
  const double fy = height * (EXAMPLE_FOCAL_LENGTH_MM / sensor_height_mm);
  const double cx = width / 2;
  const double cy = height / 2;

  calibration.add_k(fx);
  calibration.add_k(0);
  calibration.add_k(cx);
  calibration.add_k(0);
  calibration.add_k(fy);
  calibration.add_k(cy);
  calibration.add_k(0);
  calibration.add_k(0);
  calibration.add_k(1);

  calibration.add_p(fx);
  calibration.add_p(0);
  calibration.add_p(cx);
  calibration.add_p(0);
  calibration.add_p(0);
  calibration.add_p(fy);
  calibration.add_p(cy);
  calibration.add_p(0);
  calibration.add_p(0);
  calibration.add_p(0);
  calibration.add_p(1);
  calibration.add_p(0);

  return calibration;
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
  auto status = writer.open(outputFilename, writerOpts);
  if (!status.ok()) {
    spdlog::error("Failed to open output file: {}", status.message);
    return false;
  }

  const std::string topicName = "video";
  const std::string keyframeTopicName = "video/keyframes";
  const std::string calibrationTopicName = "video/calibration";

  // Create a schema for `foxglove.CameraCalibration`. A dummy calibration is
  // written to the "video/calibration" topic to enable 3D visualization in
  // Foxglove Studio
  mcap::Schema calibrationSchema{"foxglove.CameraCalibration", "protobuf",
                                 ProtobufFdSet(foxglove::CameraCalibration::descriptor())};
  writer.addSchema(calibrationSchema);

  // Create a schema for `foxglove.CompressedImage`. This schema is used for the
  // "video" topic holding video bitstream data
  mcap::Schema schema{"foxglove.CompressedImage", "protobuf",
                      ProtobufFdSet(foxglove::CompressedImage::descriptor())};
  writer.addSchema(schema);

  // Create a channel for the "video/calibration" topic and publish a single message
  mcap::Channel calibrationChannel{calibrationTopicName, "protobuf", calibrationSchema.id, {}};
  writer.addChannel(calibrationChannel);
  const auto calibration =
    CreateDummyCalibration(uint32_t(config->codedWidth), uint32_t(config->codedHeight));
  const auto serializedCalibration = calibration.SerializeAsString();
  mcap::Message calibrationMsg{};
  calibrationMsg.channelId = calibrationChannel.id;
  calibrationMsg.dataSize = serializedCalibration.size();
  calibrationMsg.data = reinterpret_cast<const std::byte*>(serializedCalibration.data());
  status = writer.write(calibrationMsg);
  if (!status.ok()) {
    spdlog::error("Failed to write calibration message: {}", status.message);
    return false;
  }

  // Create a channel for the "video" topic
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
  const std::string mimeKeyframe = mime + ";" + MimeKeyValues(keyframeMetadata);
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
    const auto serializedMsg = image.SerializeAsString();

    if (frame.isKeyframe) {
      keyframes.emplace_back(frameNumber, frame.timestamp);
    }

    mcap::Message msg;
    msg.channelId = videoChannel.id;
    msg.sequence = frameNumber;
    msg.logTime = frame.timestamp;
    msg.publishTime = frame.timestamp;
    msg.dataSize = serializedMsg.size();
    msg.data = reinterpret_cast<const std::byte*>(serializedMsg.data());
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
