#pragma once

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

  printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\r",
         tag,
         av_ts2str(pkt->pts),
         av_ts2timestr(pkt->pts, time_base),
         av_ts2str(pkt->dts),
         av_ts2timestr(pkt->dts, time_base),
         av_ts2str(pkt->duration),
         av_ts2timestr(pkt->duration, time_base),
         pkt->stream_index);
}
