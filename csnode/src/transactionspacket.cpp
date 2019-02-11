#include "csnode/transactionspacket.hpp"

#include <csdb/csdb.hpp>
#include <csdb/internal/utils.hpp>
#include <lz4.h>
#include <src/binary_streams.hpp>
#include <src/priv_crypto.hpp>

namespace cs {
//
// Static interface
//

TransactionsPacketHash TransactionsPacketHash::fromString(const ::std::string& str) {
  if (str.empty()) {
    return TransactionsPacketHash();
  }

  TransactionsPacketHash res;
  const cs::Bytes hash = ::csdb::internal::from_hex(str);

  if (::csdb::priv::crypto::hash_size == hash.size()) {
    res.m_bytes = hash;
  }

  return res;
}

TransactionsPacketHash TransactionsPacketHash::fromBinary(const cs::Bytes& data) {
  const size_t size = data.size();
  TransactionsPacketHash resHash;

  if ((0 == size) || (::csdb::priv::crypto::hash_size == size)) {
    resHash.m_bytes = data;
  }

  return resHash;
}

TransactionsPacketHash TransactionsPacketHash::calcFromData(const cs::Bytes& data) {
  TransactionsPacketHash resHash;
  resHash.m_bytes = ::csdb::priv::crypto::calc_hash(data);
  return resHash;
}

//
// Interface
//

bool TransactionsPacketHash::isEmpty() const noexcept {
  return m_bytes.empty();
}

size_t TransactionsPacketHash::size() const noexcept {
  return m_bytes.size();
}

std::string TransactionsPacketHash::toString() const noexcept {
  return csdb::internal::to_hex(m_bytes.begin(), m_bytes.end());
}

const cs::Bytes& TransactionsPacketHash::toBinary() const noexcept {
  return m_bytes;
}

bool TransactionsPacketHash::operator==(const TransactionsPacketHash& other) const noexcept {
  return m_bytes == other.m_bytes;
}

bool TransactionsPacketHash::operator!=(const TransactionsPacketHash& other) const noexcept {
  return !operator==(other);
}

bool TransactionsPacketHash::operator<(const TransactionsPacketHash& other) const noexcept {
  return m_bytes < other.m_bytes;
}

//
// Static interface
//

TransactionsPacket TransactionsPacket::fromBinary(const cs::Bytes& data) {
  return fromByteStream(reinterpret_cast<const char*>(data.data()), data.size());
}

TransactionsPacket TransactionsPacket::fromByteStream(const char* data, size_t size) {
  ::csdb::priv::ibstream is(data, size);

  TransactionsPacket res;

  if (!res.get(is)) {
    return TransactionsPacket();
  }

  res.makeHash();

  return res;
}

TransactionsPacket::TransactionsPacket(TransactionsPacket&& packet)
: m_hash(std::move(packet.m_hash))
, m_transactions(std::move(packet.m_transactions)) {
  packet.m_hash = TransactionsPacketHash();
  packet.m_transactions.clear();
}

TransactionsPacket& TransactionsPacket::operator=(const TransactionsPacket& packet) {
  if (this == &packet) {
    return *this;
  }

  m_hash = packet.m_hash;
  m_transactions = packet.m_transactions;

  return *this;
}

//
// Interface
//

cs::Bytes TransactionsPacket::toBinary(Serialization options) const noexcept {
  ::csdb::priv::obstream os;
  put(os, options);
  return os.buffer();
}

bool TransactionsPacket::makeHash() {
  bool isEmpty = isHashEmpty();

  if (isEmpty) {
    m_hash = TransactionsPacketHash::calcFromData(toBinary(Serialization::Transactions));
  }

  return isEmpty;
}

bool TransactionsPacket::isHashEmpty() const noexcept {
  return m_hash.isEmpty();
}

const TransactionsPacketHash& TransactionsPacket::hash() const noexcept {
  return m_hash;
}

size_t TransactionsPacket::transactionsCount() const noexcept {
  return m_transactions.size();
}

bool TransactionsPacket::addTransaction(const csdb::Transaction& transaction) {
  if (!transaction.is_valid() || !isHashEmpty()) {
    return false;
  }

  m_transactions.push_back(transaction);
  return true;
}

bool TransactionsPacket::addSignature(const cs::Byte index, const cs::Signature& signature) {
  for (auto& it : m_signatures) {
    if (it.first == index) {
      return false;
    }
  }
  m_signatures.emplace_back(std::make_pair(index,signature));
  return true;
 }

const cs::BlockSignatures& TransactionsPacket::signatures() const noexcept {
  return m_signatures;
}

const std::vector<csdb::Transaction>& TransactionsPacket::transactions() const noexcept {
  return m_transactions;
}

std::vector<csdb::Transaction>& TransactionsPacket::transactions() {
  return m_transactions;
}

void TransactionsPacket::clear() noexcept {
  m_transactions.clear();
}

//
// Service
//

void TransactionsPacket::put(::csdb::priv::obstream& os, Serialization options) const {
  if (options & Serialization::Transactions) {
    os.put(m_transactions.size());

    for (const auto& it : m_transactions) {
      os.put(it);
    }
  }

  if (options & Serialization::Signatures) {
    os.put(m_signatures.size());

    for (const auto& it : m_signatures) {
      os.put(it.first);
      os.put(it.second);
    }
  }
}

bool TransactionsPacket::get(::csdb::priv::ibstream& is) {
  std::size_t transactionsCount = 0;

  if (!is.get(transactionsCount)) {
    return false;
  }

  m_transactions.clear();
  m_transactions.reserve(transactionsCount);

  for (std::size_t i = 0; i < transactionsCount; ++i) {
    csdb::Transaction transaction;

    if (!is.get(transaction)) {
      return false;
    }

    m_transactions.push_back(transaction);
  }

  std::size_t signaturesCount = 0;

  if (!is.get(signaturesCount)) {
    return false;
  }

  m_signatures.clear();
  m_signatures.reserve(signaturesCount);

  for (std::size_t i = 0; i < signaturesCount; ++i) {
    cs::Byte index;
    cs::Signature signature;

    if (!is.get(index)) {
      return false;
    }

    if (!is.get(signature)) {
      return false;
    }

    m_signatures.emplace_back(std::make_pair(index, signature));
  }

  return true;
}
}  // namespace cs
