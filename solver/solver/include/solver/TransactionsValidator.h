#ifndef TRANSACTIONS_VALIDATOR_H
#define TRANSACTIONS_VALIDATOR_H

#include <limits>
#include <vector>
#include <lib/system/common.hpp>
#include <csdb/pool.h>
#include <csdb/transaction.h>
#include <solver/WalletsState.h>

namespace cs
{
    class TransactionsValidator
    {
    public:
        using Transactions = std::vector<csdb::Transaction>;
        using CharacteristicMask = cs::Bytes;
        using TransactionIndex = WalletsState::TransactionIndex;
    public:
        struct Config
        {
            size_t initialNegNodesNum_ = 2 * 1024 * 1024;
        };

    public:
        TransactionsValidator(WalletsState& walletsState, const Config& config);

        void reset(size_t transactionsNum);
        bool validateTransaction(const csdb::Transaction& trx, size_t trxInd, uint8_t& del1);
        void validateByGraph(CharacteristicMask& maskIncluded, const Transactions& trxs, csdb::Pool& trxsExcluded);
        size_t getCntRemovedTrxs() const { return cntRemovedTrxs_; }
    private:
        using TrxList = std::vector<TransactionIndex>;
        using Node = WalletsState::WalletData;
        using Stack = std::vector<Node*>;
        static constexpr csdb::Amount zeroBalance_ = 0.0_c;
    private:
        bool validateTransactionAsSource(const csdb::Transaction& trx, size_t trxInd, uint8_t& del1);
        bool validateTransactionAsTarget(const csdb::Transaction& trx);

        void removeTransactions(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded, csdb::Pool& trxsExcluded);
        bool removeTransactions_PositiveOne(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded, csdb::Pool& trxsExcluded);
        bool removeTransactions_PositiveAll(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded, csdb::Pool& trxsExcluded);
        bool removeTransactions_NegativeOne(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded, csdb::Pool& trxsExcluded);
        bool removeTransactions_NegativeAll(Node& node, const Transactions& trxs, CharacteristicMask& maskIncluded, csdb::Pool& trxsExcluded);

    private:
        Config config_;

        WalletsState& walletsState_;
        TrxList trxList_;

        Stack negativeNodes_;
        size_t cntRemovedTrxs_;
    };
}

#endif

