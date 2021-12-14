#pragma once

#include <chrono>
#include <mutex>
#include <thread>

#include "tensorflow.hpp"

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
  std::string bg_file = "backgrounds/bg.ayuv";
  std::map<segmentation_model, model_meta_info> models;
  segmentation_mode mode;
  segmentation_model model_selected;
  model_meta_info model;
  const char *in_filename = nullptr;
  const char *out_filename = nullptr;
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

public:
  program(int argc, char **argv);

  int run();
  void load_tensorflow_model();
  void process_frame(AVPacket &pkt_copy);
  void fill_input_tensor(const AVPacket &pkt_copy);
  void upscale_segregation_mask(const AVPacket &pkt_copy);
  void blur_yuv();
  void set_virtual_background_source();
  void blur_virtual_background_itself();
};
