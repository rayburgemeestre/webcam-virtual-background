#define __STDC_CONSTANT_MACROS

extern "C" {
#define __STDC_CONSTANT_MACROS

#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavformat/internal.h>
#include <libavutil/imgutils.h>
#include <libavutil/parseutils.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <iostream>
#include <string>

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

class program {
private:
  const char *in_filename = nullptr;
  const char *out_filename = nullptr;
  bool animate = false;

public:
  program(int argc, char **argv);

  int run();
};

program::program(int argc, char **argv) {
  in_filename = "/dev/video0";
  out_filename = "/dev/video9";
}

int program::run() {
  AVOutputFormat *ofmt = NULL;
  AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
  AVPacket pkt;
  AVPacket pkt_out;
  struct SwsContext *sws_ctx = nullptr;
  int ret, i;
  int stream_index = 0;
  int *stream_mapping = NULL;
  int stream_mapping_size = 0;
  int src_w = 0;
  int src_h = 0;

  AVCodec *input_codec = NULL;
  AVCodec *output_codec = NULL;
  // input
  AVCodecContext *icodecctx = NULL;
  AVCodecContext *ocodecctx = NULL;
  int got_frame = 0;

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
        // std::cout << "Input is not YUV420P, will have to convert!" << std::endl;
        // see libav/avformat.h
        // AV_PIX_FMT_YUV420P,   ///< planar YUV 4:2:0, 12bpp, (1 Cr & Cb sample per 2x2 Y samples)
        // AV_PIX_FMT_YUYV422,   ///< packed YUV 4:2:2, 16bpp, Y0 Cb Y1 Cr
        // throw std::runtime_error("Currently only YUV420P devices are supported.");
      }
      src_w = in_codecpar->width;
      src_h = in_codecpar->width;
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

    out_stream->codecpar->format = AV_PIX_FMT_YUV422P;
    out_stream->codecpar->width = 640;
    out_stream->codecpar->height = 480;

    sws_ctx = sws_getContext(in_codecpar->width,
                             in_codecpar->height,
                             (AVPixelFormat)in_codecpar->format,
                             640,
                             480,
                             (AVPixelFormat)out_stream->codecpar->format,
                             SWS_BILINEAR,
                             NULL,
                             NULL,
                             NULL);
    if (!sws_ctx) {
      fprintf(stderr,
              "Impossible to create scale context for the conversion "
              "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
              av_get_pix_fmt_name((AVPixelFormat)in_codecpar->format),
              in_codecpar->width,
              in_codecpar->height,
              av_get_pix_fmt_name((AVPixelFormat)out_stream->codecpar->format),
              640,
              480);
      ret = AVERROR(EINVAL);
      goto end;
    }
    sws_init_context(sws_ctx, NULL, NULL);
  }

  av_dump_format(ofmt_ctx, 0, out_filename, 1);

  for (i = 0; i < ifmt_ctx->nb_streams; i++) {
    input_codec = avcodec_find_decoder(ifmt_ctx->streams[i]->codecpar->codec_id);
    if (input_codec->type == AVMEDIA_TYPE_VIDEO) {
      icodecctx = avcodec_alloc_context3(input_codec);
      avcodec_parameters_to_context(icodecctx, ifmt_ctx->streams[i]->codecpar);
      avcodec_open2(icodecctx, input_codec, NULL);
      break;
    }
  }
  //	/* find the mpeg1video encoder */
  //	input_codec = avcodec_find_encoder_by_name("mpeg1video");
  //	if (!input_codec) {
  //		fprintf(stderr, "Codec '%s' not found\n", "mpeg1video");
  //		exit(1);
  //	}
  //	icodecctx = avcodec_alloc_context3(input_codec);
  //	if (!icodecctx) {
  //		fprintf(stderr, "Could not allocate video codec context\n");
  //		exit(1);
  //	}
  //	printf("OK\n");
  //	/* put sample parameters */
  //	icodecctx->bit_rate = 400000;
  //	/* resolution must be a multiple of two */
  //	icodecctx->width = src_w;
  //	icodecctx->height = src_h;
  //	/* frames per second */
  //	icodecctx->time_base = (AVRational){1, 25};
  //	icodecctx->framerate = (AVRational){25, 1};
  //	/* emit one intra frame every ten frames
  //	 * check frame pict_type before passing frame
  //	 * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
  //	 * then gop_size is ignored and the output of encoder
  //	 * will always be I frame irrespective to gop_size
  //	 */
  //	icodecctx->gop_size = 10;
  //	icodecctx->max_b_frames = 1;
  //	icodecctx->pix_fmt = AV_PIX_FMT_YUV420P;

  for (i = 0; i < ofmt_ctx->nb_streams; i++) {
    output_codec = avcodec_find_decoder(ofmt_ctx->streams[i]->codecpar->codec_id);
    if (output_codec->type == AVMEDIA_TYPE_VIDEO) {
      ocodecctx = avcodec_alloc_context3(output_codec);
      avcodec_parameters_to_context(ocodecctx, ofmt_ctx->streams[i]->codecpar);
      avcodec_open2(ocodecctx, output_codec, NULL);
      break;
    }
  }

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

    AVFrame *input_frame = av_frame_alloc();
    AVFrame *output_frame = av_frame_alloc();
    av_image_alloc(input_frame->data, input_frame->linesize, src_w, src_h, video_format, 32);

    {
      int ret = avcodec_send_packet(icodecctx, &pkt);
      if (ret < 0) {
        return 1;
      }
      do {
        ret = avcodec_receive_frame(icodecctx, input_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          break;
        } else if (ret < 0) {
          fprintf(stderr, "Error during decode (%d)\n", ret);  // get_error_text(ret));
          break;
        } else {
          //*data_present += frame->nb_samples > 0;
          printf("read data: %d\n", input_frame->nb_samples);
        }
      } while (ret > 0);
    }

    av_image_alloc(output_frame->data, output_frame->linesize, 640, 480, AV_PIX_FMT_YUV420P, 32);
    output_frame->width = 640;
    output_frame->height = 480;
    output_frame->format = AV_PIX_FMT_YUV420P;

    int rets = sws_scale(sws_ctx,
                         (const uint8_t *const *)input_frame->data,
                         input_frame->linesize,
                         0,
                         input_frame->height,
                         output_frame->data,
                         output_frame->linesize);
    printf("sws_scale: %d\n", rets);

    AVPacket pkt_out;
    pkt_out.data = NULL;
    pkt_out.size = 0;
    av_init_packet(&pkt_out);

    //		int ret = avcodec_encode_video2(ocodecctx, &pkt_out, output_frame, &got_frame);
    //		printf("ret was: %d, got: %d, ocodecctx: %p\n", ret, got_frame, ocodecctx);
    //		//	sws_freeContext(sws_c);

    int retx = avcodec_receive_packet(ocodecctx, &pkt);
    printf("ret recv pkt = %d\n", retx);

    ret = av_write_frame(ofmt_ctx, &pkt);
    if (ret < 0) {
      fprintf(stderr, "Error muxing packet\n");
      break;
    }
    av_packet_unref(&pkt);
    av_packet_unref(&pkt_copy);
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

int main(int argc, char **argv) {
  program prog(argc, argv);
  prog.run();
}