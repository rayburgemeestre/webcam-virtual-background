#pragma once

#define __STDC_CONSTANT_MACROS

extern "C" {
#define __STDC_CONSTANT_MACROS
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavformat/internal.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>
}

#include "ffmpeg_compat.hpp"
