
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

#include <mutex>

#include "./packet_queue.hpp"
#include "./tutorial02.hpp"
namespace tutorial03 {

bool quit = false;

PacketQueue audioQueue;

int audioDecodeFrame(AVCodecContext* aCodecCtx, uint8_t* buf, int bufSize) {
  AVPacket pkt;

  int ret = packetQueueGet(&audioQueue, &pkt, true);
  if (ret <= 0) {
    return ret;
  }

  avcodec_send_packet(aCodecCtx, &pkt);

  int bufIndex = 0;
  AVFrame* frame = av_frame_alloc();
  while (avcodec_receive_frame(aCodecCtx, frame) == 0) {
    int dataSize =
        av_samples_get_buffer_size(nullptr, aCodecCtx->channels, frame->nb_samples, aCodecCtx->sample_fmt, 1);
    assert(dataSize <= bufSize - bufIndex);
    memcpy(buf + bufIndex, frame->data[0], dataSize);
    bufIndex += dataSize;
  }
  av_free(frame);
  av_packet_unref(&pkt);
  return bufIndex;
}

void audioCallback(void* userdata, uint8_t* stream, int len) {
  AVCodecContext* aCodecCtx = reinterpret_cast<AVCodecContext*>(userdata);
  unsigned int len1, audioSize;

  static uint8_t audioBuf[20 * 1024];
  static unsigned int audioBufSize = 0;
  static unsigned int audioBufIndex = 0;

  while (len > 0) {
    if (audioBufIndex >= audioBufSize) {
      audioSize = audioDecodeFrame(aCodecCtx, audioBuf, sizeof(audioBuf));
      if (audioSize < 0) {
        audioBufSize = 1024;
        memset(audioBuf, 0, audioBufSize);
      } else {
        audioBufSize = audioSize;
      }
      audioBufIndex = 0;
    }

    len1 = audioBufSize - audioBufIndex;
    if (len1 > len) {
      len1 = len;
    }
    memcpy(stream, audioBuf + audioBufIndex, len1);
    len -= len1;
    stream += len1;
    audioBufIndex += len1;
  }
}

int main(int argc, char const* argv[]) {
  spdlog::info("Tutorial 03: Playing Sound");

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    spdlog::error("Could not initialize SDL - {s}", SDL_GetError());
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

  AVCodecContext *pACodecCtxOrig = nullptr, *pACodecCtx = nullptr;
  // Find the first audio stream
  unsigned int audioStream = -1;
  for (unsigned i = 0; i < pFormatCtx->nb_streams; i++) {
    if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
      audioStream = i;
      break;
    }
  }
  if (audioStream == -1) {
    return 1;  // Didn't find a video or audio stream
  }

  // Get a pointer to the code context for the video stream
  pACodecCtxOrig = pFormatCtx->streams[audioStream]->codec;

  AVCodec* pACodec = nullptr;
  // Find the decoder for the video stream
  pACodec = avcodec_find_decoder(pACodecCtxOrig->codec_id);
  if (pACodec == nullptr) {
    spdlog::error("Unsupported codec!");
    return 1;  // Codec not found
  }
  // Copy context parameters
  AVCodecParameters* pParams = avcodec_parameters_alloc();
  avcodec_parameters_from_context(pParams, pACodecCtxOrig);
  pACodecCtx = avcodec_alloc_context3(pACodec);
  if (avcodec_parameters_to_context(pACodecCtx, pParams) < 0) {
    avcodec_parameters_free(&pParams);
    spdlog::error("Couldn't copy codec contxt!");
    return 1;  // Error copying codec context
  }
  avcodec_parameters_free(&pParams);

  // Open codec
  if (avcodec_open2(pACodecCtx, pACodec, nullptr) < 0) {
    spdlog::error("Couldn't open codec");
    return 1;  // Couldn't open codec
  }

  // setup SDL audio here
  SDL_AudioSpec wantedSpec, spec;
  wantedSpec.freq = pACodecCtx->sample_rate;
  wantedSpec.format = AUDIO_F32;
  wantedSpec.channels = pACodecCtx->channels;
  wantedSpec.silence = 0;
  wantedSpec.samples = pACodecCtx->channels * 2;
  wantedSpec.callback = audioCallback;
  wantedSpec.userdata = pACodecCtx;

  if (SDL_OpenAudio(&wantedSpec, &spec) < 0) {
    spdlog::error("SDL_OpenAudio: {s}", SDL_GetError());
    return 1;
  }
  packetQueueInit(&audioQueue);
  // unmute
  SDL_PauseAudio(0);

  AVPacket packet;
  int ret;

  SDL_Event ev;

  while (!quit) {
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
        quit = true;
        audioQueue.quit = true;
      }
    }
    if (quit) {
      continue;
    }
    ret = av_read_frame(pFormatCtx, &packet);
    if (ret == 0) {
      if (packet.stream_index == audioStream) {
        packetQueuePut(&audioQueue, &packet);
      }
      av_packet_unref(&packet);
    }
  }
  quit = true;
  audioQueue.cond->notify_all();

  // Cleanup SDL
  SDL_AudioQuit();
  SDL_Quit();

  // Close the codec
  avcodec_close(pACodecCtx);
  avcodec_close(pACodecCtxOrig);

  // Close the video file
  avformat_close_input(&pFormatCtx);

  return 0;
}

}  // namespace tutorial03
