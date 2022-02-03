#pragma once

#include <atomic>
#include <functional>

template<class CbData>
class ThingWithCb {
public:
  using Cb = std::function<void(const CbData& cbData)>;

  virtual void setCb(const Cb& cb) = 0;

  using PreAcquireFiller = std::function<void()>;

  using PostAcquireFiller = std::function<void()>;

  void rtProcess(const PreAcquireFiller& preAcquireFiller, const PostAcquireFiller& postAcquireFiller) {
    preAcquireFiller();
    useCb(postAcquireFiller);
  }

protected:
  virtual void useCb(const PostAcquireFiller& postAcquireFiller) = 0;

  Cb _cb{nullptr};
};

template<class CbData>
class ThingWithCbAndSpinlock : public ThingWithCb<CbData> {
public:
  using Cb = typename ThingWithCb<CbData>::Cb;

  void setCb(const Cb& cb) override {
    while (_usingCb.test_and_set());
    this->_cb = cb;
    _usingCb.clear();
  }

protected:
  using PostAcquireFiller = typename ThingWithCb<CbData>::PostAcquireFiller;

  void useCb(const PostAcquireFiller& postAcquireFiller) override {
    if (!_usingCb.test_and_set()) {
      if (this->_cb != nullptr) {
        postAcquireFiller();
        this->_cb(CbData());
      }
      _usingCb.clear();
    }
  }

private:
  std::atomic_flag _usingCb = ATOMIC_FLAG_INIT;
};