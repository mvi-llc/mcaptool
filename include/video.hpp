#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct VideoCodecInfo {
  std::string mime;
  std::string codec;
  size_t codedWidth;
  size_t codedHeight;
  std::vector<std::byte> description;
};

struct VideoFrame {
  const std::byte* data;
  size_t size;
  uint64_t timestamp;
  bool isKeyframe;
};

std::optional<VideoCodecInfo> GetVideoCodecInfo(const std::string& videoFilename);

bool ExtractVideoFrames(const std::string& videoFilename,
                        std::function<void(const VideoFrame&)> callback);
