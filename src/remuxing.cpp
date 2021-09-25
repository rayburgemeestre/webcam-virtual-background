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

#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
}

#include <iostream>
#include <string>

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


static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
    std::cout << "size: " << pkt->size << std::endl;
}

#include "tensorflow/lite/model.h"
#include "tensorflow/lite/builtin_op_data.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/string_util.h"
#include "tensorflow/lite/kernels/register.h"

// Taken from mediapipe project
#include "transpose_conv_bias.h"

// Implemented in blur_float.cpp
extern void fast_gaussian_blur(float *& in, float *& out, int w, int h, float sigma);

#include <fstream>

int main(int argc, char **argv)
{
    float sigma_bg_blur = 5.;
    float sigma_segmask = 3.;

    // Load a background to test
    std::ifstream file("bg.ayuv", std::ios::binary | std::ios::ate);
    if (!file.good()) {
        return 1;
    }
    std::streamsize size = file.tellg();
    if (size == std::streamsize(0)) {
        return 1;
    }
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bg(size);
    if (!file.read((char *)bg.data(), size)) {
        return 1;
    }

    // Some hardcoded configuration
    const auto model_file = "models/segm_full_v679.tflite";
    // const auto file = "models/selfiesegmentation_mlkit-256x256-2021_01_19-v1215.f16.tflite";
    const int src_w = 640, src_h = 480;
    // const int dst_w = 256, dst_h = 256;
    // const int dst_w = 160, dst_h = 96;
    const int dst_w = 256, dst_h = 144;

    // Defined outside of the loop to avoid memory reallocations
    std::vector<float> mask;
    std::vector<float> background_y;

    // Load model
    std::unique_ptr<tflite::FlatBufferModel> model(tflite::FlatBufferModel::BuildFromFile(model_file));
    if (!model) {
        printf("Failed to model\n");
        exit(0);
    } else {
        printf("Loaded model\n");
    }

    tflite::ops::builtin::BuiltinOpResolver resolver;
	// Custom op for Google Meet network
	resolver.AddCustom("Convolution2DTransposeBias", mediapipe::tflite_operations::RegisterConvolution2DTransposeBias());
    std::unique_ptr<tflite::Interpreter> interpreter;
	tflite::InterpreterBuilder builder(*model, resolver);
	builder(&interpreter);

    // Resize input tensors, if desired.
    if (interpreter->AllocateTensors() != kTfLiteOk) {
     fprintf(stderr, "Something wrong");
     exit(1);
    }

    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    const char *in_filename, *out_filename;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = NULL;
    int stream_mapping_size = 0;

    if (argc < 4) {
        printf("usage: %s input output mode\n"
               "API example program to remux a media file with libavformat and libavcodec.\n"
               "The output format is guessed according to the file extension.\n"
               "mode can be: 1 (black bg), 2 (blur bg), 3 (vbg), 4 (vbg+blur)\n"
               "\n", argv[0]);
        return 1;
    }

    in_filename  = argv[1];
    out_filename = argv[2];
    enum segmentation_mode {
        black_background = 1,
        blur_background,
        virtual_background,
        virtual_background_blurred
    };
    const auto mode = (segmentation_mode)std::stoi(argv[3]);

    // Needed for detecting v4l2
    avdevice_register_all();

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

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
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
        out_stream->codecpar->codec_tag = 0;
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

        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;

        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        log_packet(ifmt_ctx, &pkt, "in");

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        log_packet(ofmt_ctx, &pkt, "out");

        std::vector<float> pixels;

        // Fill input tensor with RGB values
        auto *input = interpreter->typed_tensor<float>(0);
        for (int y=0; y<dst_h; y++) {
            const float ratio_1 = y / float(dst_h);
            for (int x=0; x<dst_w; x++) {
                const float ratio_2 = x / float(dst_w);
                int src_y = ratio_1 * src_h;
                int src_x = ratio_2 * src_w;

                int index_Y = src_y * src_w + src_x;
                int index_U = ((src_w * src_h) + (src_y / 2) * (src_w / 2) + src_x / 2);
                int index_V = (int) ((src_w * src_h) * 1.25 + (src_y / 2) * (src_w / 2) + src_x / 2);

                int Y = pkt.data[index_Y];
                int U = pkt.data[index_U];
                int V = pkt.data[index_V];

                int R = (int) (Y + 1.402f * (V - 128));
                int G = (int) (Y - 0.344f * (U - 128) - 0.714f * (V - 128));
                int B = (int) (Y + 1.772f * (U - 128));

                *input++ = float(R / 255.0);
                *input++ = float(G / 255.0);
                *input++ = float(B / 255.0);
            }
        }

        // Run inference
        interpreter->Invoke();

        // Upscale resulting segregation mask
        float* output = interpreter->typed_output_tensor<float>(0);
        for (int y=0; y<src_h; y++) {
            for (int x=0; x<src_w; x++) {
                const int src_y = y * (dst_h / float(src_h));
                const int src_x = x * (dst_w / float(src_w));
                const auto src_offset = (src_y * dst_w) + src_x;

                // Index into the right location in output
                const auto bg = *(output + src_offset * 2);
                const auto person = *(output + src_offset * 2 + 1);
                const auto shift = std::max(bg, person);
                const auto backgroundExp = exp(bg - shift);
                const auto personExp = exp(person - shift);
                // Sets only the alpha component of each pixel
                const auto val = (255 * personExp) / (backgroundExp + personExp); // softmax
                mask.push_back(float(val) / 255.0f);

                // Background Y for blurring
                int index_Y = y * src_w + x;
                background_y.push_back(pkt.data[index_Y] / 255.);
            }
        }

        // We need two copies of the mask for blurring
        auto mask2 = mask;
        float *mask_pixels = mask.data();
        float *mask_out = mask2.data();

        // We need two copies for blurring the bg as well
        auto background_y2 = background_y;
        float *bg_y = background_y.data();
        float *bg_y2 = background_y2.data();

        // blurring Y channel is sufficient
        fast_gaussian_blur(bg_y, bg_y2, src_w, src_h, sigma_bg_blur);

        // blurring virtual background
        auto bgv = bg;
        if (mode == virtual_background_blurred) {
            std::vector<float> bg1, bg2;
            auto vb = bgv.begin();
            for (int i=0; i<(src_w * src_h); i++) {
                vb++;
                bg1.push_back(*vb / 255.);
                bg2.push_back(*vb / 255.);
                vb++;
                vb++;
                vb++;
            }
            float *bg1f = (float *)bg1.data();
            float *bg2f = (float *)bg2.data();
            // TODO: for better results we should actually blur also the U and V channels.
            fast_gaussian_blur(bg1f, bg2f, src_w, src_h, sigma_bg_blur);

            // read back
            vb = bgv.begin();
            for (const float b: bg1) {
                vb++;
                *vb++ = b * 255.;
                vb++;
                vb++;
            }
        }

        // blur the mask as well, since we scaled it up
        fast_gaussian_blur(mask_pixels, mask_out, src_w, src_h, sigma_segmask);

        auto *vbg = mode == virtual_background_blurred ? bgv.data() : bg.data();
        std::cout << "a: " << bgv.size() << " vs " << bg.size() << std::endl;
        for (int y=0; y<src_h; y++) {
            for (int x=0; x<src_w; x++) {
                int index_Y = y * src_w + x;
                int index_U = (src_w * src_h) + (y / 2) * (src_w / 2) + x / 2;
                int index_V = (int) ((src_w * src_h) * 1.25 + (y / 2) * (src_w / 2) + x / 2);

                float mask_alpha_ratio = *mask_out;
                const auto val = mask_alpha_ratio * 255.f;
                mask_out++;
                float bg_alpha_ratio = 1.0f - mask_alpha_ratio;

                // blend person on top of background using mask
                switch (mode) {
                    case black_background:
                        pkt.data[index_Y] = (pkt.data[index_Y] * mask_alpha_ratio) + float(0x00 * bg_alpha_ratio);
                        pkt.data[index_U] = (pkt.data[index_U] * mask_alpha_ratio) + float(0x80 * bg_alpha_ratio);
                        pkt.data[index_V] = (pkt.data[index_V] * mask_alpha_ratio) + float(0x80 * bg_alpha_ratio);
                        break;
                    case blur_background:
                        pkt.data[index_Y] = (pkt.data[index_Y] * mask_alpha_ratio) + float(255 * *bg_y++ * bg_alpha_ratio);
                        pkt.data[index_U] = (pkt.data[index_U] * mask_alpha_ratio) + float(pkt.data[index_U] * bg_alpha_ratio);
                        pkt.data[index_V] = (pkt.data[index_V] * mask_alpha_ratio) + float(pkt.data[index_V] * bg_alpha_ratio);
                        break;
                    case virtual_background:
                        vbg++; // skip alpha channel
                        pkt.data[index_Y] = (pkt.data[index_Y] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
                        pkt.data[index_U] = (pkt.data[index_U] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
                        pkt.data[index_V] = (pkt.data[index_V] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
                        break;
                    case virtual_background_blurred:
                        vbg++; // skip alpha channel
                        pkt.data[index_Y] = (pkt.data[index_Y] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
                        pkt.data[index_U] = (pkt.data[index_U] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
                        pkt.data[index_V] = (pkt.data[index_V] * mask_alpha_ratio) + float(*vbg++ * bg_alpha_ratio);
                        break;
                }
            }
        }

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmt_ctx);
end:

    avformat_close_input(&ifmt_ctx);

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    av_freep(&stream_mapping);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}
