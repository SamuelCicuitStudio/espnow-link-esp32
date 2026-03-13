#include "espnow_link/codec.hpp"

namespace espnow_link {

bool isBuiltInCodecId(CodecId codec_id) {
  return codec_id >= kCodecIdDefault && codec_id <= kCodecIdBinaryMap;
}

CodecRegistry& CodecRegistry::instance() {
  static CodecRegistry g_registry;
  return g_registry;
}

bool CodecRegistry::registerCodec(IProfileCodec* codec) {
  if (codec == nullptr || codec->codecId() == 0 || codec->codecName() == nullptr) {
    return false;
  }

  for (auto* c : codecs_) {
    if (c == nullptr) {
      continue;
    }
    if (c->codecId() == codec->codecId()) {
      return true;
    }
  }

  codecs_.push_back(codec);
  return true;
}

IProfileCodec* CodecRegistry::find(CodecId codec_id) const {
  for (auto* c : codecs_) {
    if (c != nullptr && c->codecId() == codec_id) {
      return c;
    }
  }
  return nullptr;
}

IProfileCodec* CodecRegistry::findByName(const std::string& codec_name) const {
  for (auto* c : codecs_) {
    if (c != nullptr && codec_name == c->codecName()) {
      return c;
    }
  }
  return nullptr;
}

std::vector<IProfileCodec*> CodecRegistry::list() const {
  return codecs_;
}

void registerBuiltInCodecs(CodecRegistry& registry) {
  static DefaultProfileCodec g_default_codec;
  static CompactIndexedProfileCodec g_compact_indexed_codec;
  static PackedTlvProfileCodec g_packed_tlv_codec;
  static VarintTlvProfileCodec g_varint_tlv_codec;
  static DeltaBinaryProfileCodec g_delta_binary_codec;
  static CborLiteProfileCodec g_cbor_lite_codec;
  static BinaryMapProfileCodec g_binary_map_codec;

  (void)registry.registerCodec(&g_default_codec);
  (void)registry.registerCodec(&g_compact_indexed_codec);
  (void)registry.registerCodec(&g_packed_tlv_codec);
  (void)registry.registerCodec(&g_varint_tlv_codec);
  (void)registry.registerCodec(&g_delta_binary_codec);
  (void)registry.registerCodec(&g_cbor_lite_codec);
  (void)registry.registerCodec(&g_binary_map_codec);
}

}  // namespace espnow_link
