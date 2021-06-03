#if !defined(VIDEO_PICTURE_H)
#define VIDEO_PICTURE_H

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#ifdef __cplusplus
}
#endif

struct VideoPicture {
  AVFrame* frame;
  int width, height;  // source height & width
  bool allocated;
  double pts;
};

#endif  // VIDEO_PICTURE_H
