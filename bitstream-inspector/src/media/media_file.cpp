#include "media/media_file.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace bs {

namespace {

std::string avError(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

bool startsWithStartCode(const uint8_t *data, int size)
{
    if (!data || size < 3)
        return false;
    if (data[0] == 0 && data[1] == 0 && data[2] == 1)
        return true;
    return size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 &&
           data[3] == 1;
}

bool formatIsRaw(const std::string &format)
{
    return format == "h264" || format == "hevc";
}

bool formatIsAnnexB(const std::string &format)
{
    return formatIsRaw(format) || format.find("mpegts") != std::string::npos;
}

} // namespace

struct MediaFile::Impl {
    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    int stream = -1;
    AVRational time_base = {1, 1000};
    ContainerInfo info;
    std::vector<MediaPacket> packets;
    bool extracted = false;
    SwsContext *sws = nullptr;
    AVCodecID codec_id = AV_CODEC_ID_NONE;
    std::vector<uint8_t> extradata_annexb;
    bool pending_ps = false;
    bool annexb_packets = false;

    ~Impl() { reset(); }

    void reset()
    {
        if (sws) {
            sws_freeContext(sws);
            sws = nullptr;
        }
        if (dec)
            avcodec_free_context(&dec);
        if (fmt)
            avformat_close_input(&fmt);
        stream = -1;
        packets.clear();
        extracted = false;
        extradata_annexb.clear();
        codec_id = AV_CODEC_ID_NONE;
        info = ContainerInfo();
    }

    void buildExtradata(const std::vector<uint8_t> &es)
    {
        extradata_annexb.clear();
        bool have_sps = false;
        bool have_pps = false;
        bool have_vps = false;
        auto append = [&](const uint8_t *nal, size_t size) {
            static const uint8_t sc[4] = {0, 0, 0, 1};
            extradata_annexb.insert(extradata_annexb.end(), sc, sc + 4);
            extradata_annexb.insert(extradata_annexb.end(), nal, nal + size);
        };
        size_t i = 0;
        while (i + 3 < es.size()) {
            size_t sc = 0;
            if (es[i] == 0 && es[i + 1] == 0 && es[i + 2] == 1)
                sc = 3;
            else if (i + 4 < es.size() && es[i] == 0 && es[i + 1] == 0 &&
                     es[i + 2] == 0 && es[i + 3] == 1)
                sc = 4;
            if (!sc) {
                i++;
                continue;
            }
            size_t start = i + sc;
            size_t next = start;
            while (next + 3 < es.size()) {
                if (es[next] == 0 && es[next + 1] == 0 &&
                    (es[next + 2] == 1 ||
                     (next + 3 < es.size() && es[next + 2] == 0 &&
                      es[next + 3] == 1)))
                    break;
                next++;
            }
            if (next <= start) {
                i = start;
                continue;
            }
            const uint8_t *nal = es.data() + start;
            size_t size = next - start;
            if (codec_id == AV_CODEC_ID_H264 && size >= 1) {
                int type = nal[0] & 0x1F;
                if (type == 7 && !have_sps) {
                    append(nal, size);
                    have_sps = true;
                } else if (type == 8 && !have_pps) {
                    append(nal, size);
                    have_pps = true;
                }
            } else if (codec_id == AV_CODEC_ID_HEVC && size >= 2) {
                int type = (nal[0] >> 1) & 0x3F;
                if (type == 32 && !have_vps) {
                    append(nal, size);
                    have_vps = true;
                } else if (type == 33 && !have_sps) {
                    append(nal, size);
                    have_sps = true;
                } else if (type == 34 && !have_pps) {
                    append(nal, size);
                    have_pps = true;
                }
            }
            if (have_sps && have_pps &&
                (codec_id == AV_CODEC_ID_H264 || have_vps))
                break;
            i = next;
        }
        if (!have_sps || !have_pps)
            extradata_annexb.clear();
        if (std::getenv("BS_MEDIA_DEBUG"))
            std::fprintf(stderr,
                         "extradata: %zu bytes sps=%d pps=%d vps=%d codec=%d\n",
                         extradata_annexb.size(), have_sps, have_pps, have_vps,
                         static_cast<int>(codec_id));
        if (std::getenv("BS_MEDIA_DEBUG")) {
            std::fprintf(stderr, "ours:");
            for (size_t k = 0; k < extradata_annexb.size() && k < 64; k++)
                std::fprintf(stderr, " %02X", extradata_annexb[k]);
            std::fprintf(stderr, "\n");
        }
    }

    bool ensureDecoder(std::string &error)
    {
        if (dec)
            return true;
        AVStream *st = fmt->streams[stream];
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) {
            error = "no decoder for this codec";
            return false;
        }
        dec = avcodec_alloc_context3(codec);
        if (!dec) {
            error = "cannot allocate decoder";
            return false;
        }
        int ret = avcodec_parameters_to_context(dec, st->codecpar);
        if (ret < 0) {
            error = "avcodec_parameters_to_context: " + avError(ret);
            avcodec_free_context(&dec);
            return false;
        }
        dec->pkt_timebase = st->time_base;
        // Frame-level threading creates per-thread parameter-set copies; after a
        // seek the injected SPS/PPS only reach the main context. Single-threaded
        // decoding is fast enough for on-demand previews and avoids that.
        dec->thread_count = 1;
        dec->flags2 |= AV_CODEC_FLAG2_EXPORT_MVS;
        if (annexb_packets && !extradata_annexb.empty()) {
            av_freep(&dec->extradata);
            dec->extradata_size = 0;
            dec->extradata = static_cast<uint8_t *>(av_mallocz(
                extradata_annexb.size() + AV_INPUT_BUFFER_PADDING_SIZE));
            if (!dec->extradata) {
                error = "cannot allocate decoder extradata";
                avcodec_free_context(&dec);
                return false;
            }
            std::memcpy(dec->extradata, extradata_annexb.data(),
                        extradata_annexb.size());
            dec->extradata_size = static_cast<int>(extradata_annexb.size());
        }
        if (std::getenv("BS_MEDIA_DEBUG")) {
            std::fprintf(stderr, "decoder extradata: %d bytes (ours %zu)\n",
                         dec->extradata_size, extradata_annexb.size());
            std::fprintf(stderr, "dec hex:");
            for (int k = 0; k < dec->extradata_size && k < 64; k++)
                std::fprintf(stderr, " %02X", dec->extradata[k]);
            std::fprintf(stderr, "\n");
        }
        ret = avcodec_open2(dec, codec, nullptr);
        if (std::getenv("BS_MEDIA_DEBUG"))
            std::fprintf(stderr, "after open: extradata=%d first=%02X threads=%d\n",
                         dec->extradata_size,
                         dec->extradata_size > 0 ? dec->extradata[0] : 0,
                         dec->thread_count);
        if (ret < 0) {
            error = "avcodec_open2: " + avError(ret);
            avcodec_free_context(&dec);
            return false;
        }
        return true;
    }

    bool seekToStart()
    {
        int ret;
        if (info.raw_es) {
            // The raw H.264/HEVC demuxers do not support timestamp seeking and
            // avformat_find_stream_info() leaves them at EOF.
            ret = av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BYTE);
            if (ret >= 0) {
                avformat_flush(fmt);
            } else {
                avio_seek(fmt->pb, 0, SEEK_SET);
                avformat_flush(fmt);
                ret = 0;
            }
        } else {
            ret = av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
            if (ret < 0)
                ret = avformat_seek_file(fmt, -1, INT64_MIN, 0, INT64_MAX, 0);
        }
        if (dec)
            avcodec_flush_buffers(dec);
        return ret >= 0;
    }

    bool seekToTimestamp(int64_t ts)
    {
        int ret = av_seek_frame(fmt, stream, ts, AVSEEK_FLAG_BACKWARD);
        if (ret < 0)
            ret = avformat_seek_file(fmt, stream, INT64_MIN, ts, INT64_MAX,
                                     AVSEEK_FLAG_BACKWARD);
        if (dec)
            avcodec_flush_buffers(dec);
        return ret >= 0;
    }

    int sendPacket(AVPacket *pkt, std::string &error)
    {
        if (!pending_ps || extradata_annexb.empty())
            return avcodec_send_packet(dec, pkt);
        pending_ps = false;
        AVPacket *combined = av_packet_alloc();
        if (!combined) {
            error = "cannot allocate combined packet";
            return AVERROR(ENOMEM);
        }
        int size = static_cast<int>(extradata_annexb.size()) + pkt->size;
        int ret = av_new_packet(combined, size);
        if (ret < 0) {
            error = "cannot allocate combined payload";
            av_packet_free(&combined);
            return ret;
        }
        std::memcpy(combined->data, extradata_annexb.data(),
                    extradata_annexb.size());
        std::memcpy(combined->data + extradata_annexb.size(), pkt->data,
                    pkt->size);
        combined->pts = pkt->pts;
        combined->dts = pkt->dts;
        combined->flags = pkt->flags;
        ret = avcodec_send_packet(dec, combined);
        av_packet_free(&combined);
        return ret;
    }

    bool convert(AVFrame *frame, std::vector<uint8_t> &bgra, int &width,
                 int &height, std::string &error)
    {
        width = frame->width;
        height = frame->height;
        if (width <= 0 || height <= 0) {
            error = "decoded frame has invalid dimensions";
            return false;
        }
        sws = sws_getCachedContext(
            sws, width, height, static_cast<AVPixelFormat>(frame->format),
            width, height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr,
            nullptr);
        if (!sws) {
            error = "sws_getCachedContext failed";
            return false;
        }
        bgra.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        uint8_t *dst_data[4] = {bgra.data(), nullptr, nullptr, nullptr};
        int dst_linesize[4] = {width * 4, 0, 0, 0};
        sws_scale(sws, frame->data, frame->linesize, 0, height, dst_data,
                  dst_linesize);
        return true;
    }

    int nearestKeyframePacket(int packet_index) const
    {
        int i = std::min(std::max(packet_index, 0),
                         static_cast<int>(packets.size()) - 1);
        while (i > 0 && !packets[static_cast<size_t>(i)].keyframe)
            i--;
        return i;
    }
};

MediaFile::MediaFile() : impl_(new Impl) {}

MediaFile::~MediaFile() { delete impl_; }

const ContainerInfo &MediaFile::info() const { return impl_->info; }

const std::vector<MediaPacket> &MediaFile::packets() const
{
    return impl_->packets;
}

bool MediaFile::open(const std::string &path, std::string &error)
{
    if (std::getenv("BS_MEDIA_DEBUG"))
        av_log_set_level(AV_LOG_DEBUG);
    impl_->reset();
    int ret = avformat_open_input(&impl_->fmt, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        error = "avformat_open_input: " + avError(ret);
        impl_->reset();
        return false;
    }
    ret = avformat_find_stream_info(impl_->fmt, nullptr);
    if (ret < 0) {
        error = "avformat_find_stream_info: " + avError(ret);
        impl_->reset();
        return false;
    }
    impl_->stream = av_find_best_stream(impl_->fmt, AVMEDIA_TYPE_VIDEO, -1,
                                        -1, nullptr, 0);
    if (impl_->stream < 0) {
        error = "no video stream found";
        impl_->reset();
        return false;
    }
    AVStream *st = impl_->fmt->streams[impl_->stream];
    if (st->codecpar->codec_id != AV_CODEC_ID_H264 &&
        st->codecpar->codec_id != AV_CODEC_ID_HEVC) {
        error = std::string("unsupported video codec: ") +
                avcodec_get_name(st->codecpar->codec_id);
        impl_->reset();
        return false;
    }
    impl_->time_base = st->time_base;
    impl_->codec_id = st->codecpar->codec_id;
    impl_->info.format =
        impl_->fmt->iformat && impl_->fmt->iformat->name
            ? impl_->fmt->iformat->name
            : "";
    impl_->info.codec = avcodec_get_name(st->codecpar->codec_id);
    impl_->info.width = st->codecpar->width;
    impl_->info.height = st->codecpar->height;
    AVRational fps = av_guess_frame_rate(impl_->fmt, st, nullptr);
    impl_->info.fps = av_q2d(fps);
    if (impl_->fmt->duration != AV_NOPTS_VALUE)
        impl_->info.duration_ms =
            impl_->fmt->duration * 1000 / AV_TIME_BASE;
    impl_->info.bit_rate = impl_->fmt->bit_rate;
    impl_->info.raw_es = formatIsRaw(impl_->info.format);
    impl_->annexb_packets = formatIsAnnexB(impl_->info.format);
    return true;
}

bool MediaFile::extractAnnexB(std::vector<uint8_t> &es, std::string &error)
{
    es.clear();
    impl_->packets.clear();
    impl_->extracted = false;
    if (!impl_->fmt) {
        error = "file is not open";
        return false;
    }
    impl_->seekToStart();

    const char *bsf_name = nullptr;
    if (!formatIsAnnexB(impl_->info.format)) {
        bsf_name = impl_->fmt->streams[impl_->stream]->codecpar->codec_id ==
                           AV_CODEC_ID_H264
                       ? "h264_mp4toannexb"
                       : "hevc_mp4toannexb";
    }

    AVBSFContext *bsf = nullptr;
    if (bsf_name) {
        const AVBitStreamFilter *filter = av_bsf_get_by_name(bsf_name);
        if (!filter) {
            error = std::string("bitstream filter not available: ") + bsf_name;
            return false;
        }
        int ret = av_bsf_alloc(filter, &bsf);
        if (ret < 0) {
            error = "av_bsf_alloc: " + avError(ret);
            return false;
        }
        avcodec_parameters_copy(bsf->par_in,
                                impl_->fmt->streams[impl_->stream]->codecpar);
        bsf->time_base_in = impl_->time_base;
        ret = av_bsf_init(bsf);
        if (ret < 0) {
            error = "av_bsf_init: " + avError(ret);
            av_bsf_free(&bsf);
            return false;
        }
    }

    auto append = [&](const AVPacket *pkt) {
        size_t offset = es.size();
        if (!startsWithStartCode(pkt->data, pkt->size)) {
            static const uint8_t start_code[4] = {0, 0, 0, 1};
            es.insert(es.end(), start_code, start_code + 4);
        }
        es.insert(es.end(), pkt->data, pkt->data + pkt->size);
        MediaPacket mp;
        mp.es_offset = offset;
        mp.es_size = es.size() - offset;
        mp.pts = pkt->pts;
        mp.dts = pkt->dts;
        mp.has_pts = pkt->pts != AV_NOPTS_VALUE;
        mp.has_dts = pkt->dts != AV_NOPTS_VALUE;
        mp.keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        mp.pos = pkt->pos;
        impl_->packets.push_back(mp);
    };

    AVPacket *pkt = av_packet_alloc();
    AVPacket *out = av_packet_alloc();
    while (av_read_frame(impl_->fmt, pkt) >= 0) {
        if (pkt->stream_index != impl_->stream) {
            av_packet_unref(pkt);
            continue;
        }
        if (bsf) {
            int ret = av_bsf_send_packet(bsf, pkt);
            if (ret < 0) {
                // send_packet leaves ownership with the caller on failure.
                av_packet_unref(pkt);
                continue;
            }
            while (av_bsf_receive_packet(bsf, out) == 0) {
                append(out);
                av_packet_unref(out);
            }
        } else {
            append(pkt);
            av_packet_unref(pkt);
        }
    }
    if (bsf) {
        av_bsf_send_packet(bsf, nullptr);
        while (av_bsf_receive_packet(bsf, out) == 0) {
            append(out);
            av_packet_unref(out);
        }
    }
    av_packet_free(&pkt);
    av_packet_free(&out);
    if (bsf)
        av_bsf_free(&bsf);

    if (es.empty()) {
        error = "no video packets could be extracted";
        return false;
    }
    impl_->buildExtradata(es);
    impl_->extracted = true;
    return true;
}

void MediaFile::applyTiming(Stream &stream) const
{
    const auto &frames = stream.frames();
    std::vector<FrameTiming> timing(frames.size());
    if (impl_->packets.empty())
        return;

    std::vector<size_t> order(impl_->packets.size());
    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return impl_->packets[a].es_offset < impl_->packets[b].es_offset;
    });

    bool has_pts = false;
    int64_t reference = std::numeric_limits<int64_t>::max();
    for (const auto &p : impl_->packets) {
        if (p.has_pts) {
            has_pts = true;
            reference = std::min(reference, p.pts);
        }
    }
    if (!has_pts) {
        for (const auto &p : impl_->packets) {
            if (p.has_dts)
                reference = std::min(reference, p.dts);
        }
    }
    bool have_reference = reference != std::numeric_limits<int64_t>::max();
    double tb = av_q2d(impl_->time_base);

    for (size_t i = 0; i < frames.size(); i++) {
        const FrameInfo &f = frames[i];
        size_t packet = static_cast<size_t>(-1);
        auto it = std::upper_bound(
            order.begin(), order.end(), f.byte_offset,
            [&](size_t value, size_t index) {
                return value < impl_->packets[index].es_offset;
            });
        if (it != order.begin()) {
            size_t candidate = *(it - 1);
            const MediaPacket &p = impl_->packets[candidate];
            if (f.byte_offset >= p.es_offset &&
                f.byte_offset < p.es_offset + p.es_size)
                packet = candidate;
        }
        if (packet == static_cast<size_t>(-1) &&
            impl_->packets.size() == frames.size())
            packet = i;
        if (packet == static_cast<size_t>(-1))
            continue;

        const MediaPacket &p = impl_->packets[packet];
        int64_t ts = 0;
        bool valid = false;
        if (p.has_pts) {
            ts = p.pts;
            valid = true;
        } else if (p.has_dts) {
            ts = p.dts;
            valid = true;
        }
        FrameTiming &t = timing[i];
        t.packet_index = static_cast<int>(packet);
        t.pts = p.pts;
        t.dts = p.dts;
        if (valid && have_reference && tb > 0.0) {
            t.has_timing = true;
            t.time_sec = static_cast<double>(ts - reference) * tb;
        }
    }
    stream.applyFrameTiming(timing);
}

bool MediaFile::decodePacket(int packet_index, std::vector<uint8_t> &bgra,
                             int &width, int &height, std::string &error)
{
    if (!impl_->fmt || !impl_->extracted) {
        error = "demux the stream before decoding";
        return false;
    }
    if (packet_index < 0 ||
        packet_index >= static_cast<int>(impl_->packets.size())) {
        error = "packet index out of range";
        return false;
    }
    if (!impl_->ensureDecoder(error))
        return false;

    const MediaPacket &target = impl_->packets[static_cast<size_t>(packet_index)];
    if (!target.has_pts && !target.has_dts) {
        // No timestamps (raw ES): show the nearest keyframe instead.
        int kf = impl_->nearestKeyframePacket(packet_index);
        if (!impl_->seekToStart()) {
            error = "cannot rewind the stream for decoding";
            return false;
        }
        impl_->pending_ps =
            impl_->annexb_packets && !impl_->extradata_annexb.empty();
        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        int video_packets = 0;
        bool converted = false;
        while (av_read_frame(impl_->fmt, pkt) >= 0) {
            if (pkt->stream_index != impl_->stream) {
                av_packet_unref(pkt);
                continue;
            }
            video_packets++;
            if (video_packets < kf + 1) {
                av_packet_unref(pkt);
                continue;
            }
            int ret = impl_->sendPacket(pkt, error);
            av_packet_unref(pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN))
                continue;
            while (avcodec_receive_frame(impl_->dec, frame) == 0) {
                if (!converted && video_packets >= kf + 1) {
                    if (!impl_->convert(frame, bgra, width, height, error)) {
                        av_frame_free(&frame);
                        av_packet_free(&pkt);
                        return false;
                    }
                    converted = true;
                }
                av_frame_unref(frame);
            }
            if (converted || video_packets > kf + 64)
                break;
        }
        if (!converted) {
            avcodec_send_packet(impl_->dec, nullptr);
            while (avcodec_receive_frame(impl_->dec, frame) == 0) {
                if (!converted) {
                    if (!impl_->convert(frame, bgra, width, height, error)) {
                        av_frame_free(&frame);
                        av_packet_free(&pkt);
                        return false;
                    }
                    converted = true;
                }
                av_frame_unref(frame);
            }
        }
        av_frame_free(&frame);
        av_packet_free(&pkt);
        if (!converted) {
            error = "no frame could be decoded";
            return false;
        }
        return true;
    }

    int keyframe = impl_->nearestKeyframePacket(packet_index);
    const MediaPacket &kp = impl_->packets[static_cast<size_t>(keyframe)];
    int64_t seek_ts = target.has_dts ? target.dts : target.pts;
    if (kp.has_dts)
        seek_ts = kp.dts;
    else if (kp.has_pts)
        seek_ts = kp.pts;
    if (std::getenv("BS_MEDIA_DEBUG"))
        std::fprintf(stderr,
                     "decode target pkt=%d pts=%lld dts=%lld kf=%d seek=%lld\n",
                     packet_index, static_cast<long long>(target.pts),
                     static_cast<long long>(target.dts), keyframe,
                     static_cast<long long>(seek_ts));

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool matched = false;
    int seen_frames = 0;
    auto tryFrame = [&](AVFrame *f) -> bool {
        int64_t fts = f->best_effort_timestamp;
        if (fts == AV_NOPTS_VALUE)
            fts = f->pts;
        int64_t fdts = f->pkt_dts;
        if (std::getenv("BS_MEDIA_DEBUG") && seen_frames < 24) {
            std::fprintf(stderr,
                         "seen frame pts=%lld dts=%lld best=%lld (target %lld/%lld)\n",
                         static_cast<long long>(f->pts),
                         static_cast<long long>(fdts),
                         static_cast<long long>(f->best_effort_timestamp),
                         static_cast<long long>(target.pts),
                         static_cast<long long>(target.dts));
            seen_frames++;
        }
        bool match = false;
#if LIBAVUTIL_VERSION_MAJOR < 60
        // pkt_pos was removed in FFmpeg 8; PTS/DTS matching below still works.
        if (f->pkt_pos >= 0 && target.pos >= 0 &&
            f->pkt_pos == target.pos)
            match = true;
#endif
        if (!match && target.has_pts)
            match = fts == target.pts;
        else if (!match && target.has_dts)
            match = fdts == target.dts;
        if (std::getenv("BS_MEDIA_DEBUG") && match)
            std::fprintf(stderr,
                         "MATCH fts=%lld fdts=%lld target_pos=%lld\n",
                         static_cast<long long>(fts),
                         static_cast<long long>(fdts),
                         static_cast<long long>(target.pos));
        if (!match)
            return false;
        if (!impl_->convert(f, bgra, width, height, error))
            return false;
        return true;
    };

    // Pass 0 seeks to the keyframe preceding the target. Some demuxers (notably
    // MPEG-TS) may land past it; pass 1 then decodes from the beginning.
    for (int pass = 0; pass < 2; pass++) {
        if (matched)
            break;
        if (pass == 0) {
            if (!impl_->seekToTimestamp(seek_ts))
                impl_->seekToStart();
        } else if (!impl_->seekToStart()) {
            break;
        }
        impl_->pending_ps =
            impl_->annexb_packets && !impl_->extradata_annexb.empty();
        int video_packets = 0;
        int max_packets = static_cast<int>(impl_->packets.size()) + 128;
        if (max_packets < 256)
            max_packets = 256;
        while (av_read_frame(impl_->fmt, pkt) >= 0) {
            if (pkt->stream_index != impl_->stream) {
                av_packet_unref(pkt);
                continue;
            }
            video_packets++;
            int ret = impl_->sendPacket(pkt, error);
            av_packet_unref(pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN))
                continue;
            while (avcodec_receive_frame(impl_->dec, frame) == 0) {
                if (tryFrame(frame)) {
                    matched = true;
                    av_frame_unref(frame);
                    break;
                }
                av_frame_unref(frame);
            }
            if (matched)
                break;
            if (video_packets > max_packets)
                break;
        }
        if (!matched) {
            avcodec_send_packet(impl_->dec, nullptr);
            while (avcodec_receive_frame(impl_->dec, frame) == 0) {
                if (tryFrame(frame)) {
                    matched = true;
                    av_frame_unref(frame);
                    break;
                }
                av_frame_unref(frame);
            }
        }
    }
    if (!matched) {
        if (error.empty())
            error = "decoded frame did not match the requested packet";
        return false;
    }
    return true;
}

bool MediaFile::decodeFrameAt(const Stream &stream, int frame_index,
                              std::vector<uint8_t> &bgra, int &width,
                              int &height, bool &keyframe_only,
                              std::string &error)
{
    keyframe_only = false;
    if (frame_index < 0 ||
        frame_index >= static_cast<int>(stream.frames().size())) {
        error = "frame index out of range";
        return false;
    }
    const FrameInfo &f = stream.frames()[static_cast<size_t>(frame_index)];
    if (f.packet_index < 0 ||
        f.packet_index >= static_cast<int>(impl_->packets.size())) {
        error = "frame has no packet mapping";
        return false;
    }
    const MediaPacket &p = impl_->packets[static_cast<size_t>(f.packet_index)];
    keyframe_only = !p.has_pts && !p.has_dts;
    return decodePacket(f.packet_index, bgra, width, height, error);
}

} // namespace bs
