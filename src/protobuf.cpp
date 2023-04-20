#include "protobuf.hpp"

#include <google/protobuf/descriptor.pb.h>

#include <unordered_set>

// Recursively adds all `fd` dependencies to `fdSet`
static void ProtobufFdSetInternal(google::protobuf::FileDescriptorSet& fdSet,
                                  std::unordered_set<std::string>& files,
                                  const google::protobuf::FileDescriptor* fd) {
  for (int i = 0; i < fd->dependency_count(); ++i) {
    const auto* dep = fd->dependency(i);
    auto [_, inserted] = files.insert(dep->name());
    if (!inserted) continue;
    ProtobufFdSetInternal(fdSet, files, fd->dependency(i));
  }
  fd->CopyTo(fdSet.add_file());
}

std::string ProtobufFdSet(const google::protobuf::Descriptor* d) {
  std::string res;
  std::unordered_set<std::string> files;
  google::protobuf::FileDescriptorSet fdSet;
  ProtobufFdSetInternal(fdSet, files, d->file());
  return fdSet.SerializeAsString();
}
