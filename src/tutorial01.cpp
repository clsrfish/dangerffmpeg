
#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <string>

#include "./tutorial01.hpp"

#define CHECK_BIT(state, bitIndex) state& bitIndex == (1 << bitIndex)
#define SET_BIT(state, bitIndex) state = state | (1 << bitIndex)
#define CLEAR_BIT(state, bitIndex) state = state & ~(1 << bitIndex)

void SaveFrame(AVFrame* pFrame, int width, int height, int iFrame);

int tutorial01::main(int argc, char const* argv[]) {
  // av_register_all();

  AVFormatContext* pFormatCtx = nullptr;

  // Open video file
  if (avformat_open_input(&pFormatCtx, argv[1], nullptr, nullptr) != 0) {
    return -1;  // Couldn't open file'
  }

  // Retrive stream information
  if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
    return -1;  // Couldn't find stream information
  }

  // Dump information about the file onto standard error
  av_dump_format(pFormatCtx, 0, argv[1], 0);

  AVCodecContext *pCodecCtxOrig = nullptr, *pCodecCtx = nullptr;
  // Find the first video stream
  unsigned int videoStream = -1;
  for (unsigned i = 0; i < pFormatCtx->nb_streams; i++) {
    if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStream = i;
      break;
    }
  }
  if (videoStream == -1) {
    return -1;  // Didn't find a video stream
  }

  // Get a pointer to the code context for the video stream
  pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;

  AVCodec* pCodec = nullptr;
  // Find the decoder for the video stream
  pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
  if (pCodec == nullptr) {
    spdlog::error("Unsupported codec!");
    return -1;  // Codec not found
  }
  // Copy context parameters
  AVCodecParameters* pParams = avcodec_parameters_alloc();
  avcodec_parameters_from_context(pParams, pCodecCtxOrig);
  pCodecCtx = avcodec_alloc_context3(pCodec);
  if (avcodec_parameters_to_context(pCodecCtx, pParams) < 0) {
    avcodec_parameters_free(&pParams);
    spdlog::error("Couldn't copy codec contxt!");
    return -1;  // Error copying codec context
  }
  avcodec_parameters_free(&pParams);

  // Open codec
  if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
    return -1;  // Couldn't open codec'
  }

  // Allocate video frame
  AVFrame* pFrame = av_frame_alloc();

  AVFrame* pFrameRGB = av_frame_alloc();
  if (pFrameRGB == nullptr) {
    return -1;
  }

  // Determine required buffer size and allocate buffer
  int nbBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
  uint8_t* buffer = reinterpret_cast<uint8_t*>(av_malloc(nbBytes * sizeof(uint8_t)));

  // Assign appropriate parts of the buffer to iamge planes in pFrameRGB
  // Note that pFrameRGB is an AVFrame, but AVFrame is superset
  // of AVPicture
  avpicture_fill(reinterpret_cast<AVPicture*>(pFrameRGB), buffer, AV_PIX_FMT_RGB24, pCodecCtx->width,
                 pCodecCtx->height);

  // initialize SWS context for software scaling
  SwsContext* pSwsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width,
                                       pCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

  int i = 0;
  AVPacket packet;
  int ret;
  bool hasError = false;
  bool hasPktUnconsumed = false;
  bool hasPktEof = false;
  bool hasFinished = false;

  while (!hasError) {
    while (!hasError && !hasPktEof) {
      // Read packet from stream
      if (!hasPktUnconsumed) {
        while (true) {
          ret = av_read_frame(pFormatCtx, &packet);
          if (ret == 0 && packet.stream_index == videoStream) {
            hasPktUnconsumed = true;
          } else if (ret == AVERROR_EOF) {
            hasPktEof = true;
          } else if (ret < 0) {
            hasError = true;
          } else {  // Read next packet
            continue;
          }
          break;
        }
      }
      // Send packet to decoder
      if (!hasError) {
        if (hasPktUnconsumed && !hasPktEof) {
          ret = avcodec_send_packet(pCodecCtx, &packet);
        } else {
          ret = avcodec_send_packet(pCodecCtx, nullptr);  // Flush decoder, EOF will returned, then receive returns EOF.
        }
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret == 0) {
          hasPktUnconsumed = false;
          break;
        } else {
          hasError = true;
        }
      }
    }

    // Receive frame from decoder
    while (!hasError) {
      ret = avcodec_receive_frame(pCodecCtx, pFrame);
      if (ret == 0) {
        sws_scale(pSwsCtx, reinterpret_cast<uint8_t**>(pFrame->data), pFrame->linesize, 0, pCodecCtx->height,
                  pFrameRGB->data, pFrameRGB->linesize);
        // Save the frame to disk
        if (i++ < 5) {
          SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
        }
      } else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        hasFinished = ret == AVERROR_EOF;
        break;
      } else {
        hasError = true;
        break;
      }
    }
    if (hasFinished) {
      break;
    }
  }

  // Free frame
  av_free(buffer);
  av_free(pFrameRGB);
  av_free(pFrame);

  // Close the codec
  avcodec_close(pCodecCtx);
  avcodec_close(pCodecCtxOrig);

  // Close the video file
  avformat_close_input(&pFormatCtx);

  return hasError ? -1 : 0;
}

void SaveFrame(AVFrame* pFrame, int width, int height, int iFrame) {
  std::stringstream ss;
  ss << "build/frame" << iFrame << ".ppm";
  std::string filename = ss.str();

  // Open file
  std::ofstream file(filename, std::ios_base::out | std::ios_base::binary);
  // Write header
  std::stringstream headerSs;
  headerSs << "P6\n" << width << " " << height << "\n255\n";
  std::string header = headerSs.str();
  file.write(reinterpret_cast<const char*>(header.c_str()), header.size());
  // Write pixel data
  for (size_t y = 0; y < height; y++) {
    file.write(reinterpret_cast<const char*>(pFrame->data[0] + y * pFrame->linesize[0]), width * 3);
  }
  file.close();
}