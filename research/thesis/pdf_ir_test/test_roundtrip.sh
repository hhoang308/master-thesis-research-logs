pass=0; fail=0; count=0
# Seed corpus lives at research/seeds/full (anchored to this script's location,
# so it works regardless of cwd or checkout path).
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
for f in "$SCRIPT_DIR"/../../seeds/full/*.pdf; do
    if [ $count -ge 40 ]; then break; fi
    ((count++))
    result=$(cargo run --quiet -- "$f" 2>&1)
    if echo "$result" | grep -q "Round-trip OK"; then
        ((pass++))
        echo "PASS: $(basename $f)"
    else
        ((fail++))
        # print last line of output to see error reason
        reason=$(echo "$result" | tail -1)
        echo "FAIL: $(basename $f) -> $reason"
    fi
done
echo "Result: $pass pass / $fail fail out of $count files"