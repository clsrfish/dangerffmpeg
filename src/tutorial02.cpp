
#ifdef __cplusplus
extern "C" {
#endif
#include <SDL.h>
#include <SDL_thread.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif
#include <spdlog/spdlog.h>

#include "./tutorial02.hpp"

namespace tutorial02 {
int main(int argc, char const* argv[]) {
  spdlog::info("Tutorial 02: Outputting to the Screen");

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    spdlog::error("Could not initialize SDL - {s}", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  //  Create SDL window
  SDL_Window* win = SDL_CreateWindow("Tutorial 02: Outputting to the Screen", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_SHOWN);
  if (win == nullptr) {
    spdlog::error("Could not create window - {s}", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  // Create SDL renderer
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (ren == nullptr) {
    spdlog::error("Could not create renderer - {s}", SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  AVFormatContext* pFormatCtx = nullptr;

  // Open video file
  if (avformat_open_input(&pFormatCtx, argv[1], nullptr, nullptr) != 0) {
    return 1;  // Couldn't open file'
  }

  // Retrive stream information
  if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
    return 1;  // Couldn't find stream information
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
    return 1;  // Didn't find a video stream
  }

  // Get a pointer to the code context for the video stream
  pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;

  AVCodec* pCodec = nullptr;
  // Find the decoder for the video stream
  pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
  if (pCodec == nullptr) {
    spdlog::error("Unsupported codec!");
    return 1;  // Codec not found
  }
  // Copy context parameters
  AVCodecParameters* pParams = avcodec_parameters_alloc();
  avcodec_parameters_from_context(pParams, pCodecCtxOrig);
  pCodecCtx = avcodec_alloc_context3(pCodec);
  if (avcodec_parameters_to_context(pCodecCtx, pParams) < 0) {
    avcodec_parameters_free(&pParams);
    spdlog::error("Couldn't copy codec contxt!");
    return 1;  // Error copying codec context
  }
  avcodec_parameters_free(&pParams);

  // Open codec
  if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
    return 1;  // Couldn't open codec'
  }

  // Allocate video frame
  AVFrame* pFrame = av_frame_alloc();

  AVFrame* pFrameYV12 = av_frame_alloc();
  if (pFrameYV12 == nullptr) {
    return 1;
  }

  // Determine required buffer size and allocate buffer
  int nbBytes = avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
  uint8_t* buffer = reinterpret_cast<uint8_t*>(av_malloc(nbBytes * sizeof(uint8_t)));

  // Create texture for displaying
  SDL_Texture* texture =
      SDL_CreateTexture(ren, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);

  // Assign appropriate parts of the buffer to iamge planes in pFrameRGB
  // Note that pFrameRGB is an AVFrame, but AVFrame is superset
  // of AVPicture
  avpicture_fill(reinterpret_cast<AVPicture*>(pFrameYV12), buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width,
                 pCodecCtx->height);

  // initialize SWS context for software scaling
  SwsContext* pSwsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width,
                                       pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);

  AVPacket packet;
  int ret;
  bool hasError = false;
  bool hasPktUnconsumed = false;
  bool hasPktEof = false;
  bool hasFinished = false;

  SDL_Event ev;

  while (!hasError && !hasFinished) {
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
        hasFinished = true;
      }
    }
    if (hasFinished) {
      continue;
    }

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
          spdlog::info("Flush codec with nullptr");
        }
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret == 0) {
          hasPktUnconsumed = ret == AVERROR(EAGAIN);
          if (ret == 0) {
            // Free the packet that was allocated by av_read_frame
            av_packet_unref(&packet);
          }
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
                  pFrameYV12->data, pFrameYV12->linesize);
        // Present the frame with SDL
        SDL_RenderClear(ren);
        // Update texture
        SDL_UpdateYUVTexture(texture, nullptr, pFrameYV12->data[0], pFrameYV12->linesize[0], pFrameYV12->data[1],
                             pFrameYV12->linesize[1], pFrameYV12->data[2], pFrameYV12->linesize[2]);
        // Draw texture
        SDL_RenderCopy(ren, texture, nullptr, nullptr);
        // Update the screen
        SDL_RenderPresent(ren);
        // Take a quick break after all that hard work
        SDL_Delay(50);
      } else if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        hasFinished = ret == AVERROR_EOF;
        break;
      } else {
        hasError = true;
        break;
      }
    }
  }

  // Cleanup SDL
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();

  // Free frame
  av_free(buffer);
  av_free(pFrameYV12);
  av_free(pFrame);

  // Close the codec
  avcodec_close(pCodecCtx);
  avcodec_close(pCodecCtxOrig);

  // Close the video file
  avformat_close_input(&pFormatCtx);

  return 0;
}

}  // namespace tutorial02
