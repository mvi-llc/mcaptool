#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct VideoDecoderConfig {
  /** The mime type of the bitstream: ["video/avc", "video/hevc", "video/AV1"] */
  std::string mime;
  /** A codec-specific configuration identifier, e.g. "avc1.640028" */
  std::string codec;
  /** Width of the VideoFrame in pixels, potentially including non-visible padding, and prior to
   * considering potential ratio adjustments. */
  size_t codedWidth;
  /** Height of the VideoFrame in pixels, potentially including non-visible padding, and prior to
   * considering potential ratio adjustments. */
  size_t codedHeight;
  /** A sequence of codec specific bytes, commonly known as extradata. */
  std::vector<std::byte> description;
};

struct VideoFrame {
  const std::byte* data;
  size_t size;
  uint64_t timestamp;
  bool isKeyframe;
};

std::optional<VideoDecoderConfig> GetVideoDecoderConfig(const std::string& videoFilename);

bool ExtractVideoFrames(const std::string& videoFilename,
                        std::function<void(const VideoFrame&)> callback);
