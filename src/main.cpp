#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include "Console.hpp"
#include "ffmpeg_headers.hpp"
#include "math.hpp"
#include "program.h"
#include "snowflake.h"

#include <signal.h>
#include <stdio.h>

extern double tt;  // snowflake.cpp

namespace cr = CppReadline;
using ret = cr::Console::ReturnCode;

program::program(int argc, char **argv)
    : models({
          {google_meet_full, {"models/segm_full_v679.tflite", 256, 144}},
          {google_meet_lite, {"models/segm_lite_v681.tflite", 160, 96}},
          {mlkit, {"models/selfiesegmentation_mlkit-256x256-2021_01_19-v1215.f16.tflite", 256, 256}},
      }) {}

program::~program() {
  stop({});
}

unsigned program::list_cams(const std::vector<std::string> &input) {
  const auto usage = [=]() {
    std::cout << "Usage: " << input[0] << "\n";
  };
  if (input.size() != 1) {
    usage();
    return 1;
  }
  Process process("make probe 2>&1", "", [](const char *bytes, size_t n) {
    std::cout << "Found: " << std::string(bytes, n);
  });
  std::cout << "Current camera: " << camera_device << std::endl;
  auto exit_status = process.get_exit_status();
  std::cout << "Exit: " << exit_status << std::endl;
  return 0;
}

unsigned program::set_cam(const std::vector<std::string> &input) {
  const auto usage = [=]() {
    std::cout << "Usage: " << input[0] << " < input >\n";
    std::cout << "  e.g. " << input[0] << " /dev/video0\n";
  };
  if (input.size() != 2) {
    usage();
    return 1;
  }
  camera_device = input[1];
  std::cout << "Selected camera: " << camera_device << std::endl;
  return 0;
}

unsigned program::set_mode(const std::vector<std::string> &input) {
  const auto usage = [=]() {
    std::cout << "Usage: " << input[0] << " < mode >\n";
    std::cout << "Valid modes:\n";
    std::cout << "- normal\n";
    std::cout << "- white\n";
    std::cout << "- black\n";
    std::cout << "- blur\n";
    std::cout << "- virtual\n";
    std::cout << "- animated\n";
    std::cout << "- snowflakes" << std::endl;
    std::cout << "- snowflakesblur" << std::endl;
    std::cout << "- external <image_path>" << std::endl;
  };
  if (input.size() > 1 && input[1] != "external" || input.size() < 2) {
    if (input.size() != 2) {
      usage();
      return 1;
    }
  }

  animate = false;
  if (input[1] == "normal") {
    mode = segmentation_mode::bypass;
  } else if (input[1] == "white") {
    mode = segmentation_mode::white_background;
  } else if (input[1] == "black") {
    mode = segmentation_mode::black_background;
  } else if (input[1] == "blur") {
    mode = segmentation_mode::blur_background;
  } else if (input[1] == "virtual") {
    mode = segmentation_mode::virtual_background;
  } else if (input[1] == "animated") {
    load_spaceship_frames_into_memory(true);
    mode = segmentation_mode::virtual_background;
    animate = true;
  } else if (input[1] == "snowflakes") {
    mode = segmentation_mode::snowflakes;
  } else if (input[1] == "snowflakesblur") {
    mode = segmentation_mode::snowflakes_blur;
  } else if (input[1] == "external") {
    return set_background(input);
  } else {
    usage();
    return 1;
  }
  return 0;
}

unsigned program::preview(const std::vector<std::string> &input) {
  const auto usage = [=]() {
    std::cout << "Usage: " << input[0] << "\n";
  };
  if (input.size() != 1) {
    usage();
    return 1;
  }
  std::thread background([]() {
    Process process("ffplay /dev/video9 2>&1", "", [](const char *bytes, size_t n) {});
    auto exit_status = process.get_exit_status();
    std::cout << "Preview window exited: " << exit_status << std::endl;
  });
  background.detach();
  return 0;
}

unsigned program::set_model(const std::vector<std::string> &input) {
  model_selected = segmentation_model::google_meet_full;
  return 0;
}

unsigned program::start(const std::vector<std::string> &input) {
  process_runner_ = std::thread([&]() {
    started = true;
    // we will now assume these loopback devices are already present
    // handled via wrapper-script, since `sudo` might prompt for user interaction
    // this shell is not compatible with that right now..
    //  std::stringstream ss;
    //  ss << "sudo modprobe -r v4l2loopback; ";
    //  ss << "sudo modprobe v4l2loopback video_nr=8,9 exclusive_caps=0,1 card_label=\"Virtual Temp Camera "
    //        "Input\",\"Virtual 640x480 420P TFlite Camera\"; "e

    // // handle first part synchronously
    // Process process(ss.str(), "", [](const char *bytes, size_t n) {});
    // auto exit_status = process.get_exit_status();:

    // ss.str("");e
    // ss.clear();

    //  worked for ideapad: ss << "/usr/bin/ffmpeg -i " << camera_device
    //  check: sudo v4l2-ctl -d /dev/video0 --all
    //  check: sudo v4l2-ctl -d /dev/video0 --list-formats-ex
    std::stringstream ss;
    ss << "/usr/bin/ffmpeg -pix_fmt mjpeg -i " << camera_device
       << " -f v4l2 -input_format mjpeg -framerate 10 -video_size 1024x680 -vf "
          "scale=640:480:force_original_aspect_ratio=increase,crop=640:480 -pix_fmt yuv420p -f v4l2 /dev/video8 2>&1";

    process_.reset(new Process(
        ss.str(),
        "",
        [](const char *bytes, size_t n) {
          // std::cout << "Output from stdout: " << std::string(bytes, n);
        },
        nullptr,
        true));

    auto exit_status = process_->get_exit_status();
    std::cout << "Exit: " << exit_status << std::endl;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  if (const char *env_p = std::getenv("BG")) {
    bg_file = std::string(env_p);
  }
  model = models[model_selected];

  // Load a background to test
  // TODO: Implement loading the file path from a configuration file to increase the flexibility of the program
  if (mode != segmentation_mode::external_background) {
    load(bg, bg_file);
  }

  load_spaceship_frames_into_memory();

  runner_ = std::thread([&]() {
    stop_ = false;
    run();
  });

  return 0;
}

unsigned program::stop(const std::vector<std::string> &input) {
  std::cout << "stopping..." << std::endl;
  if (started) {
    process_->kill();
    if (process_runner_.joinable()) process_runner_.join();
    started = false;
  }
  if (!stop_) {
    std::cout << "stopping 2..." << std::endl;
    stop_ = true;
    if (runner_.joinable()) runner_.join();
  }
  return 0;
}

unsigned program::set_background(const std::vector<std::string> &input) {
  bg.clear();
  animate = false;
  if (input.size() != 3) {
    std::cout << "Usage: set-mode external <image_file_path>" << std::endl;
    return 1;
  }

  std::string background_file_path = input[2];
  std::filesystem::path filePath(background_file_path);
  std::string extension = filePath.extension().string();

  std::set<std::string> supportedImageFormats = {".png", ".jpeg", ".jpg"};
  if (extension == ".ayuv") {
    if (int errorcode = load(bg, input[2]) != 0) {
      return errorcode;
    }
  } else if (supportedImageFormats.find(extension) != supportedImageFormats.end()) {
    // Handle image file
    std::ifstream file(background_file_path);
    if (!file.good()) {
      std::cerr << "Error opening file: " << background_file_path << std::endl;
      return 1;
    }

    // Process image file using processImage function
    try {
      std::vector<uint8_t> imageData = processImage(background_file_path);
      convertRGBtoAYUV(imageData, bg);
    } catch (const std::exception &e) {
      std::cerr << "Error processing image file: " << e.what() << std::endl;
      return 1;
    }
  } else {
    std::cerr << "Unsupported image format. The supported image formats are: .png, .jpeg, .jpg, and .ayuv."
              << std::endl;
    return 1;
  }
  mode = segmentation_mode::external_background;
  return 0;
}

void program::start_console() {
  cr::Console c("cam> ");

  c.registerCommand("list-cams", std::bind(&program::list_cams, this, std::placeholders::_1));
  c.registerCommand("set-cam", std::bind(&program::set_cam, this, std::placeholders::_1));
  c.registerCommand("set-mode", std::bind(&program::set_mode, this, std::placeholders::_1));
  c.registerCommand("set-model", std::bind(&program::set_model, this, std::placeholders::_1));
  c.registerCommand("start", std::bind(&program::start, this, std::placeholders::_1));
  c.registerCommand("stop", std::bind(&program::stop, this, std::placeholders::_1));
  c.registerCommand("preview", std::bind(&program::preview, this, std::placeholders::_1));
  c.executeCommand("help");

  int retCode;
  do {
    retCode = c.readLine();
    if (retCode == ret::Ok)
      c.setGreeting("cam> ");
    else
      c.setGreeting("cam!> ");

    if (retCode != 0) {
      std::cout << "Received error code " << retCode << std::endl;
    }
  } while (retCode != ret::Quit);
}

int program::run() {
  load_tensorflow_model();

  // The rest of the run function is based on the remuxing.c example provided by ffmpeg

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
  if ((ret = avformat_open_input(&ifmt_ctx, in_filename.c_str(), input_format, 0)) < 0) {
    fprintf(stderr, "Could not open input file '%s'", in_filename.c_str());
    goto end;
  }

  if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
    fprintf(stderr, "Failed to retrieve input stream information");
    goto end;
  }

  // Specify v4l2 as the output format (cannot be detected from filename /dev/videoX)
  avformat_alloc_output_context2(&ofmt_ctx, NULL, "v4l2", out_filename.c_str());
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
  av_dump_format(ofmt_ctx, 0, out_filename.c_str(), 1);

  if (!(ofmt->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx->pb, out_filename.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
      fprintf(stderr, "Could not open output file '%s'", out_filename.c_str());
      goto end;
    }
  }

  ret = avformat_write_header(ofmt_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "Error occurred when opening output file\n");
    goto end;
  }

  while (!stop_) {
    reset();

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
    // log_packet(ofmt_ctx, &pkt, "out");

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
    // std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
  // Load model
  tflite_model =
      std::unique_ptr<tflite::FlatBufferModel>(tflite::FlatBufferModel::BuildFromFile(model.filename.c_str()));
  if (!tflite_model) {
    printf("Failed to model\n");
    exit(0);
  } else {
    printf("Loaded model\n");
  }

  resolver = std::make_unique<tflite::ops::builtin::BuiltinOpResolver>();
  // Custom op for Google Meet network
  resolver->AddCustom("Convolution2DTransposeBias", mediapipe::tflite_operations::RegisterConvolution2DTransposeBias());
  builder.reset(new tflite::InterpreterBuilder(*tflite_model, *resolver));
  (*builder)(&interpreter);

  // Resize input tensors, if desired.
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    fprintf(stderr, "Something wrong");
    exit(1);
  }
}

void program::reset() {
  mask.clear();
  background_y.clear();
  background_u.clear();
  background_v.clear();
}

void program::process_frame(AVPacket &pkt_copy) {
  // std::vector<float> pixels;

  // Fill input tensor with RGB values
  fill_input_tensor(pkt_copy);

  // Run inference
  interpreter->Invoke();

  // Upscale resulting segregation mask
  upscale_segregation_mask(pkt_copy);

  // We need two copies of the mask for blurring
  blur_yuv();

  set_virtual_background_source();

  blur_virtual_background_itself();

  // blur the mask as well, since we scaled it up
  fast_gaussian_blur(mask_pixels, mask_out, src_w, src_h, sigma_segmask);

  draw_snowflakes(pkt_copy);

  float *bg_y = background_y.data();
  float *bg_u = background_u.data();
  float *bg_v = background_v.data();

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
        case white_background:
          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(0xFF * bg_alpha_ratio);
          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(0x80 * bg_alpha_ratio);
          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(0x80 * bg_alpha_ratio);
          break;
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
        case snowflakes: {
          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(*bg_y++ * bg_alpha_ratio);
          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(*bg_u++ * bg_alpha_ratio);
          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(*bg_v++ * bg_alpha_ratio);
          break;
        }
        case snowflakes_blur: {
          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(255 * *bg_y++ * bg_alpha_ratio);
          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(255 * *bg_u++ * bg_alpha_ratio);
          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(255 * *bg_v++ * bg_alpha_ratio);
          break;
        }
        case external_background: {
          vbg++;  // skip alpha channel
          pkt_copy.data[index_Y] = (pkt_copy.data[index_Y] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
          pkt_copy.data[index_U] = (pkt_copy.data[index_U] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
          pkt_copy.data[index_V] = (pkt_copy.data[index_V] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
          break;
         }
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
  if (mode != segmentation_mode::snowflakes) {
    blur_channel(background_y);
    blur_channel(background_u);
    blur_channel(background_v);
  }
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
  const float inv_width = 1.0f / model.width;
  const float inv_height = 1.0f / model.height;

  const float kRedCoeff = 1.402f;
  const float kGreenCoeff1 = 0.344f;
  const float kGreenCoeff2 = 0.714f;
  const float kBlueCoeff = 1.772f;

  for (int y = 0; y < model.height; y++) {
    const int src_y = static_cast<int>(y * inv_height * src_h);
    const int src_y_div_2 = src_y / 2;

    for (int x = 0; x < model.width; x++) {
      const int src_x = static_cast<int>(x * inv_width * src_w);
      const int src_x_div_2 = src_x / 2;

      const int index_Y = src_y * src_w + src_x;
      const int index_U = (src_w * src_h) + (src_y_div_2) * (src_w / 2) + src_x_div_2;
      const int index_V = static_cast<int>((src_w * src_h) * 1.25 + (src_y_div_2) * (src_w / 2) + src_x_div_2);

      const int Y = pkt_copy.data[index_Y];
      const int U = pkt_copy.data[index_U];
      const int V = pkt_copy.data[index_V];

      const int R = Y + static_cast<int>(kRedCoeff * (V - 128));
      const int G = Y - static_cast<int>(kGreenCoeff1 * (U - 128) + kGreenCoeff2 * (V - 128));
      const int B = Y + static_cast<int>(kBlueCoeff * (U - 128));

      *input++ = static_cast<float>(R) / 255.0f;
      *input++ = static_cast<float>(G) / 255.0f;
      *input++ = static_cast<float>(B) / 255.0f;
    }
  }
}

void program::draw_snowflakes(AVPacket &pkt_copy) {
  if (mode != segmentation_mode::snowflakes && mode != segmentation_mode::snowflakes_blur) {
    return;
  }

  // initialize 500 flakes
  static std::vector<snowflake> flakes;
  if (flakes.empty()) {
    for (int i = 0; i < 500; i++) {
      flakes.push_back(snowflake{});
    }
  }

  // update snowflake positions and global time
  for (auto &snowflake : flakes) {
    snowflake.update();
  }
  tt += 0.0004;

  // draw each snowflake
  size_t index = 0;
  float *bg_y = background_y.data();
  float *bg_u = background_u.data();
  float *bg_v = background_v.data();

  const auto yuv_to_rgb = [](auto Y, auto U, auto V, auto &R, auto &G, auto &B) {
    B = 1.164 * (Y - 16) + 2.018 * (U - 128);
    G = 1.164 * (Y - 16) - 0.813 * (V - 128) - 0.391 * (U - 128);
    R = 1.164 * (Y - 16) + 1.596 * (V - 128);
  };

  const auto rgb_to_yuv = [](auto R, auto G, auto B, auto &Y, auto &U, auto &V) {
    Y = (0.257 * R) + (0.504 * G) + (0.098 * B) + 16;
    U = -(0.148 * R) - (0.291 * G) + (0.439 * B) + 128;
    V = (0.439 * R) - (0.368 * G) - (0.071 * B) + 128;
  };

  for (auto &snowflake : flakes) {
    auto flake_x = std::clamp(int(snowflake.x - snowflake.radiussize) - 1, 0, 640 - 1);
    auto flake_y = std::clamp(int(snowflake.y - snowflake.radiussize) - 1, 0, 480 - 1);
    auto flake_x_end = std::clamp(int(flake_x + (snowflake.radiussize * 2.5) + 2), 0, 640 - 1);
    auto flake_y_end = std::clamp(int(flake_y + (snowflake.radiussize * 2.5) + 2), 0, 480 - 1);

    for (int y = flake_y; y < flake_y_end; y++) {
      for (int x = flake_x; x < flake_x_end; x++) {
        int index_Y = y * src_w + x;
        int index_U = (src_w * src_h) + (y / 2) * (src_w / 2) + x / 2;
        int index_V = (int)((src_w * src_h) * 1.25 + (y / 2) * (src_w / 2) + x / 2);

        // pointers to Y U V
        float *pY = bg_y + (y * src_w) + x, *pU = bg_u + (y * src_w) + x, *pV = bg_v + (y * src_w) + x;

        // convert 0-1 to 0-255
        double Y = *pY * 255., U = *pU * 255., V = *pV * 255.;

        // convert YUV to RGB
        double R = 0, G = 0, B = 0;
        yuv_to_rgb(Y, U, V, R, G, B);

        // calculate pixel value of snowflake
        auto dist = index % 3 == 0 ? get_distance_approx(double(x), double(y), snowflake.x, snowflake.y)
                                   : get_distance(double(x), double(y), snowflake.x, snowflake.y);
        auto color_alpha = 1. - (snowflake.expf(dist / snowflake.radiussize, 1000));
        color_alpha *= snowflake.opacity;

        // update function to blend the pixel with background
        const auto update_snowflake_pixel = [&]() {
          auto bg_a = 1.;
          const auto a = color_alpha + (bg_a * (1. - color_alpha) / 1.);
          R = 255. * ((1. * color_alpha + (R / 255.) * bg_a * (1. - color_alpha) / 1.) / a);
          G = 255. * ((1. * color_alpha + (G / 255.) * bg_a * (1. - color_alpha) / 1.) / a);
          B = 255. * ((1. * color_alpha + (B / 255.) * bg_a * (1. - color_alpha) / 1.) / a);
        };
        // draw the pixel if it was within the snowflake
        if (dist < snowflake.radiussize) update_snowflake_pixel();

        // convert RGB to YUV
        rgb_to_yuv(R, G, B, Y, U, V);

        // commit pixel (normalized to 0-1 again)
        *pY = (Y / 255.), *pU = (U / 255.), *pV = (V / 255.);

        // now process the foreground canvas as RGB (already 0-255 values)
        yuv_to_rgb(pkt_copy.data[index_Y], pkt_copy.data[index_U], pkt_copy.data[index_V], R, G, B);

        // draw only large snowflakes (> 5.5) on top of the person!
        if (dist < snowflake.radiussize && snowflake.radiussize > 5.5) update_snowflake_pixel();

        // commit pixel (normalizing not needed)
        rgb_to_yuv(R, G, B, pkt_copy.data[index_Y], pkt_copy.data[index_U], pkt_copy.data[index_V]);
      }
    }
    index++;
  }
}

int program::load(std::vector<uint8_t> &bg, const std::string &bg_file) {
  std::ifstream file(bg_file, std::ios::binary | std::ios::ate);
  if (!file.good()) {
    std::cout << "Warning: Could not open input file: " << bg_file << std::endl;
    return 1;
  }
  std::streamsize size = file.tellg();
  if (size == std::streamsize(0)) {
    throw std::runtime_error("File is empty: " + bg_file);
  }
  file.seekg(0, std::ios::beg);
  bg.resize(size);
  if (!file.read(reinterpret_cast<char *>(bg.data()), size)) {
    std::cout << "Warning: couldn't read file: " << bg_file;
    return 1;
  }
  return 0;
};

void program::convertRGBtoAYUV(const std::vector<uint8_t> &input, std::vector<uint8_t> &output) {
  size_t size = input.size();
  output.resize(size);

  for (size_t i = 0; i < size; i += 4) {
    uint8_t r = static_cast<uint8_t>(input[i]);
    uint8_t g = static_cast<uint8_t>(input[i + 1]);
    uint8_t b = static_cast<uint8_t>(input[i + 2]);
    uint8_t a = static_cast<uint8_t>(input[i + 3]);

    uint8_t y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
    uint8_t u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    uint8_t v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
    uint8_t a2 = a;

    output[i] = a2;
    output[i + 1] = y;
    output[i + 2] = u;
    output[i + 3] = v;
  }
}
std::vector<uint8_t> program::processImage(const std::string &imagePath) {
  av_register_all();

  AVFormatContext *formatContext = nullptr;
  if (avformat_open_input(&formatContext, imagePath.c_str(), nullptr, nullptr) != 0) {
    throw std::runtime_error("Error opening file: " + imagePath);
  }

  AVCodec *codec = nullptr;
  AVCodecParameters *codecParameters = nullptr;
  int streamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
  if (streamIndex < 0 || !formatContext->streams[streamIndex]->codecpar) {
    avformat_close_input(&formatContext);
    throw std::runtime_error("No video stream found in file: " + imagePath);
  }
  codecParameters = formatContext->streams[streamIndex]->codecpar;

  AVCodecContext *codecContext = avcodec_alloc_context3(codec);
  if (avcodec_parameters_to_context(codecContext, codecParameters) < 0) {
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    throw std::runtime_error("Failed to copy codec parameters");
  }
  if (avcodec_open2(codecContext, codec, nullptr) < 0) {
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    throw std::runtime_error("Failed to open codec");
  }

  AVPacket packet;
  av_init_packet(&packet);

  if (av_read_frame(formatContext, &packet) < 0) {
    av_packet_unref(&packet);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    throw std::runtime_error("Failed to read frame from file: " + imagePath);
  }

  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    av_packet_unref(&packet);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    throw std::runtime_error("Failed to allocate frame");
  }

  int response = avcodec_send_packet(codecContext, &packet);
  if (response < 0 && response != AVERROR(EAGAIN) && response != AVERROR_EOF) {
    av_packet_unref(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    throw std::runtime_error("Error while sending a packet to the decoder");
  }

  response = avcodec_receive_frame(codecContext, frame);
  if (response < 0 && response != AVERROR_EOF) {
    av_packet_unref(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    throw std::runtime_error("Error while receiving a frame from the decoder");
  }

  AVFrame *rgbaFrame = av_frame_alloc();
  if (!rgbaFrame) {
    av_packet_unref(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    throw std::runtime_error("Failed to allocate frame for RGBA conversion");
  }
  int destWidth = 640;
  int destHeight = 480;

  // Create SwsContext for scaling and conversion
  struct SwsContext *swsContext = sws_getContext(codecContext->width,
                                                 codecContext->height,
                                                 codecContext->pix_fmt,
                                                 destWidth,
                                                 destHeight,
                                                 AV_PIX_FMT_RGBA,
                                                 SWS_BILINEAR,
                                                 nullptr,
                                                 nullptr,
                                                 nullptr);
  if (!swsContext) {
    av_packet_unref(&packet);
    av_frame_free(&frame);
    av_frame_free(&rgbaFrame);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    throw std::runtime_error("Failed to create SwsContext for RGBA conversion");
  }
  int bufferSize = destWidth * destHeight * 4;  // Assuming RGBA format (4 bytes per pixel)
  std::vector<uint8_t> buffer(bufferSize);

  // Initialize destination pointers for conversion
  uint8_t *destData[4] = {buffer.data(), nullptr, nullptr, nullptr};
  int destLinesize[4] = {destWidth * 4, 0, 0, 0};

  sws_scale(swsContext, frame->data, frame->linesize, 0, codecContext->height, destData, destLinesize);

  std::memcpy(buffer.data(), destData[0], bufferSize);

  std::vector<uint8_t> imageData(destData[0], destData[0] + bufferSize);

  sws_freeContext(swsContext);
  av_packet_unref(&packet);
  av_frame_free(&frame);
  av_frame_free(&rgbaFrame);
  avcodec_free_context(&codecContext);
  avformat_close_input(&formatContext);

  return imageData;
}

void program::load_spaceship_frames_into_memory(bool force) {
  if ((animate || force) && anim_bg.empty()) {
    anim_bg.reserve(750);
    std::stringstream ss;
    for (size_t i = 0; i < 750; i++) {
      ss << "backgrounds/spaceship/" << i << ".ayuv";
      load(anim_bg[i], ss.str());
      ss.str("");
      ss.clear();
    }
    std::cout << "pre-loaded spaceship background frames into memory." << std::endl;
  }
}

// workaround for signal();
program *global_program = nullptr;

int main(int argc, char **argv) {
  std::cout << R"(
art by: Marcin Glinski          _
                               / \
                              / .'_
                             / __| \
             `.             | / (-' |
           `.  \_..._       :  (_,-/
         `-. `,'     `-.   /`-.__,'
            `/ __       \ /     /
            /`/  \       :'    /
          _,\o\_o/       /    /
         (_) ___.--.    /    /
          `-. -._.i \.      :
             `.\  ( |:.     |
            ,' )`-' |:..   / \
   __     ,'   |    `.:.      `.
  (_ `---:     )      \:.       \
   ,'     `. .'\       \:.       )
 ,' ,'     ,'  \\ o    |:.      /
(_,'  ,7  /     \`.__.':..     /,,,
  (_,'(_,'   _gdMbp,,dp,,,,,,dMMMMMbp,,
          ,dMMMMMMMMMMMMMMMMMMMMMMMMMMMb,
       .dMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMb,  fsc
     .dMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,
    ,MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
   dMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM.
 .dMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMb
 V I R T U A L    B A C K G R O U N D   W E B C A M
)" << std::endl;  // source: https://www.asciiart.eu/cartoons/other
  program prog(argc, argv);
  global_program = &prog;

  signal(SIGINT, [](int a) {
    printf("^C caught\n");
    if (global_program != nullptr) {
      global_program->stop({});
      std::exit(0);
    }
  });

  std::filesystem::path currentPath = std::filesystem::current_path();
  if (currentPath.filename() == "src") {
    std::filesystem::path parentPath = currentPath.parent_path();
    std::filesystem::current_path(parentPath);
  }

  prog.start_console();
}
