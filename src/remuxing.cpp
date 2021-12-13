/*
 * Copyright (c) 2013 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * This file is remuxing.c from the ffmpeg examples, slightly modified to be able
 * to compile with C++. Then it adds a proof of concept using TFlite for background
 * segregation.
 */

/**
 * @file
 * libavformat/libavcodec demuxing and muxing API example.
 *
 * Remux streams from one container format to another.
 * @example remuxing.c
 */

#define __STDC_CONSTANT_MACROS

extern "C" {
#define __STDC_CONSTANT_MACROS

#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>

  // to get from an AVStream to a friggin AVFrame, I see no other solution
//#include "libavcodec/internal.h"
#include "libavformat/internal.h"
}

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

// Below was required after updating to Ubuntu 18.04.
#ifdef __cplusplus
static const std::string av_make_error_string(int errnum) {
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
  return (std::string)errbuf;
}

#undef av_err2str
#define av_err2str(errnum) av_make_error_string(errnum).c_str()

static const std::string av_ts_make_string(int64_t ts) {
  char buf[AV_TS_MAX_STRING_SIZE];
  if (ts == AV_NOPTS_VALUE)
    snprintf(buf, AV_TS_MAX_STRING_SIZE, "NOPTS");
  else
    snprintf(buf, AV_TS_MAX_STRING_SIZE, "%" PRId64, ts);
  return (std::string)buf;
}
#undef av_ts2str
#define av_ts2str(ts) av_ts_make_string(ts).c_str()

static const std::string av_ts_make_time_string(int64_t ts, AVRational *tb) {
  char buf[AV_TS_MAX_STRING_SIZE];
  if (ts == AV_NOPTS_VALUE)
    snprintf(buf, AV_TS_MAX_STRING_SIZE, "NOPTS");
  else
    snprintf(buf, AV_TS_MAX_STRING_SIZE, "%.6g", av_q2d(*tb) * ts);
  return buf;
}
#undef av_ts2timestr
#define av_ts2timestr(ts, tb) av_ts_make_time_string(ts, tb).c_str()
#endif  // __cplusplus

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag) {
  AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

  printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
         tag,
         av_ts2str(pkt->pts),
         av_ts2timestr(pkt->pts, time_base),
         av_ts2str(pkt->dts),
         av_ts2timestr(pkt->dts, time_base),
         av_ts2str(pkt->duration),
         av_ts2timestr(pkt->duration, time_base),
         pkt->stream_index);
}

#include "tensorflow/lite/builtin_op_data.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"
#include "tensorflow/lite/string_util.h"

// Taken from mediapipe project
#include "transpose_conv_bias.h"

// Implemented in blur_float.cpp
extern void fast_gaussian_blur(float *&in, float *&out, int w, int h, float sigma);

#include <fstream>

#include <chrono>
#include <mutex>
#include <thread>

enum segmentation_model {
  google_meet_full = 1,
  google_meet_lite,
  mlkit,
};

struct model_meta_info {
  std::string filename;
  int width;
  int height;
};

enum segmentation_mode {
  bypass = 0,
  black_background,
  blur_background,
  virtual_background,
  virtual_background_blurred
};

class program {
private:
  float sigma_bg_blur = 4.;
  float sigma_segmask = 1.2;
  int src_w = 640;
  int src_h = 480;
  std::string bg_file = "bg.ayuv";
  std::map<segmentation_model, model_meta_info> models;
  segmentation_mode mode;
  segmentation_model model_selected;
  model_meta_info model;
  const char *in_filename = nullptr;
  const char *out_filename = nullptr;
  bool animate = false;

  std::unique_ptr<tflite::Interpreter> interpreter;

  AVPacket *pkt_copy_ = nullptr;

  std::vector<uint8_t> bg;
  std::vector<std::vector<uint8_t>> anim_bg;

  std::vector<float> mask;
  std::vector<float> background_y, background_u, background_v;

  float *mask_pixels = nullptr;
  float *mask_out = nullptr;
  float *mask_out_2 = nullptr;

public:
  program(int argc, char **argv);

  int run();

  void flappy(AVPacket &pkt_copy);

  void fill_input_tensor(const AVPacket &pkt_copy);

  void upscale_segregation_mask(const AVPacket &pkt_copy);

  void blur_yuv();

  void temp123(uint8_t *&vbg, float *&bg_y, float *&bg_u, float *&bg_v);
};

program::program(int argc, char **argv)
    : models({
          {google_meet_full, {"models/segm_full_v679.tflite", 256, 144}},
          {google_meet_lite, {"models/segm_lite_v681.tflite", 160, 96}},
          {mlkit, {"models/selfiesegmentation_mlkit-256x256-2021_01_19-v1215.f16.tflite", 256, 256}},
      }) {
  if (argc < 5) {
    printf(
        "usage: %s input output mode model\n"
        "API example program to remux a media file with libavformat and libavcodec.\n"
        "The output format is guessed according to the file extension.\n"
        "mode can be: 1 (black bg), 2 (blur bg), 3 (vbg), 4 (vbg+blur)\n"
        "model can be: 1 (google meet v679 full), 2 (google meet v681 lite), 3 (mlkit)\n"
        "\n",
        argv[0]);
    std::exit(1);
  }
  in_filename = argv[1];
  out_filename = argv[2];

  if (const char *env_p = std::getenv("BG")) {
    bg_file = std::string(env_p);
  }
  mode = segmentation_mode::blur_background;
  // const auto mode = (segmentation_mode)std::stoi(argv[3]);
  model_selected = (segmentation_model)std::stoi(argv[4]);
  model = models[model_selected];

  // Load a background to test
  auto load = [](std::vector<uint8_t> &bg, const std::string &bg_file) {
    std::ifstream file(bg_file, std::ios::binary | std::ios::ate);
    if (!file.good()) {
      return 1;
    }
    std::streamsize size = file.tellg();
    if (size == std::streamsize(0)) {
      return 1;
    }
    file.seekg(0, std::ios::beg);
    bg.reserve(size);
    if (!file.read((char *)bg.data(), size)) {
      return 1;
    }
    return 0;
  };
  load(bg, bg_file);

  if (animate) {
    anim_bg.reserve(750);
    std::stringstream ss;
    for (size_t i = 0; i < 750; i++) {
      ss << "spaceship/" << i << ".ayuv";
      std::cout << "loading: " << ss.str() << std::endl;
      load(anim_bg[i], ss.str());
      ss.str("");
      ss.clear();
    }
  }
}

int program::run() {
  // Load model
  std::unique_ptr<tflite::FlatBufferModel> tflite_model(tflite::FlatBufferModel::BuildFromFile(model.filename.c_str()));
  if (!tflite_model) {
    printf("Failed to model\n");
    exit(0);
  } else {
    printf("Loaded model\n");
  }

  tflite::ops::builtin::BuiltinOpResolver resolver;
  // Custom op for Google Meet network
  resolver.AddCustom("Convolution2DTransposeBias", mediapipe::tflite_operations::RegisterConvolution2DTransposeBias());
  tflite::InterpreterBuilder builder(*tflite_model, resolver);
  builder(&interpreter);

  // Resize input tensors, if desired.
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    fprintf(stderr, "Something wrong");
    exit(1);
  }

  AVOutputFormat *ofmt = NULL;
  AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
  AVPacket pkt;
  AVPacket pkt_out;
  struct SwsContext *sws_ctx = nullptr;
  int ret, i;
  int stream_index = 0;
  int *stream_mapping = NULL;
  int stream_mapping_size = 0;

  // Needed for detecting v4l2
  avdevice_register_all();

  AVPixelFormat video_format = AV_PIX_FMT_NONE;

  // Specify v4l2 as the input format (cannot be detected from filename /dev/videoX)
  AVInputFormat *input_format = av_find_input_format("v4l2");
  if ((ret = avformat_open_input(&ifmt_ctx, in_filename, input_format, 0)) < 0) {
    fprintf(stderr, "Could not open input file '%s'", in_filename);
    goto end;
  }

  if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
    fprintf(stderr, "Failed to retrieve input stream information");
    goto end;
  }

  // Specify v4l2 as the output format (cannot be detected from filename /dev/videoX)
  avformat_alloc_output_context2(&ofmt_ctx, NULL, "v4l2", out_filename);
  if (!ofmt_ctx) {
    fprintf(stderr, "Could not create output context\n");
    ret = AVERROR_UNKNOWN;
    goto end;
  }

  av_dump_format(ifmt_ctx, 0, "", 0);
  av_dump_format(ofmt_ctx, 0, "", 1);

  stream_mapping_size = ifmt_ctx->nb_streams;
  stream_mapping = (int *)av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
  if (!stream_mapping) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  ofmt = ofmt_ctx->oformat;

  for (i = 0; i < ifmt_ctx->nb_streams; i++) {
    AVStream *out_stream;
    AVStream *in_stream = ifmt_ctx->streams[i];
    AVCodecParameters *in_codecpar = in_stream->codecpar;

    if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO && in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
        in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
      stream_mapping[i] = -1;
      continue;
    }
    if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_format = (AVPixelFormat)in_codecpar->format;
      std::cout << "The codec pixfmt: " << in_codecpar->format << std::endl;
      if (in_codecpar->format != AV_PIX_FMT_YUV420P)
        std::cout << "Input is not YUV420P, will have to convert!" << std::endl;

      // see libav/avformat.h
      // AV_PIX_FMT_YUV420P,   ///< planar YUV 4:2:0, 12bpp, (1 Cr & Cb sample per 2x2 Y samples)
      // AV_PIX_FMT_YUYV422,   ///< packed YUV 4:2:2, 16bpp, Y0 Cb Y1 Cr
    }

    stream_mapping[i] = stream_index++;

    out_stream = avformat_new_stream(ofmt_ctx, NULL);
    if (!out_stream) {
      fprintf(stderr, "Failed allocating output stream\n");
      ret = AVERROR_UNKNOWN;
      goto end;
    }

    ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
    if (ret < 0) {
      fprintf(stderr, "Failed to copy codec parameters\n");
      goto end;
    }
    out_stream->codecpar->format = AV_PIX_FMT_YUV420P;
    out_stream->codecpar->width = 640;
    out_stream->codecpar->height = 480;
  }
  av_dump_format(ofmt_ctx, 0, out_filename, 1);
//  ofmt_ctx->video_codec->w

  if (!(ofmt->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
      fprintf(stderr, "Could not open output file '%s'", out_filename);
      goto end;
    }
  }

  ret = avformat_write_header(ofmt_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "Error occurred when opening output file\n");
    goto end;
  }

  while (1) {
    mask.clear();
    background_y.clear();
    background_u.clear();
    background_v.clear();

    AVStream *in_stream, *out_stream;

    ret = av_read_frame(ifmt_ctx, &pkt);
    static int counter = 0;
    counter++;
    if (counter % 3 != 0) {
      // WILL THIS LEAK?
      // av_packet_unref(&pkt);
      continue;
    }
    if (ret < 0) break;

    in_stream = ifmt_ctx->streams[pkt.stream_index];
    if (pkt.stream_index >= stream_mapping_size || stream_mapping[pkt.stream_index] < 0) {
      av_packet_unref(&pkt);
      continue;
    }

    if (in_stream->codecpar->format != AV_PIX_FMT_YUV420P) {
      /* create scaling context */
      if (sws_ctx == nullptr)
        sws_ctx = sws_getContext(src_w,
                                 src_h,
                                 (AVPixelFormat)in_stream->codecpar->format,
                                 640,
                                 480,
                                 AV_PIX_FMT_YUV420P,
                                 SWS_BILINEAR,
                                 NULL,
                                 NULL,
                                 NULL);
      if (!sws_ctx) {
        fprintf(stderr,
                "Impossible to create scale context for the conversion "
                "fmt:s s:%dx%d -> fmt:s s:%dx%d\n",
                // av_get_pix_fmt_name((AVPixelFormat)in_stream->codecpar->format),
                src_w,
                src_h,
                // av_get_pix_fmt_name(AV_PIX_FMT_YUV420P),
                640,
                480);
        ret = AVERROR(EINVAL);
        goto end;
      }
    }

    pkt.stream_index = stream_mapping[pkt.stream_index];
    out_stream = ofmt_ctx->streams[pkt.stream_index];

    // log_packet(ifmt_ctx, &pkt, "in");

    /* copy packet */
    pkt.pts = av_rescale_q_rnd(
        pkt.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    pkt.dts = av_rescale_q_rnd(
        pkt.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
    pkt.pos = -1;
    // log_packet(ofmt_ctx, &pkt, "out");


    // TODO: this work-around doesn't make any sense to me, even though it works.
//    if (pkt_copy_ == nullptr) {
//      pkt_copy_ = new AVPacket;
//      av_new_packet(pkt_copy_, pkt.size);
//    }
//    av_copy_packet(pkt_copy_, &pkt);
//    AVPacket &pkt_copy = *pkt_copy_;
//    std::cout << "RBU pkt_copy.size: " << pkt_copy.size << std::endl;

    if (true) {
//      AVFrame *dstframe = av_frame_alloc();
//      if (dstframe == NULL) {
//        fprintf(stderr, "Error: av_frame_alloc() failed.\n");
//        exit(EXIT_FAILURE);
//      }
//
//      dstframe->format = AV_PIX_FMT_UYVY422; /* choose same format set on sws_getContext() */
//      //    auto &srcframe = ifmt_ctx.;
//      dstframe->width = 640;//srcframe->width;     /* must match sizes as on sws_getContext() */
//      dstframe->height = 480;//srcframe->height;   /* must match sizes as on sws_getContext() */
//      int ret = av_frame_get_buffer(dstframe, 32);
//      if (ret < 0) {
//        fprintf(stderr, "Error: could not allocate the video frame data\n");
//        exit(EXIT_FAILURE);
//      }

      // do the conversion */
      AVFrame *picture;
      int ret;
      picture = av_frame_alloc();
      if (picture) {
        picture->format = AV_PIX_FMT_YUV420P;
        picture->width = 640;
        picture->height = 480;

        /* allocate the buffers for the frame data */
        ret = av_frame_get_buffer(picture, 32);
        if (ret < 0) {
          fprintf(stderr, "Could not allocate frame data.\n");
          exit(1);
        }

        //MARK2
        AVCodecContext *avctx = out_stream->internal->avctx;
        avcodec_send_packet(avctx, &pkt);
        if (ret < 0) {
          /* error handling */
          printf("ERROR1: %d\n", ret);
        }
//
        if (pkt_out.size == 28) {
          // TODO: better solution
          av_new_packet(&pkt_out, 640 * 480 * 1.5);
        }


//        const AVCodec *codec;
//        int got_picture = 1, ret = 0;
        //AVFrame *frame_in = av_frame_alloc();
//        ret = avcodec_receive_frame(avctx, picture);
        avcodec_receive_packet(avctx, &pkt_out);
        if (ret < 0) {
          /* error handling */
          printf("ERROR2: %d\n", ret);
        }

        /* Copy here too */
        pkt_out.pts = pkt.pts;
        pkt_out.dts = pkt.dts;
        pkt_out.duration = pkt.duration;
        pkt_out.pos = -1;
//        pkt_out.size = pkt.size;

//        const uint8_t * const ptr = pkt.data;
//        int w = 640;
//        int h = 480;
//        int dstStride[4] = {w, w, w, w};
//        ret = sws_scale(sws_ctx,             /* SwsContext* on step (1) */
//                      frame_in->data,      /* srcSlice[] from decoded AVFrame */
//                      frame_in->linesize,//pkt_copy.linesize,  /* srcStride[] from decoded AVFrame */
//                      0,                   /* srcSliceY   */
//                      h,          /* srcSliceH  from decoded AVFrame */
//                      picture->data,      /* dst[]       */
//                      picture->linesize); /* dstStride[] */

//      if (ret < 0) {
//        /* error handling */
//        printf("ERROR\n");
//
//      }

      } else {
        printf(" NO PIC \n");
      }

//      picture->
      ///
//      ret = av_read_frame(ofmt_ctx, &pkt_out);

      //memcpy(pkt_out.data, pkt.data, pkt_out.size);

      // memcpy(pkt_out.data, picture->data, pkt_out.size);
      // memcpy(pkt_data, pkt.data, n);
      // memset(pkt.data, 0x00, n);
//      memcpy(pkt_out.data, picture->data, sizeof(pkt_out.data));

      /* encode the image */
      // MARK

//      int got_output = 0;
//      av_init_packet(&pkt_out);
//      pkt.data = NULL;    // packet data will be allocated by the encoder
//      pkt.size = 0;
//
//      ret = avcodec_encode_video2(out_stream->codec, &pkt_out, picture, &got_output);
//      if (ret < 0) {
//        fprintf(stderr, "Error encoding frame\n");
//        exit(1);
//      }

    }
    // flappy(pkt);

    // ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

    av_shrink_packet(&pkt_out, 640 * 480 * 1.5);

    ret = av_write_frame(ofmt_ctx, &pkt_out);
    if (ret < 0) {
      fprintf(stderr, "Error muxing packet\n");
      break;
    }
    av_packet_unref(&pkt);
//    av_packet_unref(&pkt_copy);

    // see if this helps
    // std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  av_write_trailer(ofmt_ctx);
end:

  avformat_close_input(&ifmt_ctx);

  /* close output */
  if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE)) avio_closep(&ofmt_ctx->pb);
  avformat_free_context(ofmt_ctx);

  av_freep(&stream_mapping);

  if (ret < 0 && ret != AVERROR_EOF) {
    fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
    return 1;
  }

  return 0;
}

void program::flappy(AVPacket &pkt_copy) {
  // AVPacket &pkt_copy = *pkt_copy_;

  std::vector<float> pixels;

  // Fill input tensor with RGB values
  fill_input_tensor(pkt_copy);

  // Run inference
  interpreter->Invoke();

  // Upscale resulting segregation mask
  upscale_segregation_mask(pkt_copy);

  // We need two copies of the mask for blurring
  blur_yuv();
  uint8_t *vbg;
  float *bg_y;
  float *bg_u;
  float *bg_v;
  temp123(vbg, bg_y, bg_u, bg_v);

  for (int y = 0; y < src_h; y++) {
    for (int x = 0; x < src_w; x++) {
      int index_Y = y * src_w + x;
      int index_U = (src_w * src_h) + (y / 2) * (src_w / 2) + x / 2;
      int index_V = (int)((src_w * src_h) * 1.25 + (y / 2) * (src_w / 2) + x / 2);

      float mask_alpha_ratio = *mask_out_2++;
      const auto val = mask_alpha_ratio * 255.f;
      float bg_alpha_ratio = 1.0f - mask_alpha_ratio;

      // blend person on top of background using mask
      switch (mode) {
        case black_background:
          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(0x00 * bg_alpha_ratio);
          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(0x80 * bg_alpha_ratio);
          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(0x80 * bg_alpha_ratio);
          break;
//        case blur_background:
//          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(255 * *bg_y++ * bg_alpha_ratio);
//          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(255 * *bg_u++ * bg_alpha_ratio);
//          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(255 * *bg_v++ * bg_alpha_ratio);
//          break;
//        case virtual_background:
//          vbg++;  // skip alpha channel
//          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
//          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
//          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
//          break;
//        case virtual_background_blurred:
//          vbg++;  // skip alpha channel
//          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
//          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
//          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
//          break;
      }
    }
  }
}

void program::temp123(uint8_t *&vbg, float *&bg_y, float *&bg_u, float *&bg_v) {
  auto bgv = bg;
  vbg= ([&]() {
      if (!animate) {
        auto *vbg = mode == virtual_background_blurred ? bgv.data() : bg.data();
        return vbg;
      }
      static size_t index = 0;
      if (index == 750) {
        index = 0;
      };
      auto *vbg = anim_bg[index].data();
      index++;
      return vbg;
    })();
  bg_y= background_y.data();
  bg_u= background_u.data();
  bg_v= background_v.data();// blurring virtual background

  if (mode == virtual_background_blurred) {
    std::vector<float> bg_y, bg_u, bg_v;

    auto vb = bgv.begin();
    for (int i = 0; i < (src_w * src_h); i++) {
      vb++;
      bg_y.push_back(*vb++ / 255.);
      bg_u.push_back(*vb++ / 255.);
      bg_v.push_back(*vb++ / 255.);
    }
    const auto blur_channel = [&](std::vector<float> &channel) {
      auto channel_copy = channel;
      float *bg1f = (float *)channel.data();
      float *bg2f = (float *)channel_copy.data();
      fast_gaussian_blur(bg1f, bg2f, src_w, src_h, sigma_bg_blur);
    };
    blur_channel(bg_y);
    blur_channel(bg_u);
    blur_channel(bg_v);

    // read back
    vb = bgv.begin();
    for (size_t i = 0; i < bg_y.size(); i++) {
      vb++;                    // a
      *vb++ = bg_y[i] * 255.;  // y
      *vb++ = bg_u[i] * 255.;  // u
      *vb++ = bg_v[i] * 255.;  // v
    }
  }

  // blur the mask as well, since we scaled it up
  fast_gaussian_blur(mask_pixels, mask_out, src_w, src_h, sigma_segmask);

  // TODO if animate somehow
// std::cout << "a: " << bgv.size() << " vs " << bg.size() << std::endl;
}

void program::blur_yuv() {
  auto mask2 = mask; // We need two copies for blurring the bg as well
  mask_pixels = mask.data();
  mask_out = mask2.data();
  mask_out_2 = mask2.data();
  const auto blur_channel = [&](std::vector<float> &channel) {
    auto channel2 = channel;
    float *bg_y = channel.data();
    float *bg_y2 = channel2.data();
    // blurring Y channel is sufficient
    // EDIT: actually, not in all cases...
    fast_gaussian_blur(bg_y, bg_y2, src_w, src_h, sigma_bg_blur);
  };
  blur_channel(background_y);
  blur_channel(background_u);
  blur_channel(background_v);
}

void program::upscale_segregation_mask(const AVPacket &pkt_copy) {
  float *output = interpreter->typed_output_tensor<float>(0);
  for (int y = 0; y < src_h; y++) {
    for (int x = 0; x < src_w; x++) {
      const int src_y = y * (model.height / float(src_h));
      const int src_x = x * (model.width / float(src_w));
      const auto src_offset = (src_y * model.width) + src_x;

      const auto val = ([&]() -> uint8_t {
        switch (model_selected) {
          case mlkit: {
            // Index into the right location in output
            const auto person = *(output + src_offset);
            return (255 * person);
            break;
          }
          case google_meet_full:
          case google_meet_lite:
            // Index into the right location in output
            const auto bg = *(output + src_offset * 2);
            const auto person = *(output + src_offset * 2 + 1);
            const auto shift = std::max(bg, person);
            const auto backgroundExp = exp(bg - shift);
            const auto personExp = exp(person - shift);
            // Sets only the alpha component of each pixel
            return (255 * personExp) / (backgroundExp + personExp);  // softmax
            break;
        }
      })();
      mask.push_back(float(val) / 255.0f);

      // Background Y for blurring
      int index_Y = y * src_w + x;
      int index_U = (src_w * src_h) + (y / 2) * (src_w / 2) + x / 2;
      int index_V = (int)((src_w * src_h) * 1.25 + (y / 2) * (src_w / 2) + x / 2);
      background_y.push_back(pkt_copy.data[index_Y] / 255.);
      background_u.push_back(pkt_copy.data[index_U] / 255.);
      background_v.push_back(pkt_copy.data[index_V] / 255.);
    }
  }
}

void program::fill_input_tensor(const AVPacket &pkt_copy) {
  auto *input = interpreter->typed_tensor<float>(0);
  for (int y = 0; y < model.height; y++) {
    const float ratio_1 = y / float(model.height);
    for (int x = 0; x < model.width; x++) {
      const float ratio_2 = x / float(model.width);
      int src_y = ratio_1 * src_h;
      int src_x = ratio_2 * src_w;

      int index_Y = src_y * src_w + src_x;
      int index_U = ((src_w * src_h) + (src_y / 2) * (src_w / 2) + src_x / 2);
      int index_V = (int)((src_w * src_h) * 1.25 + (src_y / 2) * (src_w / 2) + src_x / 2);

      int Y = pkt_copy.data[index_Y];
      int U = pkt_copy.data[index_U];
      int V = pkt_copy.data[index_V];

      int R = (int)(Y + 1.402f * (V - 128));
      int G = (int)(Y - 0.344f * (U - 128) - 0.714f * (V - 128));
      int B = (int)(Y + 1.772f * (U - 128));

      *input++ = float(R / 255.0);
      *input++ = float(G / 255.0);
      *input++ = float(B / 255.0);
    }
  }
}

int main(int argc, char **argv) {
  program prog(argc, argv);
  prog.run();
}
