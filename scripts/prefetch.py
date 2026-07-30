#!/usr/bin/env python3
"""Prefetch verified keyless data + headlines for the morning brief.

Runs in a GitHub Action (unrestricted network) before the 8am ET cloud
routine, which is egress-limited and can only read this repo + web search.
Writes data/latest.json and a dated archive copy. Every source is wrapped
so one failure never kills the file; failures land in "errors" so the
routine knows what to backfill via web search.
"""
import datetime as dt
import email.utils
import json
import os
import re
import sys

import feedparser
import requests
from curl_cffi import requests as crequests

UA = {"User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36"}
NOW = dt.datetime.now(dt.timezone.utc)
out = {"fetched_at_utc": NOW.isoformat(timespec="seconds"), "errors": []}

def attempt(name, fn):
    try:
        return fn()
    except Exception as e:  # noqa: BLE001 - degrade, never die
        out["errors"].append(f"{name}: {type(e).__name__}: {e}")
        return None

def get_json(url, timeout=15):
    r = requests.get(url, headers=UA, timeout=timeout)
    r.raise_for_status()
    return r.json()

# ---- market data ----
def spot():
    d = get_json("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum&vs_currencies=usd&include_24hr_change=true")
    return {"btc": d["bitcoin"]["usd"], "btc_24h_pct": round(d["bitcoin"]["usd_24h_change"], 2),
            "eth": d["ethereum"]["usd"], "eth_24h_pct": round(d["ethereum"]["usd_24h_change"], 2),
            "source": "coingecko"}

def spot_fallback():
    d = get_json("https://api.coinbase.com/v2/prices/BTC-USD/spot")
    return {"btc": float(d["data"]["amount"]), "btc_24h_pct": None,
            "eth": None, "eth_24h_pct": None, "source": "coinbase"}

out["spot"] = attempt("coingecko", spot) or attempt("coinbase", spot_fallback)

def fng():
    d = get_json("https://api.alternative.me/fng/?limit=2")["data"]
    return {"value": int(d[0]["value"]), "classification": d[0]["value_classification"],
            "yesterday": int(d[1]["value"]) if len(d) > 1 else None}

out["fear_greed"] = attempt("fear_greed", fng)

def network():
    d = get_json("https://mempool.space/api/v1/mining/hashrate/3d")
    return {"hashrate_ehs": round(d["currentHashrate"] / 1e18, 1),
            "difficulty": d["currentDifficulty"]}

out["network"] = attempt("mempool", network)

def basis():
    """Annualized basis proxy from nearest Deribit dated BTC futures vs spot."""
    if not out.get("spot"):
        raise RuntimeError("no spot price to compute basis against")
    s = out["spot"]["btc"]
    d = get_json("https://www.deribit.com/api/v2/public/get_book_summary_by_currency?currency=BTC&kind=future")
    rows = []
    for f in d["result"]:
        m = re.fullmatch(r"BTC-(\d{1,2})([A-Z]{3})(\d{2})", f["instrument_name"])
        if not m or not f.get("mark_price"):
            continue
        exp = dt.datetime.strptime(m.group(0)[4:], "%d%b%y").replace(tzinfo=dt.timezone.utc)
        days = (exp - NOW).days
        if days < 5:  # too close to expiry, annualization explodes
            continue
        rows.append({"instrument": f["instrument_name"], "days": days,
                     "ann_basis_pct": round((f["mark_price"] / s - 1) * 365 / days * 100, 2)})
    rows.sort(key=lambda r: r["days"])
    return rows[:2]

out["futures_basis"] = attempt("deribit_basis", basis)

def dgs10():
    r = requests.get("https://fred.stlouisfed.org/graph/fredgraph.csv?id=DGS10", headers=UA, timeout=15)
    r.raise_for_status()
    last = [l for l in r.text.strip().splitlines() if not l.endswith(".")][-1]
    date, val = last.split(",")
    return {"date": date, "yield_pct": float(val)}

out["us10y"] = attempt("fred_dgs10", dgs10)

def etf_flows():
    """Farside is Cloudflare-protected; browser impersonation worked from a
    residential IP but is unverified from datacenter IPs. Grab the page text
    and let the (LLM) routine read the table; degrade silently if blocked."""
    r = crequests.get("https://farside.co.uk/btc/", impersonate="chrome", timeout=25)
    if r.status_code != 200:
        raise RuntimeError(f"HTTP {r.status_code}")
    text = re.sub(r"<[^>]+>", " ", r.text)
    text = re.sub(r"\s+", " ", text)
    # keep the tail of the table: recent dates + totals live near the end
    return text[-4000:]

out["etf_flows_raw"] = attempt("farside", etf_flows)

# ---- headlines ----
FEEDS = {
    "CoinDesk": "https://www.coindesk.com/arc/outboundfeeds/rss/",
    "Cointelegraph": "https://cointelegraph.com/rss/tag/bitcoin",
    "TheBlock": "https://www.theblock.co/rss.xml",
    "Decrypt": "https://decrypt.co/feed",
    "BitcoinMagazine": "https://bitcoinmagazine.com/feed",
    "Blockworks": "https://blockworks.co/feed",
    "CNBC": "https://www.cnbc.com/id/100003114/device/rss/rss.html",
    "CNBC-Econ": "https://www.cnbc.com/id/20910258/device/rss/rss.html",
    "MarketWatch": "https://feeds.content.dowjones.io/public/rss/mw_topstories",
    "GoogleNews-BTC": "https://news.google.com/rss/search?q=bitcoin+OR+%22crypto+regulation%22&hl=en-US&gl=US&ceid=US:en",
}
CUTOFF = NOW - dt.timedelta(hours=36)

def parse_when(entry):
    for k in ("published", "updated"):
        if entry.get(k):
            try:
                return email.utils.parsedate_to_datetime(entry[k])
            except (TypeError, ValueError):
                try:
                    return dt.datetime.fromisoformat(entry[k].replace("Z", "+00:00"))
                except ValueError:
                    pass
    return None

headlines = []
for src, url in FEEDS.items():
    def pull(src=src, url=url):
        r = requests.get(url, headers=UA, timeout=20)
        r.raise_for_status()
        items = []
        for e in feedparser.parse(r.content).entries:
            when = parse_when(e)
            if when and when.tzinfo is None:
                when = when.replace(tzinfo=dt.timezone.utc)
            if when and when < CUTOFF:
                continue
            items.append({"source": src, "title": e.get("title", "").strip(),
                          "link": e.get("link"), "published": when.isoformat(timespec="minutes") if when else None})
        items.sort(key=lambda i: i["published"] or "", reverse=True)
        return items[:8]
    got = attempt(f"feed:{src}", pull)
    if got:
        headlines.extend(got)

out["headlines"] = headlines

# ---- write ----
os.makedirs("data", exist_ok=True)
today_et = dt.datetime.now(dt.timezone(dt.timedelta(hours=-4))).strftime("%Y-%m-%d")
for path in ("data/latest.json", f"data/{today_et}.json"):
    with open(path, "w") as f:
        json.dump(out, f, indent=1, default=str)
print(f"wrote {len(headlines)} headlines, errors: {out['errors'] or 'none'}")
sys.exit(0)
