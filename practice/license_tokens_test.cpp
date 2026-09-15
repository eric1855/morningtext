#include "license_tokens.cpp"
#include <iostream>
#include <random>

static int failures = 0;
void check(const string& name, vector<int> got, vector<int> want) {
    bool ok = got == want;
    if (!ok) ++failures;
    cout << (ok ? "PASS " : "FAIL ") << name << " got=[";
    for (size_t i = 0; i < got.size(); ++i) cout << (i ? "," : "") << got[i];
    cout << "] want=[";
    for (size_t i = 0; i < want.size(); ++i) cout << (i ? "," : "") << want[i];
    cout << "]\n";
}

// O(q * tokens) reference implementation.
vector<int> brute(int ttl, vector<string> queries) {
    vector<pair<string,long long>> toks;
    vector<int> out;
    for (auto& q : queries) {
        istringstream iss(q); string op; iss >> op;
        if (op == "count") { long long t; iss >> t; int c = 0;
            for (auto& p : toks) if (p.second > t) ++c;
            out.push_back(c); continue; }
        string id; long long t; iss >> id >> t;
        if (op == "generate") toks.push_back({id, t + ttl});
        else for (auto& p : toks) if (p.first == id && p.second > t) p.second = t + ttl;
    }
    return out;
}

int main() {
    check("sample", getUnexpiredTokens(5, {"generate aaa 1","renew aaa 2","count 6",
        "generate bbb 7","renew aaa 8","renew bbb 10","count 15"}), {1, 0});
    check("expiry is exclusive", getUnexpiredTokens(5, {"generate a 1","count 5","count 6"}), {1, 0});
    check("renew of unknown id", getUnexpiredTokens(3, {"renew zzz 1","count 1"}), {0});
    check("renew at expiry instant is ignored",
        getUnexpiredTokens(4, {"generate a 1","renew a 5","count 5","count 6"}), {0, 0});
    check("count with no tokens", getUnexpiredTokens(1, {"count 1"}), {0});
    check("multiple live tokens",
        getUnexpiredTokens(10, {"generate a 1","generate b 2","generate c 3","count 5",
            "count 11","count 12","count 13"}), {3, 2, 1, 0});
    check("repeated renewals keep a token alive",
        getUnexpiredTokens(2, {"generate a 1","renew a 2","renew a 3","renew a 4","count 5",
            "count 6"}), {1, 0});
    check("max magnitudes", getUnexpiredTokens(100000000,
        {"generate a 100000000","count 100000000","count 199999999"}), {1, 1});

    // Randomized comparison against the brute-force reference.
    mt19937 rng(12345);
    for (int trial = 0; trial < 300; ++trial) {
        int ttl = 1 + (int)(rng() % 6);
        vector<string> qs; long long t = 1; int next_id = 0;
        for (int i = 0; i < 60; ++i) {
            t += rng() % 3;
            int roll = rng() % 3;
            if (roll == 0) qs.push_back("generate t" + to_string(next_id++) + " " + to_string(t));
            else if (roll == 1 && next_id)
                qs.push_back("renew t" + to_string(rng() % next_id) + " " + to_string(t));
            else qs.push_back("count " + to_string(t));
        }
        if (getUnexpiredTokens(ttl, qs) != brute(ttl, qs)) {
            cout << "FAIL random trial " << trial << "\n"; ++failures; break;
        }
    }
    if (!failures) cout << "PASS randomized vs brute force (300 trials)\n";

    // Scale check at the constraint ceiling.
    vector<string> big; big.reserve(100000);
    for (int i = 0; i < 50000; ++i) big.push_back("generate k" + to_string(i) + " " + to_string(i + 1));
    for (int i = 0; i < 50000; ++i) big.push_back("count " + to_string(50000 + i));
    auto res = getUnexpiredTokens(100000, big);
    cout << (res.size() == 50000 && res.front() == 50000 && res.back() == 50000
             ? "PASS 100k queries\n" : "FAIL 100k queries\n");
    if (!(res.size() == 50000 && res.front() == 50000 && res.back() == 50000)) ++failures;

    cout << (failures ? "FAILURES: " + to_string(failures) : "ALL TESTS PASSED") << "\n";
    return failures ? 1 : 0;
}
