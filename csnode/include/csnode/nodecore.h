#ifndef NODE_CORE_H
#define NODE_CORE_H

#include <string>
#include <vector>
#include <array>
#include <csdb/pool.h>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <map>

#include <boost/smart_ptr/detail/spinlock.hpp>
#include <boost/circular_buffer.hpp>

#include <client/params.hpp>
#include <csnode/transactionspacket.h>

#include <lib/system/common.hpp>
#include <lib/system/keys.hpp>
#include <lib/system/metastorage.hpp>

#ifdef NO_DELAYS
const std::size_t TIME_TO_AWAIT_ACTIVITY = 0;
const std::size_t ROUND_DELAY = 0;
#else
const std::size_t TIME_TO_AWAIT_ACTIVITY = 300;         // ms
const std::size_t ROUND_DELAY = 1000;                   // ms
#endif

const std::size_t TIME_TO_AWAIT_SS_ROUND = 5000;        // ms

namespace std
{
    // transactions packet hash specialization
    template<>
    struct hash<cs::TransactionsPacketHash>
    {
        std::size_t operator()(const cs::TransactionsPacketHash& packetHash) const noexcept;
    };
}

enum NodeLevel {
    Normal,
    Confidant,
    Main,
    Writer
};

namespace cs
{
    // table for fast transactions storage
    using TransactionsPacketTable = std::map<TransactionsPacketHash, TransactionsPacket>;   // TODO: chechk performance of map/unordered_map

    // array of packets
    using TransactionsBlock = std::vector<cs::TransactionsPacket>;

    // array of notifications
    using Notifications = std::vector<cs::Bytes>;

    // round data
    using ConfidantsKeys = std::vector<PublicKey>;
    using Hashes = std::vector<cs::TransactionsPacketHash>;
    using Packets = std::vector<cs::TransactionsPacket>;

    // sync types
    using SharedMutex = std::shared_mutex;
    using SpinLock = boost::detail::spinlock;

    // RAII locks
    using Lock = std::lock_guard<cs::SharedMutex>;
    using SharedLock = std::shared_lock<cs::SharedMutex>;
    using SpinGuard = std::lock_guard<SpinLock>;

    enum NodeConsts : uint32_t
    {
        PublicKeyLength = PUBLIC_KEY_LENGTH,
        Black2HashLength = BLAKE2_HASH_LENGTH,
        HashLength = HASH_LENGTH,
        SignatureLength = SIGNATURE_LENGTH,
        PrivateKeyLength = PRIVATE_KEY_LENGTH,
        PacketHashesRequestDelay = 200 // ms
    };

    enum SolverConsts : uint32_t
    {
        TransactionsFlushRound = 2,
        TransactionsPacketInterval = 50,    // ms
        MaxPacketTransactions = 500,
    };

    // all info about round
    struct Characteristic
    {
      cs::Bytes mask;
    };

    struct RoundTable
    {
        RoundNumber round = 0;
        PublicKey general;
        ConfidantsKeys confidants;
        Hashes hashes;
        Characteristic charBytes;
    };

    struct CharacteristicMeta
    {
        cs::Bytes bytes;
        cs::PublicKey sender;
    };

    struct PoolMetaInfo
    {
        std::string timestamp;
        csdb::Pool::sequence_t sequenceNumber;
    };

    struct HashVector
    {
        uint8_t sender;
        cs::Hash hash;
        cs::Signature signature;
    };

    constexpr std::size_t hashVectorCount = 5;

    struct HashMatrix
    {
        uint8_t sender;
        HashVector hashVector[hashVectorCount];
        cs::Signature signature;
    };

    struct PoolSyncMeta
    {
        csdb::Pool pool;
        cs::Signature signature;
        cs::PublicKey sender;
    };

    using PoolMetaMap = std::map<csdb::Pool::sequence_t, cs::PoolSyncMeta>;

    // metas
    using CharacteristicMetaStorage = cs::MetaStorage<cs::CharacteristicMeta>;
    using TablesMetaStorage = cs::MetaStorage<cs::TransactionsPacketTable>;
    using RoundTablesMetaStorage = cs::MetaStorage<cs::RoundTable>;
    using NeededHashesMetaStorage = cs::MetaStorage<cs::Hashes>;
    using NotificationsMetaStorage = cs::MetaStorage<cs::Notifications>;
}

#endif // NODE_CORE_H

