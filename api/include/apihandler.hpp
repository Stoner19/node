#ifndef APIHANDLER_HPP
#define APIHANDLER_HPP

#if defined(_MSC_VER)
#pragma warning(push)
// 4706 - assignment within conditional expression
// 4373 - 'api::APIHandler::TokenTransfersListGet': virtual function overrides 'api::APINull::TokenTransfersListGet',
//         previous versions of the compiler did not override when parameters only differed by const/volatile qualifiers
// 4245 - 'return' : conversion from 'int' to 'SOCKET', signed / unsigned mismatch
#pragma warning(disable : 4706 4373 4245) 
#endif

#include <API.h>
#include <APIEXEC.h>
#include <executor_types.h>
#include <general_types.h>

#include <thrift/transport/TSocket.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <csnode/blockchain.hpp>

#include <csstats.hpp>
#include <deque>
#include <queue>

#include <client/params.hpp>
#include <client/config.hpp>

#include <lib/system/reference.hpp>
#include <lib/system/concurrent.hpp>
#include <lib/system/process.hpp>

#include "tokens.hpp"

#include <tuple>
#include <any>
#include <optional>

#include <csdb/currency.hpp>

namespace csconnector {
class connector;
}  // namespace csconnector

class APIHandlerBase {
public:
    enum class APIRequestStatusType : uint8_t {
        SUCCESS,
        FAILURE,
        NOT_IMPLEMENTED,
        NOT_FOUND,
        INPROGRESS,
        MAX
    };

    static void SetResponseStatus(general::APIResponse& response, APIRequestStatusType status, const std::string& details = "");
    static void SetResponseStatus(general::APIResponse& response, bool commandWasHandled);
};

struct APIHandlerInterface : public api::APINull, public APIHandlerBase {};

template <typename T>
T deserialize(std::string&& s) {
    using namespace ::apache;

    // https://stackoverflow.com/a/16261758/2016154
    static_assert(CHAR_BIT == 8 && std::is_same<std::uint8_t, unsigned char>::value, "This code requires std::uint8_t to be implemented as unsigned char.");

    const auto buffer = thrift::stdcxx::make_shared<thrift::transport::TMemoryBuffer>(reinterpret_cast<uint8_t*>(&(s[0])), static_cast<uint32_t>(s.size()));
    thrift::protocol::TBinaryProtocol proto(buffer);
    T sc;
    sc.read(&proto);
    return sc;
}

template <typename T>
std::string serialize(const T& sc) {
    using namespace ::apache;

    auto buffer = thrift::stdcxx::make_shared<thrift::transport::TMemoryBuffer>();
    thrift::protocol::TBinaryProtocol proto(buffer);
    sc.write(&proto);
    return buffer->getBufferAsString();
}

namespace cs {
class SolverCore;
class SmartContracts;
}

namespace executor {
class APIResponse;
class ContractExecutorConcurrentClient;
}  // namespace executor

namespace executor {
class Executor;

struct ExecutorSettings {
    using Types = std::tuple<cs::Reference<const BlockChain>,
                             cs::Reference<const cs::SolverCore>,
                             cs::Reference<const Config>>;

    static void set(cs::Reference<const BlockChain> blockchain,
                    cs::Reference<const cs::SolverCore> solver,
                    cs::Reference<const Config> config) {
        blockchain_ = blockchain;
        solver_ = solver;
        config_ = config;
    }

private:
    static Types get() {
        auto tuple = std::make_tuple(std::any_cast<cs::Reference<const BlockChain>>(blockchain_),
                                     std::any_cast<cs::Reference<const cs::SolverCore>>(solver_),
                                     std::any_cast<cs::Reference<const Config>>(config_));

        blockchain_.reset();
        solver_.reset();
        config_.reset();

        return tuple;
    }

    inline static std::any blockchain_;
    inline static std::any solver_;
    inline static std::any config_;

    friend class Executor;
};

class Executor {
public:  // wrappers
    
    // Pass kUseLastSequence to executeByteCode...() to use current last sequence automatically
    static constexpr cs::Sequence kUseLastSequence = 0;

    void executeByteCode(executor::ExecuteByteCodeResult& resp, const std::string& address, const std::string& smart_address, const std::vector<general::ByteCodeObject>& code,
        const std::string& state, std::vector<MethodHeader>& methodHeader, bool isGetter, cs::Sequence sequence);

    void executeByteCodeMultiple(ExecuteByteCodeMultipleResult& _return, const ::general::Address& initiatorAddress, const SmartContractBinary& invokedContract,
        const std::string& method, const std::vector<std::vector<::general::Variant>>& params, const int64_t executionTime, cs::Sequence sequence);

    void getContractMethods(GetContractMethodsResult& _return, const std::vector<::general::ByteCodeObject>& byteCodeObjects) {
        try {
            std::shared_lock lock(sharedErrorMutex_);
            origExecutor_->getContractMethods(_return, byteCodeObjects, EXECUTOR_VERSION);
        }
        catch (const ::apache::thrift::transport::TTransportException& x) {
            // sets stop_ flag to true forever, replace with new instance
            if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
                recreateOriginExecutor();
            }

            _return.status.code = 1;
            _return.status.message = x.what();

            notifyError();
        }
        catch(const std::exception& x ) {
            _return.status.code = 1;
            _return.status.message = x.what();

            notifyError();
        }
    }

    void getContractVariables(GetContractVariablesResult& _return, const std::vector<::general::ByteCodeObject>& byteCodeObjects, const std::string& contractState) {
        try {
            std::shared_lock lock(sharedErrorMutex_);
            origExecutor_->getContractVariables(_return, byteCodeObjects, contractState, EXECUTOR_VERSION);
        }
        catch (const ::apache::thrift::transport::TTransportException& x) {
            // sets stop_ flag to true forever, replace with new instance
            if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
                recreateOriginExecutor();
            }

            _return.status.code = 1;
            _return.status.message = x.what();

            notifyError();
        }
        catch(const std::exception& x ) {
            _return.status.code = 1;
            _return.status.message = x.what();

            notifyError();
        }
    }

    void compileSourceCode(CompileSourceCodeResult& _return, const std::string& sourceCode) {
        try {
            std::shared_lock slk(sharedErrorMutex_);
            origExecutor_->compileSourceCode(_return, sourceCode, EXECUTOR_VERSION);
        }
        catch (::apache::thrift::transport::TTransportException& x) {
            // sets stop_ flag to true forever, replace with new instance
            if (x.getType() == ::apache::thrift::transport::TTransportException::NOT_OPEN) {
                recreateOriginExecutor();
            }

            _return.status.code = 1;
            _return.status.message = x.what();

            notifyError();
        }
        catch(const std::exception& x ) {
            _return.status.code = 1;
            _return.status.message = x.what();

            notifyError();
        }
    }

public:
    static Executor& getInstance() {  // singlton
        static Executor executor(executor::ExecutorSettings::get());
        return executor;
    }

    bool isConnected() const {
        return executorTransport_->isOpen();
    }

    void stop() {
        requestStop_ = true;

        // wake up watching thread if it sleeps
        notifyError();

        if (executorProcess_) {
            if (executorProcess_->isRunning()) {
                disconnect();
                executorProcess_->terminate();
            }
        }
    }

    std::optional<cs::Sequence> getSequence(const general::AccessID& accessId) {
        std::shared_lock lock(mutex_);
        if (auto it = accessSequence_.find(accessId); it != accessSequence_.end()) {
            return std::make_optional(it->second);
        }
        return std::nullopt;
    }

    std::optional<csdb::TransactionID> getDeployTrxn(const csdb::Address& p_address) {
        std::shared_lock lock(mutex_);
        if (const auto it = deployTrxns_.find(p_address); it != deployTrxns_.end()) {
            return std::make_optional(it->second);
        }
        return std::nullopt;
    }

    void updateDeployTrxns(const csdb::Address& p_address, const csdb::TransactionID& p_trxnsId) {
        std::lock_guard lock(mutex_);
        deployTrxns_[p_address] = p_trxnsId;
    }

    void setLastState(const csdb::Address& p_address, const std::string& p_state) {
        std::lock_guard lock(mutex_);
        lastState_[p_address] = p_state;
    }

    std::optional<std::string> getState(const csdb::Address& p_address);

    void updateCacheLastStates(const csdb::Address& p_address, const cs::Sequence& sequence, const std::string& state) {
        std::lock_guard lock(mutex_);
        if (execCount_) {
            (cacheLastStates_[p_address])[sequence] = state;
        }
        else if (cacheLastStates_.size()) {
            cacheLastStates_.clear();
        }
    }

    std::optional<std::string> getAccessState(const general::AccessID& p_access_id, const csdb::Address& p_address) {
        std::shared_lock slk(mutex_);
        const auto access_sequence = getSequence(p_access_id);
        if (const auto unmap_states_it = cacheLastStates_.find(p_address); unmap_states_it != cacheLastStates_.end()) {
            std::pair<cs::Sequence, std::string> prev_seq_state{};
            for (const auto& [curr_seq, curr_state] : unmap_states_it->second) {
                if (curr_seq > access_sequence) {
                    return prev_seq_state.first ? std::make_optional<std::string>(prev_seq_state.second) : std::nullopt;
                }
                prev_seq_state = {curr_seq, curr_state};
            }
        }
        auto opt_last_sate = getState(p_address);
        return opt_last_sate.has_value() ? std::make_optional<std::string>(opt_last_sate.value()) : std::nullopt;
    }

    struct ExecuteResult {
        struct EmittedTrxn {
            csdb::Address source;
            csdb::Address target;
            csdb::Amount amount;
            std::string userData;
        };

        struct SmartRes {
            general::Variant retValue;
            std::map<csdb::Address, std::string> states;
            std::vector<EmittedTrxn> emittedTransactions;
            int64_t executionCost; // measured in milliseconds actual cost of execution
            ::general::APIResponse response;
        };

        ::general::APIResponse response;
        std::vector<SmartRes> smartsRes;
        //std::vector<csdb::Transaction> trxns;       
        long selfMeasuredCost; // measured in milliseconds total cost of executions       
    };

    void addInnerSendTransaction(const general::AccessID& accessId, const csdb::Transaction& transaction) {
        std::lock_guard lk(mutex_);
        innerSendTransactions_[accessId].push_back(transaction);
    }

    std::optional<std::vector<csdb::Transaction>> getInnerSendTransactions(const general::AccessID& accessId) {
        std::shared_lock slk(mutex_);
        if (const auto it = innerSendTransactions_.find(accessId); it != innerSendTransactions_.end()) {
            return std::make_optional<std::vector<csdb::Transaction>>(it->second);
        }
        return std::nullopt;
    }

    void deleteInnerSendTransactions(const general::AccessID& accessId) {
        std::lock_guard lk(mutex_);
        innerSendTransactions_.erase(accessId);
    }

    bool isDeploy(const csdb::Transaction& trxn) {
        if (trxn.user_field(0).is_valid()) {
            const auto sci = deserialize<api::SmartContractInvocation>(trxn.user_field(0).value<std::string>());
            if (sci.method.empty()) {
                return true;
            }
        }
        return false;
    }

    // Convention how to pass the method name
    enum class MethodNameConvention {
        // By default, the method name can be obtained from SmartContractInvocation object deserialized from user_field[0]
        // If method name is empty, the constructor must be called
        Default = 0,
        // Call to payable(string, string) requested
        PayableLegacy,
        // Call to payable(string, byte[]) requested
        Payable
    };

    enum ACCESS_ID_RESERVE { GETTER, START_INDEX };

    struct ExecuteTransactionInfo {
        // transaction to execute contract
        csdb::Transaction transaction;
        // transaction containing deploy info
        csdb::Transaction deploy;
        // pass method name convention
        MethodNameConvention convention;
        // max allowed fee
        csdb::Amount feeLimit;
        // block sequnece
        cs::Sequence sequence;
    };

    /**
     * Executes the transaction operation
     *
     * @param   smarts              The list of smart contract related transactions to execute.
     * @param   forceContractState  The forced state of the contract to use in execution, if not empty overrides stored state in blocks.
     * @param   validationMode      True to enable validation mode, false to disable it. If set to true the execution is only for validation,
     *                              so any contract can (and must) be modified. The result is guaranteed not to put to chain
     *
     * @returns A std::optional&lt;ExecuteResult&gt;
     */

    std::optional<ExecuteResult> executeTransaction(const std::vector<ExecuteTransactionInfo>& smarts, std::string forceContractState);

    std::optional<ExecuteResult> reexecuteContract(ExecuteTransactionInfo& contract, std::string forceContractState);

    csdb::Transaction make_transaction(const api::Transaction& transaction) {
        csdb::Transaction send_transaction;
        const auto source = BlockChain::getAddressFromKey(transaction.source);
        const uint64_t WALLET_DENOM = csdb::Amount::AMOUNT_MAX_FRACTION;  // 1'000'000'000'000'000'000ull;
        send_transaction.set_amount(csdb::Amount(transaction.amount.integral, uint64_t(transaction.amount.fraction), WALLET_DENOM));
        BlockChain::WalletData wallData{};
        BlockChain::WalletId id{};

        if (!blockchain_.findWalletData(source, wallData, id))
            return csdb::Transaction{};

        send_transaction.set_currency(csdb::Currency(1));
        send_transaction.set_source(source);
        send_transaction.set_target(BlockChain::getAddressFromKey(transaction.target));
        send_transaction.set_max_fee(csdb::AmountCommission(uint16_t(transaction.fee.commission)));
        send_transaction.set_innerID(transaction.id & 0x3fffffffffff);

        // TODO Change Thrift to avoid copy
        cs::Signature signature;
        if (transaction.signature.size() == signature.size())
            std::copy(transaction.signature.begin(), transaction.signature.end(), signature.begin());
        else
            signature.fill(0);
        send_transaction.set_signature(signature);
        return send_transaction;
    }

    void state_update(const csdb::Pool& pool);

    void addToLockSmart(const general::Address& address, const general::AccessID& accessId) {
        std::lock_guard lk(mutex_);
        lockSmarts[address] = accessId;
    }

    void deleteFromLockSmart(const general::Address& address, const general::AccessID& accessId) {
        csunused(accessId);
        std::lock_guard lk(mutex_);
        lockSmarts.erase(address);
    }

    bool isLockSmart(const general::Address& address, const general::AccessID& accessId) {
        std::lock_guard lk(mutex_);
        if (auto addrLock = lockSmarts.find(address); addrLock != lockSmarts.end() && addrLock->second == accessId)
            return true;
        return false;
    }

    mutable std::mutex mt;

    // equivalent access to the blockchain for api and other threads
    template<typename T, typename = std::enable_if_t<std::is_same_v<T, csdb::PoolHash> || std::is_same_v<T, cs::Sequence>>>
    csdb::Pool loadBlockApi(const T& p) const {
        std::lock_guard lk(mt);
        return blockchain_.loadBlock(p);
    }

    csdb::Transaction loadTransactionApi(const csdb::TransactionID& id) const {
        std::lock_guard lk(mt);
        return blockchain_.loadTransaction(id);
    }

public slots:
    void onBlockStored(const csdb::Pool& pool) {
        state_update(pool);
    }

    void onReadBlock(const csdb::Pool& block, bool* test_failed) {
        csunused(test_failed);
        state_update(block);
    }

    void onExecutorStarted() {
        if (!isConnected()) {
            connect();
        }
    }

    void onExecutorFinished() {
        if (!executorProcess_->isRunning() && !requestStop_) {
            executorProcess_->launch(cs::Process::Options::None);
        }
    }

    void onExecutorProcessError(const cs::ProcessException& exception) {
        cswarning() << "Executor process error occured " << exception.what() << ", code " << exception.code();
    }

    void onConfigChanged(const Config& updated, const Config& previous) {
        if (updated.getApiSettings().executorCmdLine == previous.getApiSettings().executorCmdLine) {
            return;
        }

        if (updated.getApiSettings().executorCmdLine.empty()) {
            return;
        }

        executorProcess_->setProgram(updated.getApiSettings().executorCmdLine);
    }

private:
    std::map<general::Address, general::AccessID> lockSmarts;

    explicit Executor(const ExecutorSettings::Types& types)
    : blockchain_(std::get<cs::Reference<const BlockChain>>(types))
    , solver_(std::get<cs::Reference<const cs::SolverCore>>(types))
    , config_(std::get<cs::Reference<const Config>>(types))
    , socket_(::apache::thrift::stdcxx::make_shared<::apache::thrift::transport::TSocket>(config_.getApiSettings().executorHost, config_.getApiSettings().executorPort))
    , executorTransport_(new ::apache::thrift::transport::TBufferedTransport(socket_))
    , origExecutor_(
          std::make_unique<executor::ContractExecutorConcurrentClient>(::apache::thrift::stdcxx::make_shared<apache::thrift::protocol::TBinaryProtocol>(executorTransport_))) {
        socket_->setSendTimeout(config_.getApiSettings().executorSendTimeout);
        socket_->setRecvTimeout(config_.getApiSettings().executorReceiveTimeout);

        if (config_.getApiSettings().executorCmdLine.empty()) {
            cswarning() << "Executor command line args are empty, process would not be created";
            return;
        }

        executorProcess_ = std::make_unique<cs::Process>(config_.getApiSettings().executorCmdLine);

        cs::Connector::connect(&executorProcess_->started, this, &Executor::onExecutorStarted);
        cs::Connector::connect(&executorProcess_->finished, this, &Executor::onExecutorFinished);
        cs::Connector::connect(&executorProcess_->errorOccured, this, &Executor::onExecutorProcessError);

        executorProcess_->launch(cs::Process::Options::None);
        while (!executorProcess_->isRunning());

        std::thread thread([this]() {
            while(!requestStop_) {
                if (isConnected()) {
                    static std::mutex mutex;
                    std::unique_lock lock(mutex);

                    cvErrorConnect_.wait(lock, [&] {
                        return !isConnected() || requestStop_;
                    });
                }

                if (requestStop_) {
                    break;
                }

                if (!isConnected()) {
                    connect();
                }
            }
        });

        thread.detach();
    }

    ~Executor() {
        stop();
    }

    struct OriginExecuteResult {
        ExecuteByteCodeResult resp;
        general::AccessID acceessId;
        // measured execution duration in milliseconds
        long long timeExecute;
    };

    // The explicit_sequence is set for generated accessId ensure having correct sequence attached to it
    uint64_t generateAccessId(cs::Sequence explicit_sequence) {
        std::lock_guard lk(mutex_);
        ++lastAccessId_;
        accessSequence_[lastAccessId_] = (explicit_sequence != kUseLastSequence ? explicit_sequence : blockchain_.getLastSeq());
        return static_cast<uint64_t>(lastAccessId_);
    }

    uint64_t getFutureAccessId() {
        return static_cast<uint64_t>(lastAccessId_ + 1);
    }

    void deleteAccessId(const general::AccessID& p_access_id) {
        std::lock_guard lk(mutex_);
        accessSequence_.erase(p_access_id);
    }

    // explicit sequence sets the sequence for accessId attached to execution
    std::optional<OriginExecuteResult> execute(const std::string& address, const SmartContractBinary& smartContractBinary,
        std::vector<MethodHeader>& methodHeader, bool isGetter, cs::Sequence explicit_sequence);

    bool connect() {
        try {
            executorTransport_->open();
        }
        catch (...) {
            notifyError();
        }

        return executorTransport_->isOpen();
    }

    void disconnect() {
        try {
            executorTransport_->close();
        }
        catch (::apache::thrift::transport::TTransportException&) {
            notifyError();
        }
    }

    void notifyError() {
        if (isConnected()) {
            disconnect();
        }

        cvErrorConnect_.notify_one();
    }

    //
    using OriginExecutor = executor::ContractExecutorConcurrentClient;
    using BinaryProtocol = apache::thrift::protocol::TBinaryProtocol;
    std::shared_mutex sharedErrorMutex_;

    void recreateOriginExecutor() {
        std::lock_guard lock(sharedErrorMutex_);
        disconnect();
        origExecutor_.reset(new OriginExecutor(::apache::thrift::stdcxx::make_shared<BinaryProtocol>(executorTransport_)));
    }
    //

private:
    const BlockChain& blockchain_;
    const cs::SolverCore& solver_;
    const Config& config_;

    ::apache::thrift::stdcxx::shared_ptr<::apache::thrift::transport::TSocket> socket_;
    ::apache::thrift::stdcxx::shared_ptr<::apache::thrift::transport::TTransport> executorTransport_;

    std::unique_ptr<executor::ContractExecutorConcurrentClient> origExecutor_;
    std::unique_ptr<cs::Process> executorProcess_;

    general::AccessID lastAccessId_{};
    std::map<general::AccessID, cs::Sequence> accessSequence_;
    std::map<csdb::Address, csdb::TransactionID> deployTrxns_;
    std::map<csdb::Address, std::string> lastState_;
    std::map<csdb::Address, std::unordered_map<cs::Sequence, std::string>> cacheLastStates_;
    std::map<general::AccessID, std::vector<csdb::Transaction>> innerSendTransactions_;

    std::shared_mutex mutex_;
    std::atomic_size_t execCount_{0};

    std::condition_variable cvErrorConnect_;
    std::atomic_bool requestStop_{ false };

    const int16_t EXECUTOR_VERSION = 2;

    // temporary solution?
    std::mutex callExecutorLock_;
};
}  // namespace executor
namespace apiexec {
class APIEXECHandler : public APIEXECNull, public APIHandlerBase {
public:
    explicit APIEXECHandler(BlockChain& blockchain, cs::SolverCore& _solver, executor::Executor& executor, const Config& config);
    APIEXECHandler(const APIEXECHandler&) = delete;
    void GetSeed(apiexec::GetSeedResult& _return, const general::AccessID accessId) override;
    void SendTransaction(apiexec::SendTransactionResult& _return, const general::AccessID accessId, const api::Transaction& transaction) override;
    void WalletIdGet(api::WalletIdGetResult& _return, const general::AccessID accessId, const general::Address& address) override;
    void SmartContractGet(SmartContractGetResult& _return, const general::AccessID accessId, const general::Address& address) override;
    void WalletBalanceGet(api::WalletBalanceGetResult& _return, const general::Address& address) override;
    void PoolGet(PoolGetResult& _return, const int64_t sequence) override;

    executor::Executor& getExecutor() const {
        return executor_;
    }

private:
    executor::Executor& executor_;
    BlockChain& blockchain_;
    cs::SolverCore& solver_;
};
}  // namespace apiexec

namespace api {
class APIFaker : public APINull {
public:
    APIFaker(BlockChain&, cs::SolverCore&) {
    }
};

class APIHandler : public APIHandlerInterface {
public:
    explicit APIHandler(BlockChain& blockchain, cs::SolverCore& _solver, executor::Executor& executor, const Config& config);
    ~APIHandler() override;

    APIHandler(const APIHandler&) = delete;

    void WalletDataGet(api::WalletDataGetResult& _return, const general::Address& address) override;
    void WalletIdGet(api::WalletIdGetResult& _return, const general::Address& address) override;
    void WalletTransactionsCountGet(api::WalletTransactionsCountGetResult& _return, const general::Address& address) override;
    void WalletBalanceGet(api::WalletBalanceGetResult& _return, const general::Address& address) override;

    void TransactionGet(api::TransactionGetResult& _return, const api::TransactionId& transactionId) override;
    void TransactionsGet(api::TransactionsGetResult& _return, const general::Address& address, const int64_t offset, const int64_t limit) override;
    void TransactionFlow(api::TransactionFlowResult& _return, const api::Transaction& transaction) override;

    // Get list of pools from last one (head pool) to the first one.
    void PoolListGet(api::PoolListGetResult& _return, const int64_t offset, const int64_t limit) override;

    // Get pool info by pool hash. Starts looking from last one (head pool).
    void PoolInfoGet(api::PoolInfoGetResult& _return, const int64_t sequence, const int64_t index) override;
    void PoolTransactionsGet(api::PoolTransactionsGetResult& _return, const int64_t sequence, const int64_t offset, const int64_t limit) override;
    void StatsGet(api::StatsGetResult& _return) override;

    void SmartContractGet(api::SmartContractGetResult& _return, const general::Address& address) override;

    void SmartContractsListGet(api::SmartContractsListGetResult& _return, const general::Address& deployer) override;

    void SmartContractAddressesListGet(api::SmartContractAddressesListGetResult& _return, const general::Address& deployer) override;

    void GetLastHash(api::PoolHash& _return) override;
    void PoolListGetStable(api::PoolListGetResult& _return, const int64_t sequence, const int64_t limit) override;

    void WaitForSmartTransaction(api::TransactionId& _return, const general::Address& smart_public) override;

    void SmartContractsAllListGet(api::SmartContractsListGetResult& _return, const int64_t offset, const int64_t limit) override;

    void WaitForBlock(PoolHash& _return, const PoolHash& obsolete) override;

    void SmartMethodParamsGet(SmartMethodParamsGetResult& _return, const general::Address& address, const int64_t id) override;

    void TransactionsStateGet(TransactionsStateGetResult& _return, const general::Address& address, const std::vector<int64_t>& v) override;

    void ContractAllMethodsGet(ContractAllMethodsGetResult& _return, const std::vector<::general::ByteCodeObject>& byteCodeObjects) override;

    void ExecuteCountGet(ExecuteCountGetResult& _return, const std::string& executeMethod) override;
    ////////new
    void iterateOverTokenTransactions(const csdb::Address&, const std::function<bool(const csdb::Pool&, const csdb::Transaction&)>);
    ////////new
    api::SmartContractInvocation getSmartContract(const csdb::Address&, bool&);
    std::vector<general::ByteCodeObject> getSmartByteCode(const csdb::Address&, bool&);
    void SmartContractDataGet(api::SmartContractDataResult&, const general::Address&) override;
    void SmartContractCompile(api::SmartContractCompileResult&, const std::string&) override;

    void TokenBalancesGet(api::TokenBalancesResult&, const general::Address&) override;
    void TokenTransfersGet(api::TokenTransfersResult&, const general::Address& token, int64_t offset, int64_t limit) override;
    void TokenTransferGet(api::TokenTransfersResult& _return, const general::Address& token, const TransactionId& id) override;
    void TokenWalletTransfersGet(api::TokenTransfersResult&, const general::Address& token, const general::Address& address, int64_t offset, int64_t limit) override;
    void TokenTransactionsGet(api::TokenTransactionsResult&, const general::Address&, int64_t offset, int64_t limit) override;
    void TokenInfoGet(api::TokenInfoResult&, const general::Address&) override;
    void TokenHoldersGet(api::TokenHoldersResult&, const general::Address&, int64_t offset, int64_t limit, const TokenHoldersSortField order, const bool desc) override;
    void TokensListGet(api::TokensListResult&, int64_t offset, int64_t limit, const TokensListSortField order, const bool desc, const TokenFilters& filters) override;
    void TokenTransfersListGet(api::TokenTransfersResult&, int64_t offset, int64_t limit) override;
    void TransactionsListGet(api::TransactionsGetResult&, int64_t offset, int64_t limit) override;
    void WalletsGet(api::WalletsGetResult& _return, int64_t offset, int64_t limit, int8_t ordCol, bool desc) override;
    void TrustedGet(api::TrustedGetResult& _return, int32_t page) override;
    ////////new

    void SyncStateGet(api::SyncStateResult& _return) override;

    BlockChain& get_s_blockchain() const noexcept {
        return s_blockchain;
    }

    executor::Executor& getExecutor() {
        return executor_;
    }

    bool isBDLoaded() { return isBDLoaded_; }
    
private:
    ::csstats::AllStats stats_;
    executor::Executor& executor_;

    bool isBDLoaded_{ false };

    struct smart_trxns_queue {
        cs::SpinLock lock{ATOMIC_FLAG_INIT};
        std::condition_variable_any new_trxn_cv{};
        size_t awaiter_num{0};
        std::deque<csdb::TransactionID> trid_queue{};
    };

    struct PendingSmartTransactions {
        std::queue<std::pair<cs::Sequence, csdb::Transaction>> queue;
        csdb::PoolHash last_pull_hash{};
        cs::Sequence last_pull_sequence = 0;
    };

    struct HashState {
        cs::Hash hash;
        std::string retVal;
        bool isOld{false};
        bool condFlg{false};
    };

    using client_type           = executor::ContractExecutorConcurrentClient;
    using smartHashStateEntry   = cs::WorkerQueue<HashState>;
   
    BlockChain& s_blockchain;
    cs::SolverCore& solver;
#ifdef MONITOR_NODE
    csstats::csstats stats;
#endif

    struct SmartOperation {
        enum class State : uint8_t {
            Pending,
            Success,
            Failed
        };

        State state = State::Pending;
        csdb::TransactionID stateTransaction;

        bool hasRetval : 1;
        bool returnsBool : 1;
        bool boolResult : 1;

        SmartOperation()
        : hasRetval(false)
        , returnsBool(false) {
        }
        SmartOperation(const SmartOperation& rhs)
        : state(rhs.state)
        , stateTransaction(rhs.stateTransaction.clone())
        , hasRetval(rhs.hasRetval)
        , returnsBool(rhs.returnsBool)
        , boolResult(rhs.boolResult) {
        }

        // SmartOperation(SmartOperation&&) = delete; //not compiled!? (will not be called because there is "SmartOperation (const SmartOperation & rhs)")
        SmartOperation& operator=(const SmartOperation&) = delete;
        SmartOperation& operator=(SmartOperation&&) = delete;

        bool hasReturnValue() const {
            return hasRetval;
        }
        bool getReturnedBool() const {
            return returnsBool && boolResult;
        }
    };

    SmartOperation getSmartStatus(const csdb::TransactionID);

    cs::SpinLockable<std::map<csdb::TransactionID, SmartOperation>> smart_operations;
    cs::SpinLockable<std::map<cs::Sequence, std::vector<csdb::TransactionID>>> smarts_pending;

    cs::SpinLockable<std::map<csdb::Address, csdb::TransactionID>> smart_origin;
    cs::SpinLockable<std::map<csdb::Address, smart_trxns_queue>> smart_last_trxn;

    cs::SpinLockable<std::map<csdb::Address, smartHashStateEntry>> hashStateSL;

    cs::SpinLockable<std::map<csdb::Address, std::vector<csdb::TransactionID>>> deployed_by_creator;
    //cs::SpinLockable<PendingSmartTransactions> pending_smart_transactions;
    cs::SpinLockable < std::map<cs::Sequence, api::Pool> > poolCache;
    std::atomic_flag state_updater_running = ATOMIC_FLAG_INIT;
    std::thread state_updater;

    std::map<std::string, int64_t> mExecuteCount_;

    api::SmartContract fetch_smart_body(const csdb::Transaction&);

private:
    //void state_updater_work_function();

    std::vector<api::SealedTransaction> extractTransactions(const csdb::Pool& pool, int64_t limit, const int64_t offset);

    api::SealedTransaction convertTransaction(const csdb::Transaction& transaction);

    std::vector<api::SealedTransaction> convertTransactions(const std::vector<csdb::Transaction>& transactions);

    api::Pool convertPool(const csdb::Pool& pool);

    api::Pool convertPool(const csdb::PoolHash& poolHash);

    // bool convertAddrToPublicKey(const csdb::Address& address);

    template <typename Mapper>
    size_t getMappedDeployerSmart(const csdb::Address& deployer, Mapper mapper, std::vector<decltype(mapper(api::SmartContract()))>& out);

    bool updateSmartCachesTransaction(csdb::Transaction trxn, cs::Sequence sequence);

    void run();

    ::csdb::Transaction make_transaction(const ::api::Transaction&);
    void dumb_transaction_flow(api::TransactionFlowResult& _return, const ::api::Transaction&);
    void smart_transaction_flow(api::TransactionFlowResult& _return, const ::api::Transaction&);

    std::optional<std::string> checkTransaction(const ::api::Transaction&);

    TokensMaster tm_;

    const uint8_t ERROR_CODE = 1;

    friend class ::csconnector::connector;

    std::condition_variable_any newBlockCv_;
    std::mutex dbLock_;

private slots:
    void updateSmartCachesPool(const csdb::Pool& pool);
    void store_block_slot(const csdb::Pool& pool);
    void collect_all_stats_slot(const csdb::Pool& pool);
};
}  // namespace api

bool is_deploy_transaction(const csdb::Transaction& tr);
bool is_smart(const csdb::Transaction& tr);
bool is_smart_state(const csdb::Transaction& tr);
bool is_smart_deploy(const api::SmartContractInvocation& smart);

#endif  // APIHANDLER_HPP
