#pragma once

#include "nalunit.h"
#include <functional>
#include <string>

// Demux a video file with FFmpeg and write one Annex B byte stream for the NAL
// analyzer. Raw H.264/H.265 inputs leave `annexBPath` empty (parse the original
// file directly); containers (MP4/MOV/MKV/TS...) produce a temporary file that
// the caller should remove right after BitstreamParser has mapped it.
// Opens the file with FFmpeg to determine the codec and whether the input is a
// raw Annex B elementary stream (as opposed to a container).
bool probeVideoFile(const std::string &path, CodecType &codec, bool &isRawAnnexB,
                    std::string &error);

bool buildAnnexBFile(const std::string &path, std::string &annexBPath,
                     CodecType &codec, std::string &error,
                     const std::function<bool(double)> &progress = {});
