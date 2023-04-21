#include "video.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <optional>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

struct AV1CodecConfigurationRecord {
  uint8_t profile;
  uint8_t level;
  char tier;
  uint8_t bitDepth;
  uint8_t monochrome;
  uint8_t chromaSubsampling;
  uint8_t colorPrimaries;
  uint8_t transferCharacteristics;
  uint8_t matrixCoefficients;
  uint8_t videoFullRangeFlag;
  uint8_t initialPresentationDelayPresent;
  uint8_t initialPresentationDelayMinusOne;
  std::vector<uint8_t> configOBUs;
};

/**
 * Parse an AV1CodecConfigurationRecord from a buffer, such as `extradata` from an AVCodecParameters
 * when parsing an MP4 file.
 *
 * This code is based on code from the Chromium project, licensed under the BSD 3-clause license.
 * <https://chromium.googlesource.com/chromium/src/media/+/master/formats/mp4/box_definitions.cc#897>
 */
static std::optional<AV1CodecConfigurationRecord> ParseAV1CodecConfigurationRecord(
  const std::byte* data, size_t size) {
  // Parse the AV1CodecConfigurationRecord, which has the following format:
  // unsigned int (1) marker = 1;
  // unsigned int (7) version = 1;
  // unsigned int (3) seq_profile;
  // unsigned int (5) seq_level_idx_0;
  // unsigned int (1) seq_tier_0;
  // unsigned int (1) high_bitdepth;
  // unsigned int (1) twelve_bit;
  // unsigned int (1) monochrome;
  // unsigned int (1) chroma_subsampling_x;
  // unsigned int (1) chroma_subsampling_y;
  // unsigned int (2) chroma_sample_position;
  // unsigned int (3) reserved = 0;
  //
  // unsigned int (1) initial_presentation_delay_present;
  // if (initial_presentation_delay_present) {
  //   unsigned int (4) initial_presentation_delay_minus_one;
  // } else {
  //   unsigned int (4) reserved = 0;
  // }
  //
  // unsigned int (8)[] configOBUs;

  // TODO: Implement this
  (void)data;
  (void)size;
  return {};
  // uint8_t av1c_byte = 0;
  // RCHECK(reader->Read1(&av1c_byte));
  // const uint8_t av1c_marker = av1c_byte >> 7;
  // if (!av1c_marker) {
  //   MEDIA_LOG(ERROR, media_log) << "Unsupported av1C: marker unset.";
  //   return false;
  // }
  // const uint8_t av1c_version = av1c_byte & 0b01111111;
  // if (av1c_version != 1) {
  //   MEDIA_LOG(ERROR, media_log) << "Unsupported av1C: unexpected version number: " <<
  //   av1c_version; return false;
  // }
  // RCHECK(reader->Read1(&av1c_byte));
  // const uint8_t seq_profile = av1c_byte >> 5;
  // switch (seq_profile) {
  //   case 0:
  //     profile = AV1PROFILE_PROFILE_MAIN;
  //     break;
  //   case 1:
  //     profile = AV1PROFILE_PROFILE_HIGH;
  //     break;
  //   case 2:
  //     profile = AV1PROFILE_PROFILE_PRO;
  //     break;
  //   default:
  //     MEDIA_LOG(ERROR, media_log) << "Unsupported av1C: unknown profile 0x" << std::hex <<
  //     seq_profile; return false;
  // }
  // // The remaining fields are ignored since we don't care about them yet.
  // return true;
}

std::optional<VideoDecoderConfig> GetVideoDecoderConfig(const std::string& videoFilename) {
  AVFormatContext* formatCtx = nullptr;
  if (avformat_open_input(&formatCtx, videoFilename.data(), nullptr, nullptr) != 0) {
    spdlog::error("Failed to open \"{}\"", videoFilename);
    return {};
  }

  if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
    spdlog::error("Failed to find stream info for \"{}\"", videoFilename);
    avformat_close_input(&formatCtx);
    return {};
  }

  int videoStreamIndex = -1;
  for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
    if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStreamIndex = int(i);
      break;
    }
  }

  if (videoStreamIndex >= 0) {
    const AVStream* videoStream = formatCtx->streams[videoStreamIndex];
    AVCodecParameters* codecParams = videoStream->codecpar;
    const AVCodecDescriptor* codecDesc = avcodec_descriptor_get(codecParams->codec_id);
    if (!codecDesc) {
      spdlog::error("Failed to get codec descriptor for \"{}\"", videoFilename);
      avformat_close_input(&formatCtx);
      return {};
    }

    // Check if the video has B-frames
    const AVCodec* avCodec = avcodec_find_decoder(codecParams->codec_id);
    AVCodecContext* codecCtx = avcodec_alloc_context3(avCodec);
    avcodec_parameters_to_context(codecCtx, codecParams);
    avcodec_open2(codecCtx, avCodec, nullptr);
    const bool hasBFrames = bool(codecCtx->has_b_frames);
    avcodec_free_context(&codecCtx);
    if (hasBFrames) {
      spdlog::error("B-frames are not supported");
      avformat_close_input(&formatCtx);
      return {};
    }

    // FIXME: Confirm these are coded width/height (bitmap size) and not display width/height
    const size_t codedWidth = size_t(codecParams->width);
    const size_t codedHeight = size_t(codecParams->height);

    if (codecParams->codec_id == AV_CODEC_ID_HEVC) {
      // ${cccc}.${PP}.${C}.${T}${LL}.${CC}
      // HEVC codec format is <fourcc>.<profile>.<compatibility>.<tier><level>.B<flags>
      // Examples: hev1.1.6.L93.B0, hvc1.2.4.L153.B0
      // See
      // <https://www.w3.org/TR/webcodecs-hevc-codec-registration/#fully-qualified-codec-strings>
      const uint8_t* extradata = codecParams->extradata;
      if (codecParams->extradata_size < 23) {
        spdlog::error("HEVC extradata is too small (%d bytes) for \"{}\"",
                      codecParams->extradata_size, videoFilename);
        avformat_close_input(&formatCtx);
        return {};
      }

      const uint8_t generalTierFlag = (extradata[1] >> 5) & 0x1;
      const uint32_t generalProfileCompatibilityFlags =
        uint32_t((extradata[2] << 24) | (extradata[3] << 16) | (extradata[4] << 8) | extradata[5]);
      const uint8_t compatibilityIdc = (generalProfileCompatibilityFlags >> 16) & 0xFF;

      const int profile = codecParams->profile;
      const uint8_t compatibility = compatibilityIdc;
      const char tier = generalTierFlag ? 'H' : 'L';
      const int level = codecParams->level;
      const int flags = 0;
      const std::string mime = "video/hevc";
      const std::string codec =
        fmt::format("hev1.{}.{}.{}{}.B{}", profile, compatibility, tier, level, flags);

      avformat_close_input(&formatCtx);
      return VideoDecoderConfig{mime, codec, codedWidth, codedHeight, {}};
    } else if (codecParams->codec_id == AV_CODEC_ID_H264) {
      // H264 codec format is <fourcc>.<profile_idc>.<profile_compatibility>.<level_idc>
      // Where profile_idc, profile_compatibility, and level_idc are one byte hex values (two
      // characters) Examples: avc1.640028, avc1.4D401E See
      // <https://www.w3.org/TR/webcodecs-avc-codec-registration/#fully-qualified-codec-strings> and
      // <https://www.rfc-editor.org/rfc/rfc6381#section-3.6>
      const uint8_t* extradata = codecParams->extradata;
      const size_t extradataSize = size_t(codecParams->extradata_size);

      if (extradataSize <= 9 || extradata[0] != 1) {
        spdlog::error("Error: Invalid H.264 extradata in \"{}\"", videoFilename);
        avformat_close_input(&formatCtx);
        return {};
      }

      const uint8_t profileIdc = extradata[1];
      const uint8_t profileCompatibility = extradata[2];
      const uint8_t levelIdc = extradata[3];
      const std::string mime = "video/avc";
      const std::string codec =
        fmt::format("avc1.{:02x}{:02x}{:02x}", profileIdc, profileCompatibility, levelIdc);

      avformat_close_input(&formatCtx);
      return VideoDecoderConfig{mime, codec, codedWidth, codedHeight, {}};
    } else if (codecParams->codec_id == AV_CODEC_ID_AV1) {
      // AV1 codec format is
      // <fourcc>.<profile>.<level><tier>.<bitDepth>.<monochrome>.<chromaSubsampling>.
      //                     <colorPrimaries>.<transferCharacteristics>.<matrixCoefficients>.<videoFullRangeFlag>
      // These values are obtained from the AV1CodecConfigurationRecord in the extradata.
      // Example: av01.0.04M.10.0.112.09.16.09.0,
      // See <https://www.w3.org/TR/webcodecs-av1-codec-registration/#fully-qualified-codec-strings>
      // and <https://aomediacodec.github.io/av1-isobmff/#codecsparam>
      assert(false && "AV1 codec not yet supported");
      const std::byte* extradata = reinterpret_cast<const std::byte*>(codecParams->extradata);
      const size_t extradataSize = size_t(codecParams->extradata_size);

      const auto av1Config = ParseAV1CodecConfigurationRecord(extradata, extradataSize);
      if (!av1Config) {
        spdlog::error("Error: Invalid AV1 extradata in \"{}\"", videoFilename);
        avformat_close_input(&formatCtx);
        return {};
      }

      const std::string mime = "video/AV1";
      const std::string codec = fmt::format(
        "av01.{}.{:02}{}.{:02}.{}.{:02}.{:02}.{:02}.{:02}.{}", av1Config->profile, av1Config->level,
        av1Config->tier, av1Config->bitDepth, av1Config->monochrome, av1Config->chromaSubsampling,
        av1Config->colorPrimaries, av1Config->transferCharacteristics,
        av1Config->matrixCoefficients, av1Config->videoFullRangeFlag);

      // Copy `extradata` to `description`
      std::vector<std::byte> description;
      description.assign(extradata, extradata + extradataSize);

      avformat_close_input(&formatCtx);
      return VideoDecoderConfig{mime, codec, codedWidth, codedHeight, description};
    }
  }

  spdlog::error("Failed to find compatible video stream in \"{}\"", videoFilename);
  avformat_close_input(&formatCtx);
  return {};
}

bool ExtractVideoFrames(const std::string& videoFilename,
                        std::function<void(const VideoFrame&)> callback) {
  AVFormatContext* formatCtx = nullptr;
  if (avformat_open_input(&formatCtx, videoFilename.c_str(), nullptr, nullptr) != 0) {
    spdlog::error("Failed to open \"{}\"", videoFilename);
    return false;
  }

  if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
    spdlog::error("Failed to find stream info for \"{}\"", videoFilename);
    avformat_close_input(&formatCtx);
    return false;
  }

  const int videoStreamIndex =
    av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (videoStreamIndex < 0) {
    spdlog::error("Failed to find video stream in \"{}\"", videoFilename);
    return false;
  }

  AVStream* stream = formatCtx->streams[videoStreamIndex];
  const AVCodecID codecId = stream->codecpar->codec_id;

  AVPacket* packet = av_packet_alloc();
  AVPacket* packetFiltered = av_packet_alloc();

  auto cleanup = [&]() {
    if (packet->data) av_packet_unref(packet);
    if (packetFiltered->data) av_packet_unref(packetFiltered);
    av_packet_free(&packet);
    av_packet_free(&packetFiltered);
    avformat_close_input(&formatCtx);
  };

  if (codecId == AV_CODEC_ID_HEVC || codecId == AV_CODEC_ID_H264) {
    // Construct a bitstream filter to convert the H.264/HEVC stream to Annex B format
    // FIXME: Try writing `avc` bitstream instead of Annex B format
    const AVBitStreamFilter* bitstreamFilter =
      av_bsf_get_by_name(codecId == AV_CODEC_ID_HEVC ? "hevc_mp4toannexb" : "h264_mp4toannexb");
    if (!bitstreamFilter) {
      spdlog::error("av_bsf_get_by_name() failed for \"{}\"", videoFilename);
      cleanup();
      return false;
    }
    AVBSFContext* bsfContext = nullptr;
    if (av_bsf_alloc(bitstreamFilter, &bsfContext) < 0) {
      spdlog::error("av_bsf_alloc() failed for \"{}\"", videoFilename);
      cleanup();
      return false;
    }
    bsfContext->par_in = stream->codecpar;
    if (av_bsf_init(bsfContext) < 0) {
      spdlog::error("av_bsf_init() failed for \"{}\"", videoFilename);
      cleanup();
      return false;
    }

    // Process all packets in the video file
    while (true) {
      if (packet->data) av_packet_unref(packet);
      if (packetFiltered->data) av_packet_unref(packetFiltered);

      // Read packets until we find a packet from the relevant stream
      int err = 0;
      while ((err = av_read_frame(formatCtx, packet)) >= 0 &&
             packet->stream_index != videoStreamIndex) {
        av_packet_unref(packet);
      }

      // Check if an expected (EOF) or unexpected error occurred
      if (err < 0) {
        const int64_t packetPos = packet->pos;
        if (packet->data) av_packet_unref(packet);
        if (packetFiltered->data) av_packet_unref(packetFiltered);
        avformat_close_input(&formatCtx);

        if (err != AVERROR_EOF) {
          // Unexpected error
          char errStr[128] = {};
          av_strerror(err, errStr, sizeof(errStr));
          spdlog::error("av_read_frame() failed at position {} in \"{}\": {}", packetPos,
                        videoFilename, errStr);
          cleanup();
          return false;
        }

        // End of file reached
        cleanup();
        return true;
      }

      // Send the packet to the bitstream filter
      if (av_bsf_send_packet(bsfContext, packet) < 0) {
        spdlog::error("av_bsf_send_packet() failed for \"{}\"", videoFilename);
        cleanup();
        return false;
      }

      int recvStatus = 1;
      while (recvStatus > 0) {
        recvStatus = av_bsf_receive_packet(bsfContext, packetFiltered);
        if (recvStatus < 0 && recvStatus != AVERROR(EAGAIN) && recvStatus != AVERROR_EOF) {
          // Unexpected error
          char errStr[128]{};
          av_strerror(recvStatus, errStr, sizeof(errStr));
          spdlog::error("av_bsf_receive_packet() failed for \"{}\": {}", videoFilename, errStr);
          cleanup();
          return false;
        }

        if (recvStatus >= 0) {
          // A filtered packet was produced, construct a VideoFrame and fire the callback
          VideoFrame frame;
          frame.data = reinterpret_cast<const std::byte*>(packetFiltered->data);
          frame.size = size_t(packetFiltered->size);
          frame.timestamp =
            uint64_t(double(packetFiltered->pts) * av_q2d(stream->time_base) * 1e9);  // [ns]
          frame.isKeyframe = packetFiltered->flags & AV_PKT_FLAG_KEY;
          callback(frame);
        }

        av_packet_unref(packetFiltered);
      }
    }

    // We should never reach this point
    assert(false && "Unexpected code path");
  } else if (codecId == AV_CODEC_ID_AV1) {
    assert(false && "AV1 codec not yet supported");
  }

  spdlog::error("Failed to find compatible video stream in \"{}\"", videoFilename);
  cleanup();
  return false;
}
