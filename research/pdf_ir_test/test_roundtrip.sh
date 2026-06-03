pass=0; fail=0; count=0
for f in /home/parkle/master-thesis-research-logs/research/pdf-seeds/*.pdf; do
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