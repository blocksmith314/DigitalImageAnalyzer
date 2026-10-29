extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t FONT5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00},  // '!'
    {0x00, 0x07, 0x00, 0x07, 0x00},  // '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // '$'
    {0x23, 0x13, 0x08, 0x64, 0x62},  // '%'
    {0x36, 0x49, 0x55, 0x22, 0x50},  // '&'
    {0x00, 0x05, 0x03, 0x00, 0x00},  // '\''
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // '('
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // ')'
    {0x14, 0x08, 0x3E, 0x08, 0x14},  // '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // '+'
    {0x00, 0x50, 0x30, 0x00, 0x00},  // ','
    {0x08, 0x08, 0x08, 0x08, 0x08},  // '-'
    {0x00, 0x60, 0x60, 0x00, 0x00},  // '.'
    {0x20, 0x10, 0x08, 0x04, 0x02},  // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // '1'
    {0x42, 0x61, 0x51, 0x49, 0x46},  // '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // '4'
    {0x27, 0x45, 0x45, 0x45, 0x39},  // '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // '6'
    {0x01, 0x71, 0x09, 0x05, 0x03},  // '7'
    {0x36, 0x49, 0x49, 0x49, 0x36},  // '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // '9'
    {0x00, 0x36, 0x36, 0x00, 0x00},  // ':'
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ';'
    {0x08, 0x14, 0x22, 0x41, 0x00},  // '<'
    {0x14, 0x14, 0x14, 0x14, 0x14},  // '='
    {0x00, 0x41, 0x22, 0x14, 0x08},  // '>'
    {0x02, 0x01, 0x51, 0x09, 0x06},  // '?'
    {0x32, 0x49, 0x59, 0x51, 0x3E},  // '@'
    {0x7C, 0x12, 0x11, 0x12, 0x7C},  // 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  // 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31},  // 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F},  // 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63},  // 'X'
    {0x07, 0x08, 0x70, 0x08, 0x07},  // 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43},  // 'Z'
};

static void draw_char_y(uint8_t* y, int stride, int x, int y_pos, char c,
                        uint8_t value) {
  if (c < 32 || c > 90) return;
  const uint8_t* glyph = FONT5x7[c - 32];
  for (int col = 0; col < 5; col++) {
    uint8_t mask = glyph[col];
    for (int row = 0; row < 7; row++) {
      if (mask & (1 << row)) {
        int yy = y_pos + row;
        int xx = x + col;
        if (yy >= 0 && yy < stride && xx >= 0 && xx < (int)stride) {
          y[yy * stride + xx] = value;
        }
      }
    }
  }
}

static void draw_text_yuv(AVFrame* frame, const char* text, int x, int y,
                          uint8_t yv, uint8_t u, uint8_t v, int scale) {
  int w = frame->width, h = frame->height;
  int ystride = frame->linesize[0];
  int ustride = frame->linesize[1];
  for (const char* p = text; *p; p++) {
    char ch = *p;
    if (ch < 32 || ch > 90) {
      x += 6 * scale;
      continue;
    }
    for (int r = 0; r < 7; r++) {
      for (int c = 0; c < 5; c++) {
        uint8_t mask = FONT5x7[ch - 32][c];
        if (mask & (1 << r)) {
          for (int sy = 0; sy < scale; sy++) {
            for (int sx = 0; sx < scale; sx++) {
              int yy = y + r * scale + sy;
              int xx = x + c * scale + sx;
              if (yy >= 0 && yy < h && xx >= 0 && xx < w) {
                frame->data[0][yy * ystride + xx] = yv;
                int hyy = yy / 2;
                int hxx = xx / 2;
                frame->data[1][hyy * ustride + hxx] = u;
                frame->data[2][hyy * ustride + hxx] = v;
              }
            }
          }
        }
      }
    }
    x += 6 * scale;
  }
}

static void fill_checkerboard(AVFrame* frame, int tile) {
  int w = frame->width, h = frame->height;
  int stride = frame->linesize[0];
  int half_w = (w + 1) / 2, half_h = (h + 1) / 2;
  int ustride = frame->linesize[1];

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int tx = x / tile;
      int ty = y / tile;
      uint8_t v = ((tx + ty) & 1) ? 28 : 48;
      frame->data[0][y * stride + x] = v;
    }
  }
  for (int y = 0; y < half_h; y++) {
    for (int x = 0; x < half_w; x++) {
      frame->data[1][y * ustride + x] = 128;
      frame->data[2][y * ustride + x] = 128;
    }
  }
}

static void draw_filled_rect_yuv(AVFrame* frame, int x, int y, int w, int h,
                                 uint8_t yv, uint8_t u, uint8_t v) {
  int fw = frame->width, fh = frame->height;
  int stride = frame->linesize[0];
  x = x < 0 ? 0 : x;
  y = y < 0 ? 0 : y;
  int x2 = x + w > fw ? fw : x + w;
  int y2 = y + h > fh ? fh : y + h;

  for (int yy = y; yy < y2; yy++) {
    for (int xx = x; xx < x2; xx++) {
      frame->data[0][yy * stride + xx] = yv;
    }
  }

  int half_w = (fw + 1) / 2, half_h = (fh + 1) / 2;
  int ustride = frame->linesize[1];
  int cx1 = x / 2, cy1 = y / 2;
  int cx2 = (x2 + 1) / 2, cy2 = (y2 + 1) / 2;
  if (cx2 > half_w) cx2 = half_w;
  if (cy2 > half_h) cy2 = half_h;
  for (int yy = cy1; yy < cy2; yy++) {
    for (int xx = cx1; xx < cx2; xx++) {
      frame->data[1][yy * ustride + xx] = u;
      frame->data[2][yy * ustride + xx] = v;
    }
  }
}

static void format_number(char* buf, int n, int width) {
  char tmp[32];
  snprintf(tmp, sizeof(tmp), "%d", n);
  int len = (int)strlen(tmp);
  int pad = width - len;
  int pos = 0;
  for (int i = 0; i < pad; i++) buf[pos++] = '0';
  memcpy(buf + pos, tmp, len);
  buf[pos + len] = '\0';
}

int main(int argc, char** argv) {
  int width = 352;
  int height = 288;
  int fps = 10;
  int total_frames = 120;
  int keyint = 5;
  const char* codec_name = "libx264";
  const char* output_path = "output.h264";

  if (argc >= 2) {
    width = atoi(argv[1]);
  }
  if (argc >= 3) {
    height = atoi(argv[2]);
  }
  if (argc >= 4) {
    fps = atoi(argv[3]);
  }
  if (argc >= 5) {
    total_frames = atoi(argv[4]);
  }
  if (argc >= 6) {
    keyint = atoi(argv[5]);
  }
  if (argc >= 7) {
    codec_name = argv[6];
  }
  if (argc >= 8) {
    output_path = argv[7];
  }

  int is_h265 = (strstr(codec_name, "265") != nullptr);
  int codec_id = is_h265 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;

  const AVCodec* codec = avcodec_find_encoder_by_name(codec_name);
  if (!codec) {
    fprintf(stderr, "encoder '%s' not found\n", codec_name);
    return 1;
  }

  AVCodecContext* enc = avcodec_alloc_context3(codec);
  enc->width = width;
  enc->height = height;
  enc->time_base = (AVRational){1, fps};
  enc->framerate = (AVRational){fps, 1};
  enc->pix_fmt = AV_PIX_FMT_YUV420P;
  enc->gop_size = keyint;
  enc->max_b_frames = 2;
  enc->bit_rate = 500000;
  enc->thread_count = 1;

  if (!is_h265) {
    av_opt_set(enc->priv_data, "preset", "veryfast", 0);
    av_opt_set(enc->priv_data, "scenecut", "0", 0);
    av_opt_set(enc->priv_data, "keyint_min", "0", 0);
    av_opt_set(enc->priv_data, "slices", "1", 0);
    av_opt_set(enc->priv_data, "threads", "1", 0);
  } else {
    av_opt_set(enc->priv_data, "preset", "veryfast", 0);
    av_opt_set(enc->priv_data, "scenecut", "0", 0);
    av_opt_set(enc->priv_data, "keyint_min", "0", 0);
    av_opt_set(enc->priv_data, "slices", "1", 0);
    av_opt_set(enc->priv_data, "threads", "1", 0);
  }

  if (avcodec_open2(enc, codec, nullptr) < 0) {
    fprintf(stderr, "failed to open encoder\n");
    avcodec_free_context(&enc);
    return 1;
  }

  AVFormatContext* fmt = nullptr;
  avformat_alloc_output_context2(&fmt, nullptr, is_h265 ? "hevc" : "h264",
                                 output_path);
  if (!fmt) {
    fprintf(stderr, "failed to create output format\n");
    avcodec_free_context(&enc);
    return 1;
  }

  if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&fmt->pb, output_path, AVIO_FLAG_WRITE) < 0) {
      fprintf(stderr, "failed to open '%s'\n", output_path);
      avformat_free_context(fmt);
      avcodec_free_context(&enc);
      return 1;
    }
  }

  AVStream* st = avformat_new_stream(fmt, nullptr);
  avcodec_parameters_from_context(st->codecpar, enc);
  st->time_base = enc->time_base;

  if (avformat_write_header(fmt, nullptr) < 0) {
    fprintf(stderr, "failed to write header\n");
    if (!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
    avformat_free_context(fmt);
    avcodec_free_context(&enc);
    return 1;
  }

  AVFrame* frame = av_frame_alloc();
  frame->format = AV_PIX_FMT_YUV420P;
  frame->width = width;
  frame->height = height;
  av_frame_get_buffer(frame, 32);

  AVPacket* pkt = av_packet_alloc();

  double box_w = 36;
  double box_h = 36;
  double margin = 20;
  double total_duration = (double)total_frames / fps;
  double start_x = margin;
  double end_x = width - margin - box_w;
  double y_center = height * 0.5;
  double y_amplitude = height * 0.3;
  int sine_cycles = 2;

  for (int f = 0; f < total_frames; f++) {
    double t = (double)f / fps;
    double progress = (double)f / (total_frames - 1);

    fill_checkerboard(frame, 16);

    double box_x = start_x + (end_x - start_x) * progress;
    double box_y =
        y_center + y_amplitude * sin(progress * 2.0 * M_PI * sine_cycles);

    int ix = (int)box_x;
    int iy = (int)box_y;

    uint8_t frame_num = (uint8_t)(200 + (f * 13) % 40);
    draw_filled_rect_yuv(frame, ix, iy, (int)box_w, (int)box_h, 235, 80, 240);

    char pos[48];
    snprintf(pos, sizeof(pos), "F=%d", f);
    draw_text_yuv(frame, pos, 8, 8, 235, 128, 128, 2);

    char label[32];
    format_number(label, f, 4);
    int label_scale = 2;
    int label_w = 6 * 5 * label_scale;
    draw_text_yuv(frame, label, width - label_w - 8, 8, 235, 128, 128,
                  label_scale);

    snprintf(pos, sizeof(pos), "%dX%d %dFPS", width, height, fps);
    draw_text_yuv(frame, pos, 8, height - 22, 235, 128, 128, 2);

    char key_label[16];
    if (f == 0 || f % keyint == 0) {
      snprintf(key_label, sizeof(key_label), "KEY");
      draw_text_yuv(frame, key_label, width - 50, height - 22, 235, 128, 128,
                    2);
    }

    frame->pts = f;
    frame->pict_type = AV_PICTURE_TYPE_NONE;
    if (f == 0 || f % keyint == 0) {
      frame->flags |= AV_FRAME_FLAG_KEY;
    }

    avcodec_send_frame(enc, frame);
    while (avcodec_receive_packet(enc, pkt) == 0) {
      av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
      pkt->stream_index = 0;
      av_interleaved_write_frame(fmt, pkt);
      av_packet_unref(pkt);
    }
  }

  avcodec_send_frame(enc, nullptr);
  while (avcodec_receive_packet(enc, pkt) == 0) {
    av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
    pkt->stream_index = 0;
    av_interleaved_write_frame(fmt, pkt);
    av_packet_unref(pkt);
  }

  av_write_trailer(fmt);

  av_frame_free(&frame);
  av_packet_free(&pkt);
  if (!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
  avformat_free_context(fmt);
  avcodec_free_context(&enc);

  printf("generated: %s (%d frames, %dx%d, %dfps, keyint=%d)\n", output_path,
         total_frames, width, height, fps, keyint);
  return 0;
}
