#include <csnode/roundstat.hpp>
#include <lib/system/logger.hpp>
#include <sstream>

namespace cs {
RoundStat::RoundStat()
: totalReceivedTransactions_(0)
, totalAcceptedTransactions_(0)
, deferredTransactionsCount_(0)
, totalDurationMs_(0)
, node_start_round(0)
, start_skip_rounds(2) {
}

void RoundStat::onRoundStart(RoundNumber round, bool skip_logs) {
    // minimal statistics, skip 0 & 1 rounds because of possibility extra timeouts
    if (start_skip_rounds > 0) {
        start_skip_rounds--;
        node_start_round = round;
        startPointMs_ = std::chrono::steady_clock::now();
        totalDurationMs_ = 0;
    }
    else {
        using namespace std::chrono;
        auto new_duration_ms = duration_cast<milliseconds>(steady_clock::now() - startPointMs_).count();
        auto last_round_ms = cs::numeric_cast<size_t>(new_duration_ms) - totalDurationMs_;
        totalDurationMs_ = cs::numeric_cast<size_t>(new_duration_ms);
        size_t cnt_r = 1;
        if (round > node_start_round) {
            cnt_r = round - node_start_round;
        }
        ave_round_ms = totalDurationMs_ / cnt_r;

        // shortest_rounds.insert(last_round_ms);
        // longest_rounds.insert(last_round_ms);

        // TODO: use more intelligent output formatting
        if (!skip_logs) {
            std::ostringstream os;
            constexpr size_t in_minutes = 5 * 60 * 1000;
            constexpr size_t in_seconds = 10 * 1000;

            os << " last round ";

            if (last_round_ms > in_minutes) {
                os << "> " << last_round_ms / 60000 << "min";
            }
            else if (last_round_ms > in_seconds) {
                os << "> " << last_round_ms / 1000 << "sec";
            }
            else {
                os << last_round_ms << "ms";
            }

            os << ", average round ";

            if (ave_round_ms > in_seconds) {
                os << "> " << ave_round_ms / 1000 << "sec";
            }
            else {
                os << ave_round_ms << "ms";
            }

            os << ", "
                //<< totalReceivedTransactions_ << " viewed transactions, "
                << WithDelimiters(totalAcceptedTransactions_) << " stored transactions.";
            cslog() << os.str();
        }
    }
}

void RoundStat::onReadBlock(csdb::Pool block, bool* /*should_stop*/) {
    totalAcceptedTransactions_ += block.transactions_count();
}

void RoundStat::onStoreBlock(csdb::Pool block) {
    totalAcceptedTransactions_ += block.transactions_count();
}

size_t RoundStat::getAveTime() {
    return ave_round_ms;
}

size_t RoundStat::getNodeStartRound() {
	return node_start_round;
}


}  // namespace cs
