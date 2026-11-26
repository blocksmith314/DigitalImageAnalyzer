#ifndef DIA_FRAMECACHE_H_
#define DIA_FRAMECACHE_H_

#include <QHash>
#include <QImage>
#include <cstddef>
#include <list>

struct CachedFrame {
  int index = 0;
  QImage image;
  double pts_sec = 0.0;
};

// Bounded LRU cache of decoded frames. Insert/Find are cheap; Find updates the
// recency order and returns a pointer valid until the next Insert/Clear.
class FrameCache {
 public:
  explicit FrameCache(std::size_t limit_bytes = 256ull * 1024 * 1024);

  void SetLimitBytes(std::size_t limit_bytes);
  void Clear();
  const CachedFrame* Find(int index);
  const CachedFrame* Nearest(int index) const;
  void Insert(int index, const QImage& image, double pts_sec);

  std::size_t CurrentBytes() const { return bytes_; }
  std::size_t HitCount() const { return hits_; }
  std::size_t MissCount() const { return misses_; }

 private:
  struct Entry {
    CachedFrame frame;
    std::list<int>::iterator lru;
  };

  void Evict();

  QHash<int, Entry> entries_;
  std::list<int> lru_;  // front = most recently used
  std::size_t limit_bytes_;
  std::size_t bytes_ = 0;
  std::size_t hits_ = 0;
  std::size_t misses_ = 0;
};

#endif  // DIA_FRAMECACHE_H_
