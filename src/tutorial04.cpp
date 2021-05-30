
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

#include <thread>

#include "./packet_queue.hpp"
#include "./tutorial04.hpp"

namespace tutorial04 {
const int SCREEN_W = 640, SCREEN_H = 480;

const int MAX_AUDIOQ_SIZE = 5 * 16 * 1024;
const int MAX_VIDEOQ_SIZE = 5 * 256 * 1024;

const int VIDEO_PICTURE_QUEUE_SIZE = 10;

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

struct VideoPicture {
  AVFrame* frame;
  int width, height;  // source height & width
  bool allocated;
};

struct VideoState {
  AVFormatContext* formatCtx;
  int videoStream, audioStream;

  AVStream* audioSt;
  AVCodecContext* audioCtx;
  PacketQueue audioQueue;
  uint8_t audioBuf[20480];
  unsigned int audioBufSize;
  unsigned int audioBufIndex;
  AVPacket audioPkt;

  AVStream* videoSt;
  AVCodecContext* videoCtx;
  PacketQueue videoQueue;
  SwsContext* swsCtx;

  std::thread decodeT;
  std::thread videoT;

  VideoPicture picQueue[VIDEO_PICTURE_QUEUE_SIZE];
  int pqWindex = 0;
  int pqRIndex = 0;
  std::mutex* picQueueMutex;
  std::condition_variable* picQueueCond;
  int picQueueSize;

  std::string filename;

  SDL_Window* sdlWin;
  SDL_Renderer* sdlRen;
  SDL_Texture* sdlTex;

  bool quit;
};

int audioDecodeFrame(VideoState* is, uint8_t* buf, int bufSize) {
  AVPacket pkt;

  int ret = packetQueueGet(&is->audioQueue, &pkt, true);
  if (ret <= 0) {
    return ret;
  }

  avcodec_send_packet(is->audioCtx, &pkt);

  int bufIndex = 0;
  AVFrame* frame = av_frame_alloc();
  while (avcodec_receive_frame(is->audioCtx, frame) == 0) {
    int dataSize =
        av_samples_get_buffer_size(nullptr, is->audioCtx->channels, frame->nb_samples, is->audioCtx->sample_fmt, 1);
    assert(dataSize <= bufSize - bufIndex);
    memcpy(buf + bufIndex, frame->data[0], dataSize);
    bufIndex += dataSize;
  }
  av_free(frame);
  av_packet_unref(&pkt);
  return bufIndex;
}

void audioCallback(void* userdata, uint8_t* stream, int len) {
  VideoState* is = reinterpret_cast<VideoState*>(userdata);
  unsigned int len1, audioSize;

  while (len > 0) {
    if (is->audioBufIndex >= is->audioBufSize) {
      audioSize = audioDecodeFrame(is, is->audioBuf, sizeof(is->audioBuf));
      if (audioSize < 0) {
        is->audioBufSize = 1024;
        memset(is->audioBuf, 0, is->audioBufSize);
      } else {
        is->audioBufSize = audioSize;
      }
      is->audioBufIndex = 0;
    }

    len1 = is->audioBufSize - is->audioBufIndex;
    if (len1 > len) {
      len1 = len;
    }
    memcpy(stream, is->audioBuf + is->audioBufIndex, len1);
    len -= len1;
    stream += len1;
    is->audioBufIndex += len1;
  }
}

void allocPic(VideoState* is) {
  VideoPicture* vp = &is->picQueue[is->pqWindex];
  if (vp->frame != nullptr) {
    av_free(vp->frame->data);
    // we already have one make another, bigger/smaller
    spdlog::info("alloc a new frame");
    av_frame_free(&vp->frame);
    vp->frame = nullptr;
  }
  spdlog::info("fill the frame");
  // Allocate a place to put our frame
  vp->frame = av_frame_alloc();
  vp->width = is->videoSt->codec->width;
  vp->height = is->videoSt->codec->height;
  vp->allocated = true;
  // Determine required buffer size and allocate buffer
  int nbBytes = avpicture_get_size(AV_PIX_FMT_YUV420P, vp->width, vp->height);
  uint8_t* buffer = reinterpret_cast<uint8_t*>(av_malloc(nbBytes * sizeof(uint8_t)));

  // Assign appropriate parts of the buffer to iamge planes in pFrameRGB
  // Note that pFrameRGB is an AVFrame, but AVFrame is superset
  // of AVPicture
  avpicture_fill(reinterpret_cast<AVPicture*>(vp->frame), buffer, AV_PIX_FMT_YUV420P, vp->width, vp->height);
}

int queuePicture(VideoState* is, AVFrame* pFrame) {
  /* wait until we have space for a new pic */
  std::unique_lock<std::mutex> lk((*is->picQueueMutex));
  while (is->picQueueSize >= VIDEO_PICTURE_QUEUE_SIZE && !is->quit) {
    is->picQueueCond->wait(lk);
  }

  if (is->quit) {
    return -1;
  }

  // windex is set to 0 initially
  VideoPicture* vp = &is->picQueue[is->pqWindex];

  // allocate or resize the buffer
  if (vp->frame == nullptr || vp->width != is->videoSt->codec->width || vp->height != is->videoSt->codec->height) {
    vp->allocated = false;
    allocPic(is);
    if (is->quit) {
      return -1;
    }
  }

  // point pic at the queue
  sws_scale(is->swsCtx, reinterpret_cast<uint8_t**>(pFrame->data), pFrame->linesize, 0, is->videoSt->codec->height,
            reinterpret_cast<uint8_t**>(vp->frame->data), vp->frame->linesize);

  // now we inform out displat thread that we have a pic ready
  if (++is->pqWindex == VIDEO_PICTURE_QUEUE_SIZE) {
    is->pqWindex = 0;
  }
  is->picQueueSize++;
  return 0;
}

void videoThread(VideoState* is) {
  AVFrame* pFrame = av_frame_alloc();
  AVPacket pkt1, *packet = &pkt1;
  while (true) {
    int ret = packetQueueGet(&is->videoQueue, packet, true);
    if (ret < 0) {
      // means we quit getting packets
      break;
    }
    // Decode video frame
    ret = avcodec_send_packet(is->videoCtx, packet);
    av_packet_unref(packet);
    while (avcodec_receive_frame(is->videoCtx, pFrame) == 0) {
      if (queuePicture(is, pFrame) < 0) {
        break;
      }
    }
  }
  av_frame_free(&pFrame);
  spdlog::info("decoding ends");
}

int streamComponentOpen(VideoState* is, int streamIndex) {
  AVFormatContext* pFormatCtx = is->formatCtx;

  if (streamIndex < 0 || streamIndex >= pFormatCtx->nb_streams) {
    return -1;
  }

  AVCodec* pCodec = avcodec_find_decoder(pFormatCtx->streams[streamIndex]->codec->codec_id);
  if (pCodec == nullptr) {
    return -1;
  }

  AVCodecContext* pCodecCtx = avcodec_alloc_context3(pCodec);
  AVCodecParameters* pParams = avcodec_parameters_alloc();
  avcodec_parameters_from_context(pParams, pFormatCtx->streams[streamIndex]->codec);
  if (avcodec_parameters_to_context(pCodecCtx, pParams) < 0) {
    avcodec_parameters_free(&pParams);
    return -1;
  }
  avcodec_parameters_free(&pParams);

  if (pCodecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
    // setup SDL audio here
    SDL_AudioSpec wantedSpec, spec;
    wantedSpec.freq = pCodecCtx->sample_rate;
    wantedSpec.format = AUDIO_F32;
    wantedSpec.channels = pCodecCtx->channels;
    wantedSpec.silence = 0;
    wantedSpec.samples = pCodecCtx->channels * 2;
    wantedSpec.callback = audioCallback;
    wantedSpec.userdata = is;

    if (SDL_OpenAudio(&wantedSpec, &spec) < 0) {
      spdlog::error("SDL_OpenAudio: {s}", SDL_GetError());
      return -1;
    }
  }

  if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
    spdlog::error("Unsupported codec!");
    return -1;
  }

  switch (pCodecCtx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      is->audioStream = streamIndex;
      is->audioSt = pFormatCtx->streams[streamIndex];
      is->audioCtx = pCodecCtx;
      is->audioBufSize = 0;
      is->audioBufIndex = 0;
      packetQueueInit(&is->audioQueue);
      SDL_PauseAudio(0);
      break;
    case AVMEDIA_TYPE_VIDEO:
      is->videoStream = streamIndex;
      is->videoSt = pFormatCtx->streams[streamIndex];
      is->videoCtx = pCodecCtx;

      packetQueueInit(&is->videoQueue);

      is->videoT = std::thread(videoThread, is);
      // initialize SWS context for software scaling
      is->swsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width,
                                  pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
      break;
    defult:
      return -1;
  }
  return 0;
}

int decodeThread(VideoState* is) {
  AVFormatContext* pFormatCtx = nullptr;
  if (avformat_open_input(&pFormatCtx, is->filename.c_str(), nullptr, nullptr) != 0) {
    spdlog::error("Cannot open file");
    return -1;
  }
  is->formatCtx = pFormatCtx;

  // Retrive strean info
  if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
    spdlog::error("Could not find stream information");
    return -1;
  }

  // Dump information about file onto standard output
  av_dump_format(pFormatCtx, 0, is->filename.c_str(), 0);

  int videoIndex = -1, audioIndex = -1;
  // Find the first video stream
  for (int i = 0; i < pFormatCtx->nb_streams; i++) {
    AVMediaType codecType = pFormatCtx->streams[i]->codec->codec_type;
    if (codecType == AVMEDIA_TYPE_VIDEO && videoIndex == -1) {
      videoIndex = i;
    } else if (codecType == AVMEDIA_TYPE_AUDIO && audioIndex == -1) {
      audioIndex = i;
    }
  }

  if (videoIndex >= 0) {
    streamComponentOpen(is, videoIndex);
  }
  if (audioIndex >= 0) {
    streamComponentOpen(is, audioIndex);
  }

  while (!is->quit) {
    if (is->audioQueue.size > MAX_AUDIOQ_SIZE || is->videoQueue.size > MAX_VIDEOQ_SIZE) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    AVPacket packet;
    if (av_read_frame(is->formatCtx, &packet) < 0) {
      if (is->formatCtx->pb->error == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      } else {
        break;
      }
    }

    // Is this a packet from video stream?
    if (packet.stream_index == is->videoStream) {
      packetQueuePut(&is->videoQueue, &packet);
    } else if (packet.stream_index == is->audioStream) {
      packetQueuePut(&is->audioQueue, &packet);
    }
    av_packet_unref(&packet);
  }
  while (!is->quit) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  SDL_Event event;
  event.type = FF_QUIT_EVENT;
  event.user.data1 = is;
  SDL_PushEvent(&event);
  return 0;
}

uint32_t sdlRefreshTimerCb(uint32_t interval, void* opaque) {
  SDL_Event event;
  event.type = FF_REFRESH_EVENT;
  event.user.data1 = opaque;
  SDL_PushEvent(&event);
  return 0; /* 0 means stop timer */
}

/* schedule a video refresh in 'delay' ms */
void scheduleRefresh(VideoState* is, int delay) {
  SDL_AddTimer(delay, sdlRefreshTimerCb, is);
}

void videoDisplay(VideoState* is) {
  VideoPicture* vp = &is->picQueue[is->pqRIndex];
  if (vp->frame == nullptr) {
    return;
  }
  AVFrame* frame = vp->frame;
  // Present the frame with SDL
  SDL_RenderClear(is->sdlRen);
  // Update texture
  if (is->sdlTex == nullptr) {
    // Create texture for displaying
    SDL_Texture* texture = SDL_CreateTexture(is->sdlRen, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
                                             is->videoCtx->width, is->videoCtx->height);
    if (texture == nullptr) {
      spdlog::error("Could not create texture - {s}", SDL_GetError());
      return;
    }
    is->sdlTex = texture;
  }
  SDL_UpdateYUVTexture(is->sdlTex, nullptr, frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1],
                       frame->data[2], frame->linesize[2]);

  // Calculate rect
  float aspectRatio;
  if (is->videoSt->codec->sample_aspect_ratio.num == 0) {
    aspectRatio = static_cast<float>(is->videoSt->codec->width) / static_cast<float>(is->videoSt->codec->height);
  } else {
    aspectRatio =
        av_q2d(is->videoSt->codec->sample_aspect_ratio) * is->videoSt->codec->width / is->videoSt->codec->height;
  }
  int screenW, screenH, w, h;
  SDL_GetWindowSize(is->sdlWin, &screenW, &screenH);
  h = screenH;
  w = static_cast<int>(std::rint(h * aspectRatio)) & -3;
  if (w > screenW) {
    w = screenW;
    h = static_cast<int>(std::rint(w / aspectRatio)) & -3;
  }
  int x = (screenW - w) / 2;
  int y = (screenH - h) / 2;

  SDL_Rect dstRect{x, y, w, h};

  // Draw texture
  SDL_RenderCopy(is->sdlRen, is->sdlTex, nullptr, &dstRect);
  // Update the screen
  SDL_RenderPresent(is->sdlRen);
}

void videoRefreshTimer(void* userdata) {
  VideoState* is = reinterpret_cast<VideoState*>(userdata);
  if (is->videoSt != nullptr) {
    if (is->picQueueSize == 0) {
      scheduleRefresh(is, 1);
    } else {
      // VideoPicture* vp = &is->picQ[is->pqRIndex];
      // Timing code goes here

      scheduleRefresh(is, 20);

      std::lock_guard<std::mutex> lk(*(is->picQueueMutex));

      // show the picture
      videoDisplay(is);

      // update queeu for next picture
      if (++is->pqRIndex == VIDEO_PICTURE_QUEUE_SIZE) {
        is->pqRIndex = 0;
      }
      is->picQueueSize--;
      is->picQueueCond->notify_all();
    }
  } else {
    scheduleRefresh(is, 100);
  }
}

/**
 * @brief main
 *
 * @param argc
 * @param argv
 * @return int
 */
int main(int argc, char const* argv[]) {
  spdlog::info("Tutorial 04: Spawning Threads");

  VideoState* is = reinterpret_cast<VideoState*>(av_mallocz(sizeof(VideoState)));

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    spdlog::error("Could not initialize SDL - {s}", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  //  Create SDL window
  SDL_Window* win = SDL_CreateWindow("Tutorial 04: Spawning Threads", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
  if (win == nullptr) {
    spdlog::error("Could not create window - {s}", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  is->sdlWin = win;
  // Create SDL renderer
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (ren == nullptr) {
    spdlog::error("Could not create renderer - {s}", SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }
  is->sdlRen = ren;

  is->filename = std::string(argv[1]);
  is->picQueueMutex = new std::mutex();
  is->picQueueCond = new std::condition_variable();
  packetQueueInit(&is->videoQueue);
  packetQueueInit(&is->audioQueue);

  is->decodeT = std::thread(decodeThread, is);

  scheduleRefresh(is, 40);

  SDL_Event event;
  while (!is->quit) {
    SDL_WaitEvent(&event);
    switch (event.type) {
      case SDL_QUIT:
        is->quit = true;
        is->videoQueue.quit = true;
        is->audioQueue.quit = true;
        break;
      case FF_REFRESH_EVENT:
        videoRefreshTimer(event.user.data1);
        break;
    }
  }

  // Make sure thread exit
  is->audioQueue.cond->notify_all();
  is->videoQueue.cond->notify_all();
  is->picQueueCond->notify_all();

  // Cleanup SDL
  SDL_DestroyTexture(is->sdlTex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();

  // Close the codec
  avcodec_close(is->videoCtx);
  avcodec_close(is->audioCtx);

  // Close the video file
  avformat_close_input(&is->formatCtx);

  return 0;
}

}  // namespace tutorial04
