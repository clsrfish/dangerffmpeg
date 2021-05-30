#if !defined(PKT_QUEUE_H)
#define PKT_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#ifdef __cplusplus
}
#endif
#include <thread>

struct PacketQueue {
  AVPacketList *firstPkt, *lastPkt;
  int nbPackets;
  int size;
  std::mutex* mtx;
  std::condition_variable* cond;
  bool quit;
};

void packetQueueInit(PacketQueue* queue);

int packetQueueGet(PacketQueue* queue, AVPacket* pkt, bool block);

int packetQueuePut(PacketQueue* queue, AVPacket* pkt);

#endif  // PKT_QUEUE_H
