#pragma once

#include <chrono>
#include <mutex>
#include <thread>

#include "process.hpp"
#include "tensorflow.hpp"

using namespace TinyProcessLib;

// Implemented in blur_float.cpp
extern void fast_gaussian_blur(float *&in, float *&out, int w, int h, float sigma);

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
  white_background,
  black_background,
  blur_background,
  virtual_background,
  virtual_background_blurred,
  snowflakes,
  snowflakes_blur,
};

class program {
private:
  std::thread runner_;
  bool stop_ = false;

  std::unique_ptr<Process> process_;
  std::thread process_runner_;

  std::unique_ptr<tflite::FlatBufferModel> tflite_model;
  std::unique_ptr<tflite::ops::builtin::BuiltinOpResolver> resolver;
  std::unique_ptr<tflite::InterpreterBuilder> builder;

  // float sigma_bg_blur = 4.;
  // float sigma_bg_blur = 8.;
  float sigma_bg_blur = 6.;
  float sigma_segmask = 1.2;
  int src_w = 640;
  int src_h = 480;
  std::string bg_file = "backgrounds/bg.ayuv";
  std::map<segmentation_model, model_meta_info> models;
  segmentation_mode mode = segmentation_mode::virtual_background;
  segmentation_model model_selected = segmentation_model::google_meet_full;
  model_meta_info model;
  // TODO: make these strings, and configurable.
  std::string camera_device = "/dev/video0";
  std::string in_filename = "/dev/video8";
  std::string out_filename = "/dev/video9";
  bool animate = true;

  std::unique_ptr<tflite::Interpreter> interpreter;
  std::vector<uint8_t> bg;
  std::vector<std::vector<uint8_t>> anim_bg;

  std::vector<float> mask;
  std::vector<float> mask2;
  std::vector<float> background_y, background_u, background_v;

  float *mask_pixels = nullptr;
  float *mask_out = nullptr;
  float *mask_out_2 = nullptr;

  uint8_t *vbg = nullptr;
  bool started = false;

public:
  program(int argc, char **argv);
  ~program();

  void start_console();
  int run();
  unsigned list_cams(const std::vector<std::string> &input);
  unsigned set_cam(const std::vector<std::string> &input);
  unsigned set_mode(const std::vector<std::string> &input);
  unsigned set_model(const std::vector<std::string> &input);
  unsigned start(const std::vector<std::string> &input);
  unsigned stop(const std::vector<std::string> &input);
  unsigned preview(const std::vector<std::string> &input);

  int load(std::vector<uint8_t> &bg, const std::string &bg_file);
  void load_spaceship_frames_into_memory(bool force = false);
  void load_tensorflow_model();
  void reset();
  void process_frame(AVPacket &pkt_copy);
  void fill_input_tensor(const AVPacket &pkt_copy);
  void upscale_segregation_mask(const AVPacket &pkt_copy);
  void blur_yuv();
  void set_virtual_background_source();
  void blur_virtual_background_itself();
  void draw_snowflakes(AVPacket &pkt_copy);
};
