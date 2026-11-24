#include "framecache.h"

#include <cstdlib>
#include <utility>

FrameCache::FrameCache(std::size_t limit_bytes) : limit_bytes_(limit_bytes) {}

void FrameCache::SetLimitBytes(std::size_t limit_bytes) {
  limit_bytes_ = limit_bytes;
  Evict();
}

void FrameCache::Clear() {
  entries_.clear();
  lru_.clear();
  bytes_ = 0;
}

const CachedFrame* FrameCache::Nearest(int index) const {
  const CachedFrame* best = nullptr;
  int best_distance = 0;
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    const int distance = std::abs(it.key() - index);
    if (best == nullptr || distance < best_distance) {
      best = &it->frame;
      best_distance = distance;
    }
  }
  return best;
}

const CachedFrame* FrameCache::Find(int index) {
  auto it = entries_.find(index);
  if (it == entries_.end()) {
    misses_++;
    return nullptr;
  }
  hits_++;
  lru_.erase(it->lru);
  lru_.push_front(index);
  it->lru = lru_.begin();
  return &it->frame;
}

void FrameCache::Insert(int index, const QImage& image, double pts_sec) {
  if (image.isNull()) {
    return;
  }
  const std::size_t image_bytes = (std::size_t)image.sizeInBytes();
  auto it = entries_.find(index);
  if (it != entries_.end()) {
    bytes_ -= (std::size_t)it->frame.image.sizeInBytes();
    lru_.erase(it->lru);
    entries_.erase(it);
  }

  Entry entry;
  entry.frame.index = index;
  entry.frame.image = image;
  entry.frame.pts_sec = pts_sec;
  lru_.push_front(index);
  entry.lru = lru_.begin();
  entries_.insert(index, entry);
  bytes_ += image_bytes;
  Evict();
}

void FrameCache::Evict() {
  while (bytes_ > limit_bytes_ && !lru_.empty()) {
    const int oldest = lru_.back();
    lru_.pop_back();
    auto it = entries_.find(oldest);
    if (it != entries_.end()) {
      bytes_ -= (std::size_t)it->frame.image.sizeInBytes();
      entries_.erase(it);
    }
  }
}
