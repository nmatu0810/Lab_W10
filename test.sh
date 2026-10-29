#!/bin/sh
# ./test.sh [port]

PORT=${1:-8080}
URL="http://localhost:$PORT"
PASS=0
FAIL=0

check() {
	desc=$1; want=$2; shift 2
	got=$(curl -s -o /dev/null -w '%{http_code}' --http1.0 "$@")
	if [ "$got" = "$want" ]; then
		PASS=$((PASS + 1)); echo "PASS  $desc"
	else
		FAIL=$((FAIL + 1)); echo "FAIL  $desc ($want != $got)"
	fi
}

check "GET"        200 "$URL/index.html"
check "HEAD"       200 -I "$URL/index.html"
check "404"        404 "$URL/abc.html"
check "../"        403 --path-as-is "$URL/../../etc/passwd"
check "dir"        200 "$URL/"
check "304"        304 -H "If-Modified-Since: Fri, 01 Jan 2100 00:00:00 GMT" "$URL/index.html"
check "POST"       501 -X POST "$URL/index.html"

echo "$PASS pass, $FAIL fail"
[ "$FAIL" -eq 0 ]
