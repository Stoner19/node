/* Send blaming letters to @yrtimd */
#ifndef NETWORK_HPP
#define NETWORK_HPP

#include <boost/asio.hpp>

#include <client/config.hpp>
#include <lib/system/cache.hpp>
#include "pacmans.hpp"

using io_context = boost::asio::io_context;

class Transport;

class Network {
public:
  explicit Network(const Config&, Transport*);
  ~Network();

  bool isGood() const {
    return good_;
  }

  ip::udp::endpoint resolve(const EndpointData&);

  void sendInit();
  void sendDirect(const Packet&, const ip::udp::endpoint&);

  bool resendFragment(const cs::Hash&, const uint16_t, const ip::udp::endpoint&);
  void registerMessage(Packet*, const uint32_t size);

  Network(const Network&) = delete;
  Network(Network&&) = delete;
  Network& operator=(const Network&) = delete;
  Network& operator=(Network&&) = delete;

  enum ThreadStatus {
    NonInit,
    Failed,
    Success
  };

private:
  void readerRoutine(const Config&);
  void writerRoutine(const Config&);
  void processorRoutine();
  inline void processTask(TaskPtr<IPacMan>&);

  ip::udp::socket* getSocketInThread(const bool, const EndpointData&, std::atomic<ThreadStatus>&, const bool useIPv6);

  bool good_;
  bool stopReaderRoutine = false;
  bool stopWriterRoutine = false;
  bool stopProcessorRoutine = false;

  io_context context_;
  ip::udp::resolver resolver_;

  IPacMan iPacMan_;
  OPacMan oPacMan_;

  Transport* transport_;

  FixedHashMap<cs::Hash, uint32_t, uint16_t, 100000> packetMap_;

  // Only needed in a one-socket configuration
  __cacheline_aligned std::atomic<bool> singleSockOpened_ = {false};
  __cacheline_aligned std::atomic<ip::udp::socket*> singleSock_ = {nullptr};
  std::atomic<bool> initFlag_ = { false };

  __cacheline_aligned std::atomic<ThreadStatus> readerStatus_ = {NonInit};
  __cacheline_aligned std::atomic<ThreadStatus> writerStatus_ = {NonInit};

  std::thread readerThread_;
  std::thread writerThread_;
  std::thread processorThread_;

  PacketCollector collector_;
#ifdef __linux__
  int readerEventfd_;
  int writerEventfd_;
  std::atomic<int> count_ = 0;
#endif
};

#endif  // NETWORK_HPP
