#include "khdays/assets/mods_video.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "khdays/assets/mobiclip.h"
#include "mobiclip_reference.hpp"

namespace khdays::assets {

namespace {

std::uint32_t read32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
           | (static_cast<std::uint32_t>(p[1]) << 8U)
           | (static_cast<std::uint32_t>(p[2]) << 16U)
           | (static_cast<std::uint32_t>(p[3]) << 24U);
}

struct Packet final {
    std::size_t payload_offset = 0;
    std::size_t payload_size = 0;
    std::size_t audio_blocks_per_channel = 0;
};

struct FrameStorage final {
    std::vector<std::uint8_t> luma;
    std::vector<std::uint8_t> chroma_first;
    std::vector<std::uint8_t> chroma_second;
};

}  // namespace

class ModsVideoDecoder::Impl final {
public:
    Impl(std::vector<std::uint8_t> input,
         MobiClipCoefficientTables input_tables)
        : data(std::move(input)), tables(std::move(input_tables)) {
        info = parse_mods_header(data.data(), data.size());
        if (info.width != 256 || info.height <= 0 || (info.height & 15) != 0) {
            throw std::runtime_error(
                "MODS video: only the game's 256-wide, macroblock-aligned clips are supported");
        }
        std::size_t limit = data.size();
        if (info.audio_info_offset != 0U) {
            limit = std::min(limit,
                             static_cast<std::size_t>(info.audio_info_offset));
        }
        if (info.key_table_offset != 0U) {
            limit = std::min(limit,
                             static_cast<std::size_t>(info.key_table_offset));
        }

        std::size_t offset = info.packet_data_offset;
        packets.reserve(info.frame_count);
        for (std::uint32_t index = 0; index < info.frame_count; ++index) {
            if (offset + 4U > limit) {
                throw std::runtime_error("MODS video: packet header exceeds packet area");
            }
            const auto packed = read32(data.data() + offset);
            const auto payload_size = static_cast<std::size_t>(packed >> 14U);
            const auto payload_offset = offset + 4U;
            if (payload_size > limit - payload_offset) {
                throw std::runtime_error("MODS video: packet payload exceeds packet area");
            }
            packets.push_back({payload_offset, payload_size,
                               static_cast<std::size_t>(packed & 0x3fffU)});
            offset = payload_offset + payload_size;
        }
        motion.resize(static_cast<std::size_t>(info.width / 16 + 3));
        ima_states.resize(static_cast<std::size_t>(info.audio_channels));
    }

    FrameStorage make_frame() const {
        FrameStorage output;
        output.luma.resize(static_cast<std::size_t>(info.width) * info.height);
        const auto chroma_size = static_cast<std::size_t>(info.width / 2)
                                 * static_cast<std::size_t>(info.height / 2);
        output.chroma_first.resize(chroma_size);
        output.chroma_second.resize(chroma_size);
        return output;
    }

    bool decode_next(const bool ds_exact) {
        if (next_packet >= packets.size()) {
            return false;
        }

        const auto& packet = packets[next_packet];
        auto output = make_frame();
        ::khdays::mobiclip::CoefficientTable decoder_tables[2]{};
        for (std::size_t i = 0; i < 2U; ++i) {
            decoder_tables[i].lookup = tables[i].lookup.data();
            decoder_tables[i].residue = tables[i].residue.data();
        }

        std::vector<::khdays::mobiclip::DecoderReferenceFrame> references;
        references.reserve(histories.size());
        for (const auto& history : histories) {
            references.push_back({history.luma.data(), history.chroma_first.data(),
                                  history.chroma_second.data(),
                                  static_cast<unsigned int>(info.width),
                                  static_cast<unsigned int>(info.width / 2)});
        }

        const ::khdays::mobiclip::DecoderFrameBuffer destination{
            output.luma.data(), output.chroma_first.data(),
            output.chroma_second.data(), static_cast<unsigned int>(info.width),
            static_cast<unsigned int>(info.width / 2)};
        ::khdays::mobiclip::DecoderFrameResult result{};
        const bool decoded = ::khdays::mobiclip::decodeFrame(
            data.data() + packet.payload_offset,
            static_cast<unsigned int>(packet.payload_size),
            static_cast<unsigned int>(info.width),
            static_cast<unsigned int>(info.height), decoder_tables,
            references.empty() ? nullptr : references.data(),
            static_cast<unsigned int>(references.size()), previous_quantizer,
            previous_format, destination, motion.data(),
            static_cast<unsigned int>(motion.size()), result);
        if (!decoded) {
            throw std::runtime_error(
                "MODS video: MobiClip rejected frame "
                + std::to_string(next_packet));
        }

        decode_audio(packet, result);

        // Preserve the original decoder's native chroma order.  Its first and
        // second compact planes are the first and second halves of each DS
        // chroma row respectively; reinterpreting FFmpeg's plane labels here
        // swaps Co/Cg and gives the whole movie a strong green cast.
        std::vector<std::uint8_t> packed_chroma(
            static_cast<std::size_t>(info.height / 2) * 256U);
        for (int y = 0; y < info.height / 2; ++y) {
            const auto source = static_cast<std::size_t>(y) * (info.width / 2);
            const auto target = static_cast<std::size_t>(y) * 256U;
            std::copy_n(output.chroma_first.data() + source, info.width / 2,
                        packed_chroma.data() + target);
            std::copy_n(output.chroma_second.data() + source, info.width / 2,
                        packed_chroma.data() + target + 128U);
        }
        current = khdays::assets::mobiclip::frame_to_rgba(
            output.luma.data(), packed_chroma.data(), info.width, info.height,
            ds_exact);

        previous_quantizer = result.header.quantizer;
        previous_format = result.header.formatVariant;
        histories.insert(histories.begin(), std::move(output));
        if (histories.size() > 6U) {
            histories.pop_back();
        }
        ++next_packet;
        return true;
    }

    void reset() {
        next_packet = 0;
        previous_quantizer = 12;
        previous_format = false;
        histories.clear();
        current = {};
        current_audio = {};
        std::fill(ima_states.begin(), ima_states.end(),
                  khdays::assets::mobiclip::ImaAdpcmState{});
    }

    void decode_audio(
        const Packet& packet,
        const ::khdays::mobiclip::DecoderFrameResult& result) {
        current_audio = {};
        current_audio.sample_rate = static_cast<std::uint32_t>(info.audio_rate);
        current_audio.channels = static_cast<std::uint16_t>(info.audio_channels);
        if (!info.has_audio() || packet.audio_blocks_per_channel == 0U) {
            return;
        }
        if (info.audio_coding != 3) {
            throw std::runtime_error(
                "MODS audio: only the game's IMA ADPCM coding 3 is supported");
        }

        const auto video_bytes =
            (static_cast<std::size_t>(result.bitsConsumed) + 15U) / 16U * 2U;
        const auto extension_bytes = result.header.intra ? 4U : 0U;
        std::size_t cursor = packet.payload_offset + video_bytes + extension_bytes;
        const auto packet_end = packet.payload_offset + packet.payload_size;
        const auto channels = static_cast<std::size_t>(info.audio_channels);
        const auto sample_frames = packet.audio_blocks_per_channel * 256U;
        current_audio.samples.assign(sample_frames * channels, 0);

        for (std::size_t block = 0; block < packet.audio_blocks_per_channel;
             ++block) {
            for (std::size_t channel = 0; channel < channels; ++channel) {
                if (result.header.intra && block == 0U) {
                    if (cursor + 4U > packet_end) {
                        throw std::runtime_error("MODS audio: truncated IMA state");
                    }
                    ima_states[channel].step_index = data[cursor];
                    ima_states[channel].predictor = static_cast<std::int16_t>(
                        static_cast<std::uint16_t>(data[cursor + 2U])
                        | (static_cast<std::uint16_t>(data[cursor + 3U]) << 8U));
                    if (ima_states[channel].step_index > 88) {
                        throw std::runtime_error("MODS audio: invalid IMA step index");
                    }
                    cursor += 4U;
                }
                if (cursor + 0x80U > packet_end) {
                    throw std::runtime_error("MODS audio: truncated IMA block");
                }
                std::array<std::int16_t, 256> decoded{};
                khdays::assets::mobiclip::decode_ima_adpcm(
                    data.data() + cursor, 0x80U, ima_states[channel],
                    decoded.data());
                cursor += 0x80U;
                for (std::size_t sample = 0; sample < decoded.size(); ++sample) {
                    const auto base = block * 256U + sample;
                    current_audio.samples[(base * channels) + channel] =
                        decoded[sample];
                }
            }
        }
        if (packet_end - cursor > 3U) {
            throw std::runtime_error("MODS audio: unexpected bytes after ADPCM blocks");
        }
    }

    std::vector<std::uint8_t> data;
    MobiClipCoefficientTables tables;
    ModsInfo info;
    std::vector<Packet> packets;
    std::vector<FrameStorage> histories;
    std::vector<::khdays::mobiclip::MotionVector> motion;
    DecodedTexture current;
    DecodedAudio current_audio;
    std::vector<khdays::assets::mobiclip::ImaAdpcmState> ima_states;
    std::size_t next_packet = 0;
    unsigned int previous_quantizer = 12;
    bool previous_format = false;
};

ModsVideoDecoder::ModsVideoDecoder(
    std::vector<std::uint8_t> container,
    MobiClipCoefficientTables coefficient_tables)
    : impl_(std::make_unique<Impl>(std::move(container),
                                  std::move(coefficient_tables))) {}

ModsVideoDecoder::~ModsVideoDecoder() = default;
ModsVideoDecoder::ModsVideoDecoder(ModsVideoDecoder&&) noexcept = default;
ModsVideoDecoder& ModsVideoDecoder::operator=(ModsVideoDecoder&&) noexcept = default;

const ModsInfo& ModsVideoDecoder::info() const { return impl_->info; }
double ModsVideoDecoder::frames_per_second() const {
    return impl_->info.frames_per_second();
}
std::size_t ModsVideoDecoder::frame_index() const { return impl_->next_packet; }
bool ModsVideoDecoder::finished() const {
    return impl_->next_packet >= impl_->packets.size();
}
bool ModsVideoDecoder::decode_next(const bool ds_exact) {
    return impl_->decode_next(ds_exact);
}
const DecodedTexture& ModsVideoDecoder::frame() const {
    if (impl_->current.rgba.empty()) {
        throw std::logic_error("MODS video: no frame has been decoded");
    }
    return impl_->current;
}
const DecodedAudio& ModsVideoDecoder::audio_chunk() const {
    return impl_->current_audio;
}
void ModsVideoDecoder::reset() { impl_->reset(); }

}  // namespace khdays::assets
