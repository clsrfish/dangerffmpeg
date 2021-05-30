#include "./packet_queue.hpp"

void packetQueueInit(PacketQueue* queue) {
  memset(queue, 0, sizeof(PacketQueue));
  queue->mtx = new std::mutex;
  queue->cond = new std::condition_variable;
}

int packetQueuePut(PacketQueue* queue, AVPacket* pkt) {
  AVPacketList* pktl;
  AVPacket* dst = av_packet_alloc();
  if (av_packet_ref(dst, pkt) < 0) {
    return -1;
  }
  pktl = reinterpret_cast<AVPacketList*>(av_malloc(sizeof(AVPacketList)));
  if (pktl == nullptr) {
    return -1;
  }

  pktl->pkt = *dst;
  pktl->next = nullptr;

  std::lock_guard<std::mutex> lk(*(queue->mtx));

  if (queue->lastPkt == nullptr) {
    queue->firstPkt = pktl;
  } else {
    queue->lastPkt->next = pktl;
  }
  queue->lastPkt = pktl;
  queue->nbPackets++;
  queue->size += pktl->pkt.size;

  queue->cond->notify_all();

  return 0;
}

int packetQueueGet(PacketQueue* queue, AVPacket* pkt, bool block) {
  AVPacketList* pktl;

  int ret;

  std::unique_lock<std::mutex> lk(*(queue->mtx));
  while (true) {
    if (queue->quit) {
      return -1;
    }

    pktl = queue->firstPkt;
    if (pktl != nullptr) {
      queue->firstPkt = pktl->next;
      if (queue->firstPkt == nullptr) {
        queue->lastPkt = nullptr;
      }
      queue->nbPackets--;
      queue->size -= pktl->pkt.size;
      *pkt = pktl->pkt;
      av_free(pktl);
      ret = 1;
      break;
    } else if (!block) {
      ret = 0;
    } else {
      queue->cond->wait(lk);
    }
  }
  return ret;
}