// Ordered-map variant: parse each query into a Triple, then keep tokens in a
// set ordered by expiration time so the expired ones are always at the front.
//
// Triple and parseQuery must be defined ABOVE getUnexpiredTokens or it will not
// compile — a function can only use a type declared before it.

#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

struct Triple {
    string op;    // "generate", "renew", or "count"
    string key;   // token_id, or empty for "count"
    long long time;  // current_time
};

Triple parseQuery(const string& line) {
    istringstream iss(line);
    Triple t;
    t.key.clear();
    t.time = 0;

    iss >> t.op;
    if (t.op == "count") {
        iss >> t.time;
    } else {
        iss >> t.key >> t.time;
    }
    return t;
}

vector<int> getUnexpiredTokens(int time_to_live, vector<string> queries) {
    // Ordered by (expires_at, token_id) — the id breaks ties so that two tokens
    // expiring at the same instant are distinct entries.
    set<pair<long long, string>> by_expiry;
    unordered_map<string, long long> expires_at;  // token_id -> its entry's key

    // Expiration is exclusive: a token expiring exactly now is already dead,
    // hence <=. Run this before every op, so "in the map" means "live".
    auto purge = [&](long long now) {
        while (!by_expiry.empty() && by_expiry.begin()->first <= now) {
            expires_at.erase(by_expiry.begin()->second);
            by_expiry.erase(by_expiry.begin());
        }
    };

    vector<int> results;
    for (const string& query : queries) {
        Triple t = parseQuery(query);
        purge(t.time);

        if (t.op == "count") {
            results.push_back(static_cast<int>(by_expiry.size()));
        } else if (t.op == "generate") {
            long long expiry = t.time + time_to_live;
            expires_at[t.key] = expiry;
            by_expiry.insert({expiry, t.key});
        } else if (t.op == "renew") {
            auto it = expires_at.find(t.key);
            if (it != expires_at.end()) {  // expired ids were dropped by purge
                by_expiry.erase({it->second, t.key});
                long long expiry = t.time + time_to_live;  // NOT old expiry + ttl
                it->second = expiry;
                by_expiry.insert({expiry, t.key});
            }
        }
    }
    return results;
}
