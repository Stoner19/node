#include <algorithm>
#include <unordered_map>

#include <boost/functional/hash.hpp>

#include <csnode/walletsids.hpp>
#include <csnode/walletspools.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

using namespace std;

namespace std {
template <typename T>
struct hash<vector<T>> {
    using Vec = vector<T>;
    size_t operator()(const Vec& vec) const {
        return boost::hash_range(vec.begin(), vec.end());
    }
};
}  // namespace std

namespace cs {
namespace {

class WalletsPoolsImpl {
public:
    using WalletData = WalletsPools::WalletData;
    using WalletId = WalletsPools::WalletId;
    using PoolHash = WalletsPools::PoolHash;

public:
    WalletsPoolsImpl(csdb::Address genesisAddress, csdb::Address startAddress, const WalletsIds& walletsIds);
    ~WalletsPoolsImpl();

    void addWallet(WalletId id);

    template <Direction Dir>
    void load(csdb::Pool& curr);

    const WalletData* findWallet(WalletId id) const;

private:
    template <Direction Dir>
    void load(csdb::Transaction& tr, const PoolHash& poolHash);

    template <Direction Dir>
    void loadTrxForSource(csdb::Transaction& tr, const PoolHash& poolHash);

    template <Direction Dir>
    void loadTrxForTarget(csdb::Transaction& tr, const PoolHash& poolHash);

    bool findWalletId(const csdb::Address& address, WalletId& id) const;
    WalletData* findWalletData(WalletId id);

    template <Direction Dir>
    void addPoolHash(WalletData& walData, const PoolHash& poolHash);

private:
    const csdb::Address genesisAddress_;
    const csdb::Address startAddress_;
    const WalletsIds& walletsIds_;

    using Data = std::unordered_map<WalletId, WalletData>;
    Data data_;
};

WalletsPoolsImpl::WalletsPoolsImpl(csdb::Address genesisAddress, csdb::Address startAddress, const WalletsIds& walletsIds)
: genesisAddress_(genesisAddress)
, startAddress_(startAddress)
, walletsIds_(walletsIds) {
    /*csdebug() << __FUNCTION__ << ": sizeof(PoolHash)=" << sizeof(PoolHash) << " alignof(PoolHash)=" << alignof(PoolHash);
    csdebug() << __FUNCTION__ << ": sizeof(WalletData::PoolHashData)=" << sizeof(WalletData::PoolHashData) << "
    alignof(WalletData::PoolHashData)=" << alignof(WalletData::PoolHashData); csdebug() << __FUNCTION__ << ":
    sizeof(WalletData::PoolsHashes)=" << sizeof(WalletData::PoolsHashes) << " alignof(WalletData::PoolsHashes)=" <<
    alignof(WalletData::PoolsHashes); csdebug() << __FUNCTION__ << ": sizeof(WalletData)=" << sizeof(WalletData) << "
    alignof(WalletData)=" << alignof(WalletData);*/
}

WalletsPoolsImpl::~WalletsPoolsImpl() {
}

template <Direction Dir>
void WalletsPoolsImpl::load(csdb::Pool& curr) {
    PoolHash poolHash;
    WalletsPools::convert(curr.hash(), poolHash);
    // csdebug() << __FUNCTION__ << ": mode=" << mode << " poolHash=" << poolHash << " trxNum=" <<
    // curr.transactions_count();

    for (size_t i = 0; i < curr.transactions_count(); i++) {
        csdb::Transaction tr = curr.transaction(i);
        load<Dir>(tr, poolHash);
    }
}

template <Direction Dir>
void WalletsPoolsImpl::load(csdb::Transaction& tr, const PoolHash& poolHash) {
    loadTrxForSource<Dir>(tr, poolHash);
    loadTrxForTarget<Dir>(tr, poolHash);
}

template <Direction Dir>
void WalletsPoolsImpl::loadTrxForSource(csdb::Transaction& tr, const PoolHash& poolHash) {
    csdb::Address wallAddress = tr.source();

    if (wallAddress == genesisAddress_ || wallAddress == startAddress_)
        return;
    WalletId id{};
    if (!findWalletId(wallAddress, id)) {
        cserror() << "Cannot find source wallet";
        return;
    }
    auto* walData = findWalletData(id);
    if (!walData)
        return;

    addPoolHash<Dir>(*walData, poolHash);
}

template <Direction Dir>
void WalletsPoolsImpl::loadTrxForTarget(csdb::Transaction& tr, const PoolHash& poolHash) {
    csdb::Address wallAddress = tr.target();

    if (wallAddress == genesisAddress_ || wallAddress == startAddress_)
        return;
    WalletId id{};
    if (!findWalletId(wallAddress, id)) {
        cserror() << "Cannot find target wallet";
        return;
    }
    auto* walData = findWalletData(id);
    if (!walData)
        return;

    addPoolHash<Dir>(*walData, poolHash);
}

bool WalletsPoolsImpl::findWalletId(const csdb::Address& address, WalletId& id) const {
    if (!walletsIds_.normal().find(address, id))
        return false;
    return true;
}

WalletsPoolsImpl::WalletData* WalletsPoolsImpl::findWalletData(WalletId id) {
    auto it = data_.find(id);
    if (it == data_.end())
        return nullptr;
    return &it->second;
}

void WalletsPoolsImpl::addWallet(WalletId id) {
    (void)data_.emplace(id, WalletData());
}

template <>
void WalletsPoolsImpl::addPoolHash<Direction::PrevBlock>(WalletData& walData, const PoolHash& poolHash) {
    bool isHashEqual = (!walData.poolsHashes_.empty() && walData.poolsHashes_.front().poolHash == poolHash);

    if (!isHashEqual && !walData.poolsHashes_.full()) {
        walData.poolsHashes_.push_front(WalletData::PoolHashData{poolHash, 0});
        isHashEqual = true;
    }
    if (isHashEqual && walData.poolsHashes_.front().trxNum < WalletData::PoolHashData::maxTrxNum) {
        ++walData.poolsHashes_.front().trxNum;
    }
}

template <>
void WalletsPoolsImpl::addPoolHash<Direction::NextBlock>(WalletData& walData, const PoolHash& poolHash) {
    bool isHashEqual = (!walData.poolsHashes_.empty() && walData.poolsHashes_.back().poolHash == poolHash);

    if (!isHashEqual) {
        if (walData.poolsHashes_.full())
            walData.poolsHashes_.pop_front();
        walData.poolsHashes_.push_back(WalletData::PoolHashData{poolHash, 0});
        isHashEqual = true;
    }
    if (isHashEqual && walData.poolsHashes_.back().trxNum < WalletData::PoolHashData::maxTrxNum) {
        ++walData.poolsHashes_.back().trxNum;
    }
}

const WalletsPoolsImpl::WalletData* WalletsPoolsImpl::findWallet(WalletId id) const {
    auto itWalData = data_.find(id);
    if (itWalData == data_.end())
        return nullptr;
    return &itWalData->second;
}

}  // namespace

WalletsPools::WalletsPools(csdb::Address genesisAddress, csdb::Address startAddress, const WalletsIds& walletsIds)
: impl_(new WalletsPoolsImpl(genesisAddress, startAddress, walletsIds)) {
}

WalletsPools::~WalletsPools() {
    WalletsPoolsImpl* impl = (WalletsPoolsImpl*)impl_;
    delete impl;
}

void WalletsPools::addWallet(WalletId id) {
    WalletsPoolsImpl* impl = (WalletsPoolsImpl*)impl_;
    impl->addWallet(id);
}

void WalletsPools::loadPrevBlock(csdb::Pool& curr) {
    WalletsPoolsImpl* impl = (WalletsPoolsImpl*)impl_;
    impl->load<Direction::PrevBlock>(curr);
}

void WalletsPools::loadNextBlock(csdb::Pool& curr) {
    WalletsPoolsImpl* impl = (WalletsPoolsImpl*)impl_;
    impl->load<Direction::NextBlock>(curr);
}

const WalletsPools::WalletData* WalletsPools::findWallet(WalletId id) const {
    WalletsPoolsImpl* impl = (WalletsPoolsImpl*)impl_;
    return impl->findWallet(id);
}

void WalletsPools::convert(const csdb::PoolHash& poolHashDb, PoolHash& poolHashCache) {
    const cs::Bytes& phVec = poolHashDb.to_binary();
    copy(phVec.begin(), phVec.end(), poolHashCache.begin());
}

void WalletsPools::convert(const PoolHash& poolHashCache, csdb::PoolHash& poolHashDb) {
    cs::Bytes hashBytes(poolHashCache.begin(), poolHashCache.end());
    poolHashDb = csdb::PoolHash::from_binary(std::move(hashBytes));
}

}  // namespace cs
