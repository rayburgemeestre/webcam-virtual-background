#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#include "ffmpeg_headers.hpp"
#include "program.h"

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
  mode = (segmentation_mode)std::stoi(argv[3]);
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
      ss << "backgrounds/spaceship/" << i << ".ayuv";
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
      if (in_codecpar->format != AV_PIX_FMT_YUV420P) {
        // see libav/avformat.h
        // AV_PIX_FMT_YUV420P,   ///< planar YUV 4:2:0, 12bpp, (1 Cr & Cb sample per 2x2 Y samples)
        // AV_PIX_FMT_YUYV422,   ///< packed YUV 4:2:2, 16bpp, Y0 Cb Y1 Cr
        throw std::runtime_error("Currently only YUV420P devices are supported.");
      }
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
    // out_stream->codecpar->format = AV_PIX_FMT_YUV420P;
    // out_stream->codecpar->width = 640;
    // out_stream->codecpar->height = 480;
  }
  av_dump_format(ofmt_ctx, 0, out_filename, 1);

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
    if (ret < 0) break;

    in_stream = ifmt_ctx->streams[pkt.stream_index];
    if (pkt.stream_index >= stream_mapping_size || stream_mapping[pkt.stream_index] < 0) {
      av_packet_unref(&pkt);
      continue;
    }

    pkt.stream_index = stream_mapping[pkt.stream_index];
    out_stream = ofmt_ctx->streams[pkt.stream_index];

    /* copy packet */
    pkt.pts = av_rescale_q_rnd(
        pkt.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    pkt.dts = av_rescale_q_rnd(
        pkt.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
    pkt.pos = -1;
    log_packet(ofmt_ctx, &pkt, "out");

    static AVPacket *pkt_copy_ = nullptr;
    if (pkt_copy_ == nullptr) {
      pkt_copy_ = new AVPacket;
      av_new_packet(pkt_copy_, pkt.size);
    }

    av_copy_packet(pkt_copy_, &pkt);
    AVPacket &pkt_copy = *pkt_copy_;

    process_frame(pkt);

    ret = av_write_frame(ofmt_ctx, &pkt);
    if (ret < 0) {
      fprintf(stderr, "Error muxing packet\n");
      break;
    }
    av_packet_unref(&pkt);
    av_packet_unref(&pkt_copy);

    // TODO: make optional, chromium doesn't seem to handle too many frames very well..
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  av_write_trailer(ofmt_ctx);
end:

  avformat_close_input(&ifmt_ctx);

  if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE)) avio_closep(&ofmt_ctx->pb);
  avformat_free_context(ofmt_ctx);

  av_freep(&stream_mapping);

  if (ret < 0 && ret != AVERROR_EOF) {
    fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
    return 1;
  }

  return 0;
}

void program::load_tensorflow_model() {
  // TODO : move the code into here, properly.
}

void program::process_frame(AVPacket &pkt_copy) {
  std::vector<float> pixels;

  // Fill input tensor with RGB values
  fill_input_tensor(pkt_copy);

  // Run inference
  interpreter->Invoke();

  // Upscale resulting segregation mask
  upscale_segregation_mask(pkt_copy);

  // We need two copies of the mask for blurring
  blur_yuv();

  float *bg_y = background_y.data();
  float *bg_u = background_u.data();
  float *bg_v = background_v.data();

  set_virtual_background_source();

  blur_virtual_background_itself();

  // blur the mask as well, since we scaled it up
  fast_gaussian_blur(mask_pixels, mask_out, src_w, src_h, sigma_segmask);

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
        case blur_background:
          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(255 * *bg_y++ * bg_alpha_ratio);
          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(255 * *bg_u++ * bg_alpha_ratio);
          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(255 * *bg_v++ * bg_alpha_ratio);
          break;
        case virtual_background:
          vbg++;  // skip alpha channel
          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
          break;
        case virtual_background_blurred:
          vbg++;  // skip alpha channel
          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
          break;
      }
    }
  }
}

void program::set_virtual_background_source() {
  if (!animate) {
    vbg = bg.data();
    return;
  }
  static size_t index = 0;
  if (index == 750) {
    index = 0;
  };
  vbg = anim_bg[index].data();
  index++;
}

void program::blur_virtual_background_itself() {
  if (mode == virtual_background_blurred) {
    std::vector<float> bg_y, bg_u, bg_v;

    auto vb = vbg;
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
    vb = vbg;
    for (size_t i = 0; i < bg_y.size(); i++) {
      vb++;                    // a
      *vb++ = bg_y[i] * 255.;  // y
      *vb++ = bg_u[i] * 255.;  // u
      *vb++ = bg_v[i] * 255.;  // v
    }
  }
}

void program::blur_yuv() {
  mask2 = mask;  // We need two copies for blurring the bg as well
  mask_out = mask2.data();
  mask_out_2 = mask2.data();
  const auto blur_channel = [&](std::vector<float> &channel) {
    auto channel2 = channel;
    float *bg_y = channel.data();
    float *bg_y2 = channel2.data();
    fast_gaussian_blur(bg_y, bg_y2, src_w, src_h, sigma_bg_blur);
  };
  blur_channel(background_y);
  blur_channel(background_u);
  blur_channel(background_v);

  // gaussian the mask
  mask_pixels = mask.data();
  fast_gaussian_blur(mask_pixels, mask_out, src_w, src_h, sigma_segmask);
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
