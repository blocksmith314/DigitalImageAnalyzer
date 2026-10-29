#include "bitstreamparser.h"

#include <algorithm>
#include <cstring>

namespace
{

    struct Box
    {
        uint32_t type = 0;
        uint64_t offset = 0; // byte offset of the box header
        uint64_t size = 0; // total box size including the header
    };

    uint32_t readU32(const uint8_t* p)
    {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }

    uint64_t readU64(const uint8_t* p) { return ((uint64_t)readU32(p) << 32) | readU32(p + 4); }

    // Iterates boxes inside [pos, end).
    bool nextBox(const uint8_t* data, uint64_t end, uint64_t& pos, Box& box)
    {
        if (pos + 8 > end)
            return false;
        const uint8_t* p = data + pos;
        uint64_t size = readU32(p);
        const uint32_t type = readU32(p + 4);
        uint64_t header = 8;
        if (size == 1)
        {
            if (pos + 16 > end)
                return false;
            size = readU64(p + 8);
            header = 16;
        }
        else if (size == 0)
        {
            size = end - pos;
        }
        if (size < header || pos + size > end)
            return false;
        box.type = type;
        box.offset = pos;
        box.size = size;
        pos += size;
        return true;
    }

    bool findBox(const uint8_t* data, uint64_t start, uint64_t end, uint32_t wanted, Box& out)
    {
        uint64_t pos = start;
        Box box;
        while (nextBox(data, end, pos, box))
        {
            if (box.type == wanted)
            {
                out = box;
                return true;
            }
        }
        return false;
    }

    bool appendExtraNalUnit(std::vector<NALUnit>& nals, std::vector<uint8_t>& extra, uint64_t extraBase,
                            const uint8_t* data, int len, CodecType codec)
    {
        if (len < 1)
            return false;
        NALUnit nal;
        nal.codec = codec;
        nal.offset = extraBase + extra.size();
        nal.size = (uint32_t)len;
        nal.startCodeLen = 0;
        nal.headerByte = data[0];
        nal.nalType = (codec == CodecType::H264) ? (data[0] & 0x1F) : ((data[0] >> 1) & 0x3F);
        if (codec == CodecType::H265 && len > 1)
        {
            nal.nuhLayerId = ((data[0] & 0x80) >> 7) | ((data[1] & 0xF8) >> 3);
            nal.nuhTemporalIdPlus1 = data[1] & 0x07;
        }
        extra.insert(extra.end(), data, data + len);
        nals.push_back(nal);
        return true;
    }

    void appendExtradata(const uint8_t* ex, int size, CodecType codec, std::vector<NALUnit>& nals,
                         std::vector<uint8_t>& extra, uint64_t extraBase, int& lengthSize)
    {
        lengthSize = 4;
        if (!ex || size <= 5 || ex[0] != 1)
            return;

        size_t pos;
        if (codec == CodecType::H264)
        {
            lengthSize = (ex[4] & 0x03) + 1;
            pos = 5;
            if (pos < (size_t)size)
            {
                const int n = ex[pos++] & 0x1F;
                for (int i = 0; i < n && pos + 2 <= (size_t)size; i++)
                {
                    const int len = (ex[pos] << 8) | ex[pos + 1];
                    pos += 2;
                    if (len <= 0 || pos + len > (size_t)size)
                        return;
                    appendExtraNalUnit(nals, extra, extraBase, ex + pos, len, codec);
                    pos += len;
                }
                if (pos < (size_t)size)
                {
                    const int n2 = ex[pos++];
                    for (int i = 0; i < n2 && pos + 2 <= (size_t)size; i++)
                    {
                        const int len = (ex[pos] << 8) | ex[pos + 1];
                        pos += 2;
                        if (len <= 0 || pos + len > (size_t)size)
                            return;
                        appendExtraNalUnit(nals, extra, extraBase, ex + pos, len, codec);
                        pos += len;
                    }
                }
            }
        }
        else
        {
            if (size < 23)
                return;
            lengthSize = (ex[21] & 0x03) + 1;
            pos = 22;
            const int arrays = ex[pos++];
            for (int a = 0; a < arrays && pos + 3 <= (size_t)size; a++)
            {
                pos += 1;
                const int n = (ex[pos] << 8) | ex[pos + 1];
                pos += 2;
                for (int i = 0; i < n && pos + 2 <= (size_t)size; i++)
                {
                    const int len = (ex[pos] << 8) | ex[pos + 1];
                    pos += 2;
                    if (len <= 0 || pos + len > (size_t)size)
                        return;
                    appendExtraNalUnit(nals, extra, extraBase, ex + pos, len, codec);
                    pos += len;
                }
            }
        }
    }

} // namespace

bool BitstreamParser::parseMp4Indexed(const std::string& path, CodecType& codec, const ProgressFn& progress)
{
    resetStorage();
    m_error.clear();

    if (!m_mapped.open(path, m_error))
        return false;
    m_base = m_mapped.data();

    const uint8_t* data = m_base;
    const uint64_t fileSize = m_mapped.size();

    // ---- locate moov ----
    Box moov;
    bool haveMoov = false;
    uint64_t top = 0;
    Box b;
    while (nextBox(data, fileSize, top, b))
    {
        if (b.type == 0x6D6F6F76u)
        { // 'moov'
            moov = b;
            haveMoov = true;
            break;
        }
    }
    if (!haveMoov)
    {
        m_error = "Not an MP4 with moov";
        resetStorage();
        return false;
    }

    // ---- pick the video track ----
    const uint64_t moovEnd = moov.offset + moov.size;
    const uint64_t moovBody = moov.offset + 8;
    bool foundVideo = false;
    Box stszBox, stcoBox, co64Box, stscBox, stsdBox;
    std::vector<Box> traks;
    {
        uint64_t pos = moovBody;
        Box box;
        while (nextBox(data, moovEnd, pos, box))
        {
            if (box.type == 0x7472616Bu)
                traks.push_back(box); // 'trak'
        }
    }

    for (const Box& trak : traks)
    {
        const uint64_t trakEnd = trak.offset + trak.size;
        Box mdia;
        if (!findBox(data, trak.offset + 8, trakEnd, 0x6D646961u, mdia))
            continue; // 'mdia'

        const uint64_t mdiaEnd = mdia.offset + mdia.size;
        // handler type must be 'vide'
        Box hdlr;
        if (!findBox(data, mdia.offset + 8, mdiaEnd, 0x68646C72u, hdlr))
            continue; // 'hdlr'
        if (hdlr.offset + 8 + 8 + 4 > fileSize)
            continue;
        if (readU32(data + hdlr.offset + 16) != 0x76696465u)
            continue; // 'vide'

        Box minf;
        if (!findBox(data, mdia.offset + 8, mdiaEnd, 0x6D696E66u, minf))
            continue; // 'minf'
        Box stbl;
        if (!findBox(data, minf.offset + 8, minf.offset + minf.size, 0x7374626Cu, stbl))
            continue; // 'stbl'

        const uint64_t stblEnd = stbl.offset + stbl.size;
        if (!findBox(data, stbl.offset + 8, stblEnd, 0x7374737Au, stszBox))
            continue; // 'stsz'
        if (!findBox(data, stbl.offset + 8, stblEnd, 0x73747363u, stscBox))
            continue; // 'stsc'
        foundVideo = findBox(data, stbl.offset + 8, stblEnd, 0x73747364u, stsdBox); // 'stsd'
        if (!findBox(data, stbl.offset + 8, stblEnd, 0x7374636Fu, stcoBox))
        { // 'stco'
            if (!findBox(data, stbl.offset + 8, stblEnd, 0x636F3634u, co64Box))
            { // 'co64'
                foundVideo = false;
            }
        }
        if (foundVideo)
            break;
    }

    if (!foundVideo)
    {
        m_error = "No indexed video track";
        resetStorage();
        return false;
    }

    // ---- codec + extradata from the first visual sample entry ----
    if (stsdBox.size < 16)
    {
        m_error = "Bad stsd";
        resetStorage();
        return false;
    }
    const uint64_t entryStart = stsdBox.offset + 16; // version/flags + entry count
    const uint64_t stsdEnd = stsdBox.offset + stsdBox.size;
    if (entryStart + 8 > stsdEnd)
    {
        m_error = "Bad stsd entry";
        resetStorage();
        return false;
    }
    const uint32_t entryType = readU32(data + entryStart + 4);
    if (entryType == 0x61766331u || entryType == 0x61766333u)
    { // avc1/avc3
        codec = CodecType::H264;
    }
    else if (entryType == 0x68766331u || entryType == 0x68657631u)
    { // hvc1/hev1
        codec = CodecType::H265;
    }
    else
    {
        m_error = "Unsupported video codec (only H.264/H.265)";
        resetStorage();
        return false;
    }
    m_codec = codec;

    Box cfg;
    const uint64_t childStart = entryStart + 8 + 78; // visual sample entry header
    if (!findBox(data, childStart, stsdEnd, codec == CodecType::H264 ? 0x61766343u : 0x68766343u, cfg))
    {
        m_error = "Missing avcC/hvcC (fragmented MP4?)";
        resetStorage();
        return false;
    }
    int lengthSize = 4;
    appendExtradata(data + cfg.offset + 8, (int)(cfg.size - 8), codec, m_nals, m_extra, m_mapped.size(), lengthSize);

    // ---- sample sizes ----
    if (stszBox.size < 20)
    {
        m_error = "Bad stsz";
        resetStorage();
        return false;
    }
    const uint8_t* stsz = data + stszBox.offset;
    const uint32_t uniformSize = readU32(stsz + 12);
    const uint32_t sampleCount = readU32(stsz + 16);
    if (sampleCount == 0)
    {
        m_error = "Empty sample table";
        resetStorage();
        return false;
    }
    std::vector<uint32_t> sizes;
    if (uniformSize != 0)
    {
        sizes.assign(sampleCount, uniformSize);
    }
    else
    {
        if (stszBox.size < 20 + (uint64_t)sampleCount * 4)
        {
            m_error = "Truncated stsz";
            resetStorage();
            return false;
        }
        sizes.resize(sampleCount);
        const uint8_t* tbl = stsz + 20;
        for (uint32_t i = 0; i < sampleCount; i++)
            sizes[i] = readU32(tbl + 4 * i);
    }

    // With small (dense) samples the header pages cover the whole file anyway;
    // a sequential demux is faster in that case.
    uint64_t totalSampleBytes = 0;
    for (uint32_t i = 0; i < sampleCount; i++)
        totalSampleBytes += sizes[i];
    if (sampleCount > 0 && (double)totalSampleBytes / (double)sampleCount < 4096.0)
    {
        m_error = "Dense sample table";
        resetStorage();
        return false;
    }

    // ---- chunk offsets ----
    std::vector<uint64_t> chunkOffsets;
    if (stcoBox.size >= 16)
    {
        const uint8_t* p = data + stcoBox.offset;
        const uint32_t n = readU32(p + 12);
        chunkOffsets.resize(n);
        for (uint32_t i = 0; i < n; i++)
            chunkOffsets[i] = readU32(p + 16 + 4 * i);
    }
    else if (co64Box.size >= 16)
    {
        const uint8_t* p = data + co64Box.offset;
        const uint32_t n = readU32(p + 12);
        chunkOffsets.resize(n);
        for (uint32_t i = 0; i < n; i++)
            chunkOffsets[i] = readU64(p + 16 + 8 * i);
    }
    if (chunkOffsets.empty())
    {
        m_error = "Missing chunk offsets";
        resetStorage();
        return false;
    }

    // ---- sample-to-chunk ----
    struct StscEntry
    {
        uint32_t firstChunk;
        uint32_t samplesPerChunk;
    };
    std::vector<StscEntry> stsc;
    if (stscBox.size >= 16)
    {
        const uint8_t* p = data + stscBox.offset;
        const uint32_t n = readU32(p + 12);
        if (stscBox.size < 16 + (uint64_t)n * 12)
        {
            m_error = "Truncated stsc";
            resetStorage();
            return false;
        }
        stsc.resize(n);
        for (uint32_t i = 0; i < n; i++)
        {
            stsc[i].firstChunk = readU32(p + 16 + 12 * i);
            stsc[i].samplesPerChunk = readU32(p + 20 + 12 * i);
        }
    }
    if (stsc.empty())
    {
        m_error = "Missing stsc";
        resetStorage();
        return false;
    }

    // ---- walk samples and build NAL units from length prefixes ----
    uint32_t sampleIdx = 0;
    size_t stscIdx = 0;
    for (uint32_t chunk = 0; chunk < chunkOffsets.size() && sampleIdx < sampleCount; chunk++)
    {
        while (stscIdx + 1 < stsc.size() && stsc[stscIdx + 1].firstChunk <= chunk + 1)
            stscIdx++;
        uint64_t offset = chunkOffsets[chunk];
        const uint32_t perChunk = stsc[stscIdx].samplesPerChunk;
        for (uint32_t k = 0; k < perChunk && sampleIdx < sampleCount; k++, sampleIdx++)
        {
            const uint32_t sampleSize = sizes[sampleIdx];
            const uint64_t sampleStart = offset;
            offset += sampleSize;
            if (sampleStart + sampleSize > fileSize)
                break;

            size_t rel = 0;
            while (rel + (size_t)lengthSize <= sampleSize)
            {
                uint32_t nalLen = 0;
                for (int bidx = 0; bidx < lengthSize; bidx++)
                    nalLen = (nalLen << 8) | data[sampleStart + rel + bidx];
                rel += (size_t)lengthSize;
                if (nalLen == 0 || rel + nalLen > sampleSize)
                    break;

                const uint8_t* np = data + sampleStart + rel;
                NALUnit nal;
                nal.codec = codec;
                nal.offset = sampleStart + rel;
                nal.size = nalLen;
                nal.startCodeLen = 0;
                nal.headerByte = np[0];
                if (codec == CodecType::H264)
                {
                    nal.nalType = np[0] & 0x1F;
                }
                else
                {
                    nal.nalType = (np[0] >> 1) & 0x3F;
                    if (nalLen > 1)
                    {
                        nal.nuhLayerId = ((np[0] & 0x80) >> 7) | ((np[1] & 0xF8) >> 3);
                        nal.nuhTemporalIdPlus1 = np[1] & 0x07;
                    }
                }
                m_nals.push_back(nal);
                rel += nalLen;
            }

            if (progress && (sampleIdx % 5000 == 4999))
            {
                if (!progress((double)sampleIdx / (double)sampleCount))
                {
                    m_error = "Cancelled";
                    resetStorage();
                    return false;
                }
            }
        }
    }

    if (m_nals.empty())
    {
        m_error = "No NAL units found";
        resetStorage();
        return false;
    }
    if (progress)
        progress(1.0);
    return true;
}
