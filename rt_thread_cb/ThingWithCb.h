#pragma once

#include <atomic>
#include <functional>
#include "ea_data_structures.h"
#include "../common/SPSCQ.h"

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
    // "test and test-and-set" loop
    while (true) {
      // try to set the flag
      if (!_usingCb.test_and_set(std::memory_order::acquire)) {
        break;
      }
      // if that fails, spin on a load to reduce cache traffic
      while (_usingCb.test(std::memory_order::relaxed)) {
        // save power by using the platform-specific pause instruction
        // TODO: Find all the ones we care about and add them
#ifdef __GNUC__
        __builtin_ia32_pause();
#endif
      }
    }
    this->_cb = cb;
    _usingCb.clear(std::memory_order::release);
  }

protected:
  using PostAcquireFiller = typename ThingWithCb<CbData>::PostAcquireFiller;

  void useCb(const PostAcquireFiller& postAcquireFiller) override {
    if (!_usingCb.test_and_set(std::memory_order::acquire)) {
      if (this->_cb != nullptr) {
        postAcquireFiller();
        this->_cb(CbData());
      }
      _usingCb.clear(std::memory_order::release);
    }
  }

private:
  std::atomic_flag _usingCb = ATOMIC_FLAG_INIT;
};

template<class CbData>
class ThingWithCbAndSPSCQ : public ThingWithCb<CbData> {
public:
  using Cb = typename  ThingWithCb<CbData>::Cb;

  void setCb(const Cb& cb) override {
    _q.push(cb);
  }

protected:
  using PostAcquireFiller = typename ThingWithCb<CbData>::PostAcquireFiller;

  void useCb(const PostAcquireFiller& postAcquireFiller) override {
    _q.popLast(this->_cb);
    if (this->_cb != nullptr) {
      postAcquireFiller();
      this->_cb(CbData());
    }
  }

private:
  SPSCQ<Cb, 8> _q;
};

template<class CbData>
class ThingWithCbAndEyalAmirFifo : public ThingWithCb<CbData> {
public:
  using Cb = typename  ThingWithCb<CbData>::Cb;

  void setCb(const Cb& cb) override {
    _q.push(cb);
  }

protected:
  using PostAcquireFiller = typename ThingWithCb<CbData>::PostAcquireFiller;

  void useCb(const PostAcquireFiller& postAcquireFiller) override {
    this->_cb = _q.pull();
    if (this->_cb != nullptr) {
      postAcquireFiller();
      this->_cb(CbData());
    }
  }

private:
  EA::Fifo<Cb, 5> _q;
};
