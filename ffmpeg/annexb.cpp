#include "annexb.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <unistd.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
}

bool probeVideoFile(const std::string &path, CodecType &codec, bool &isRawAnnexB,
                    std::string &error) {
    isRawAnnexB = false;

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        error = "Failed to open input";
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        error = "Failed to find stream info";
        return false;
    }

    int videoIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIdx = (int)i;
            break;
        }
    }
    if (videoIdx < 0) {
        avformat_close_input(&fmt);
        error = "No video stream found";
        return false;
    }

    const AVCodecParameters *par = fmt->streams[videoIdx]->codecpar;
    if (par->codec_id == AV_CODEC_ID_H264) {
        codec = CodecType::H264;
    } else if (par->codec_id == AV_CODEC_ID_HEVC) {
        codec = CodecType::H265;
    } else {
        avformat_close_input(&fmt);
        error = "Unsupported video codec (only H.264/H.265)";
        return false;
    }

    const char *name = (fmt->iformat && fmt->iformat->name) ? fmt->iformat->name : "";
    isRawAnnexB = (std::strcmp(name, "h264") == 0 || std::strcmp(name, "hevc") == 0);

    avformat_close_input(&fmt);
    return true;
}

bool buildAnnexBFile(const std::string &path, std::string &annexBPath,
                     CodecType &codec, std::string &error,
                     const std::function<bool(double)> &progress) {
    annexBPath.clear();

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        error = "Failed to open input";
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        error = "Failed to find stream info";
        return false;
    }

    int videoIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIdx = (int)i;
            break;
        }
    }
    if (videoIdx < 0) {
        avformat_close_input(&fmt);
        error = "No video stream found";
        return false;
    }

    AVStream *stream = fmt->streams[videoIdx];
    AVCodecParameters *par = stream->codecpar;
    if (par->codec_id == AV_CODEC_ID_H264) {
        codec = CodecType::H264;
    } else if (par->codec_id == AV_CODEC_ID_HEVC) {
        codec = CodecType::H265;
    } else {
        avformat_close_input(&fmt);
        error = "Unsupported video codec (only H.264/H.265)";
        return false;
    }

    const std::string formatName = (fmt->iformat && fmt->iformat->name) ? fmt->iformat->name : "";
    if (formatName == "h264" || formatName == "hevc") {
        // Raw Annex B stream: parse the original file directly.
        avformat_close_input(&fmt);
        return true;
    }

    // Container: convert AVCC/hvcC back to Annex B in a temporary file so the
    // converted stream never has to live in memory.
    const char *tmpDir = std::getenv("TMPDIR");
    std::string tmpl = std::string((tmpDir && tmpDir[0]) ? tmpDir : "/tmp") + "/dsh_annexb_XXXXXX";
    std::vector<char> nameBuf(tmpl.begin(), tmpl.end());
    nameBuf.push_back('\0');

    const int fd = mkstemp(nameBuf.data());
    if (fd < 0) {
        avformat_close_input(&fmt);
        error = "Cannot create temporary file";
        return false;
    }
    FILE *out = fdopen(fd, "wb");
    if (!out) {
        ::close(fd);
        std::remove(nameBuf.data());
        avformat_close_input(&fmt);
        error = "Cannot open temporary file";
        return false;
    }

    // AVCC/hvcC extradata starts with configurationVersion 1.
    const bool lengthPrefixed = (par->extradata && par->extradata_size > 1 &&
                                 par->extradata[0] == 1);

    AVBSFContext *bsf = nullptr;
    if (lengthPrefixed) {
        const AVBitStreamFilter *filter = av_bsf_get_by_name(
            codec == CodecType::H265 ? "hevc_mp4toannexb" : "h264_mp4toannexb");
        if (!filter || av_bsf_alloc(filter, &bsf) < 0 ||
            avcodec_parameters_copy(bsf->par_in, par) < 0 || av_bsf_init(bsf) < 0) {
            if (bsf) av_bsf_free(&bsf);
            std::fclose(out);
            std::remove(nameBuf.data());
            avformat_close_input(&fmt);
            error = "Failed to initialize bitstream filter";
            return false;
        }
    }

    AVPacket *pkt = av_packet_alloc();
    AVPacket *converted = av_packet_alloc();
    bool ok = true;

    const int64_t totalFrames = (stream->nb_frames > 0) ? stream->nb_frames : 0;
    int64_t seenPackets = 0;
    bool cancelled = false;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (progress && (++seenPackets % 200 == 0)) {
            const double f = totalFrames > 0
                                 ? std::min(1.0, (double)seenPackets / (double)totalFrames)
                                 : -1.0;
            if (!progress(f)) { cancelled = true; av_packet_unref(pkt); break; }
        }
        if (pkt->stream_index == videoIdx) {
            if (bsf) {
                if (av_bsf_send_packet(bsf, pkt) < 0) {
                    ok = false;
                    av_packet_unref(pkt);
                    break;
                }
                while (av_bsf_receive_packet(bsf, converted) >= 0) {
                    if (std::fwrite(converted->data, 1, (size_t)converted->size, out) !=
                        (size_t)converted->size) {
                        ok = false;
                    }
                    av_packet_unref(converted);
                }
            } else if (std::fwrite(pkt->data, 1, (size_t)pkt->size, out) !=
                       (size_t)pkt->size) {
                ok = false;
            }
        }
        av_packet_unref(pkt);
    }

    if (bsf) {
        av_bsf_send_packet(bsf, nullptr);
        while (av_bsf_receive_packet(bsf, converted) >= 0) {
            if (std::fwrite(converted->data, 1, (size_t)converted->size, out) !=
                (size_t)converted->size) {
                ok = false;
            }
            av_packet_unref(converted);
        }
    }

    av_packet_free(&pkt);
    av_packet_free(&converted);
    if (bsf) av_bsf_free(&bsf);
    avformat_close_input(&fmt);

    const bool closed = (std::fclose(out) == 0);
    if (cancelled) {
        std::remove(nameBuf.data());
        error = "Cancelled";
        return false;
    }
    if (!ok || !closed) {
        std::remove(nameBuf.data());
        error = "Failed to extract video data";
        return false;
    }

    annexBPath.assign(nameBuf.data());
    return true;
}
