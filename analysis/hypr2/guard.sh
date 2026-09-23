#!/bin/zsh
# wait until no file in captures/ modified in the last 90 s
D="$(cd "$(dirname "$0")/../.." && pwd)/captures"
while true; do
  now=$(date +%s); newest=$(stat -f %m $D/* | sort -n | tail -1)
  age=$((now-newest))
  if [ $age -ge 90 ]; then break; fi
  sleep $((91-age))
done
