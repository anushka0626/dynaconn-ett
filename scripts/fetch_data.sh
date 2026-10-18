#!/usr/bin/env bash
# Downloads the external datasets into data/ (gitignored) and checks them against the
# SHA-256 sums pinned in traces/DATA_SHA256SUMS (committed).
#
#   scripts/fetch_data.sh          download what is missing, verify everything
#   scripts/fetch_data.sh --pin    first download only: record the sums of the current files
#
# Datasets (plan 6.5; SIGMOD 2025 comparability, amendment A2):
#   SNAP temporal graphs for sliding windows: CollegeMsg, email-Eu-core-temporal, sx-mathoverflow
#   DIMACS 9th challenge road network: New York (USA-road-d.NY)
#   SNAP com-YouTube (one of SIGMOD 2025's datasets)
# SIGMOD 2025's StackOverflow graph (6.3e7 edges) is not fetched: it does not fit in this
# machine's 15 GB of RAM for the levelled structures (HDT is ~580 B/edge).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
sums="$root/traces/DATA_SHA256SUMS"
mkdir -p "$root/traces"
pin=0
[ "${1:-}" = "--pin" ] && pin=1

fetch() {  # url, destination relative to data/
    local url="$1" dest="$root/data/$2"
    mkdir -p "$(dirname "$dest")"
    if [ ! -f "$dest" ]; then
        echo "fetching $2"
        curl -fsSL "$url" | gunzip > "$dest.part"
        mv "$dest.part" "$dest"
    fi
}

fetch https://snap.stanford.edu/data/CollegeMsg.txt.gz snap/CollegeMsg.txt
fetch https://snap.stanford.edu/data/email-Eu-core-temporal.txt.gz snap/email-Eu-core-temporal.txt
fetch https://snap.stanford.edu/data/sx-mathoverflow.txt.gz snap/sx-mathoverflow.txt
fetch https://snap.stanford.edu/data/bigdata/communities/com-youtube.ungraph.txt.gz snap/com-youtube.ungraph.txt
fetch http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.NY.gr.gz dimacs/USA-road-d.NY.gr

cd "$root/data"
files=(snap/CollegeMsg.txt snap/email-Eu-core-temporal.txt snap/sx-mathoverflow.txt snap/com-youtube.ungraph.txt
       dimacs/USA-road-d.NY.gr)
if [ "$pin" = 1 ]; then
    sha256sum "${files[@]}" > "$sums"
    echo "pinned sums in traces/DATA_SHA256SUMS"
fi
sha256sum -c "$sums"
