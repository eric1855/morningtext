// Adobe ID short-lived license tokens.
//
// A token issued at time t is live until t + time_to_live, exclusive: a token
// whose expiration time equals the current time is already expired and can be
// neither renewed nor counted.
//
//   generate <token_id> <current_time>  issue a new token (ids are unique)
//   renew    <token_id> <current_time>  extend a live token; ignored otherwise
//   count    <current_time>             number of live tokens
//
// current_time is non-decreasing across queries, so expired tokens can be
// retired lazily from a min-heap keyed on expiration time. Each query does
// O(log q) amortized work: every generate/renew pushes exactly one heap entry
// and each entry is popped at most once.

#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

vector<int> getUnexpiredTokens(int time_to_live, vector<string> queries) {
    using Entry = pair<long long, string>;  // (expires_at, token_id)

    unordered_map<string, long long> expires_at;
    priority_queue<Entry, vector<Entry>, greater<Entry>> by_expiry;
    long long live = 0;

    // Retire every token whose expiration time has been reached; heap entries
    // superseded by a renewal are dropped without touching the live count.
    auto expire_through = [&](long long now) {
        while (!by_expiry.empty() && by_expiry.top().first <= now) {
            auto [expiry, id] = by_expiry.top();
            by_expiry.pop();
            auto it = expires_at.find(id);
            if (it != expires_at.end() && it->second == expiry) {
                expires_at.erase(it);
                --live;
            }
        }
    };

    vector<int> results;
    for (const string& query : queries) {
        istringstream iss(query);
        string op;
        iss >> op;

        if (op == "count") {
            long long now;
            iss >> now;
            expire_through(now);
            results.push_back(static_cast<int>(live));
            continue;
        }

        string id;
        long long now;
        iss >> id >> now;
        expire_through(now);

        long long expiry = now + time_to_live;
        if (op == "generate") {
            expires_at[id] = expiry;
            by_expiry.emplace(expiry, id);
            ++live;
        } else if (op == "renew") {
            auto it = expires_at.find(id);
            if (it != expires_at.end()) {  // expired ids were just retired
                it->second = expiry;
                by_expiry.emplace(expiry, id);
            }
        }
    }
    return results;
}
