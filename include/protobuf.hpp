#pragma once

#include <string>

namespace google {
namespace protobuf {
class Descriptor;
}  // namespace protobuf
}  // namespace google

// Returns a serialized google::protobuf::FileDescriptorSet
std::string ProtobufFdSet(const google::protobuf::Descriptor* d);
