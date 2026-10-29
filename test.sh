#!/bin/sh
# ./test.sh [port] [docroot]
# chay server truoc: ./sws -d -p 8080 -c cgi-bin www

PORT=${1:-8080}
DOCROOT=${2:-www}
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

# body co chua chuoi $2 khong
check_body() {
	desc=$1; want=$2; shift 2
	if curl -s --http1.0 "$@" | grep -q "$want"; then
		PASS=$((PASS + 1)); echo "PASS  $desc"
	else
		FAIL=$((FAIL + 1)); echo "FAIL  $desc (khong thay '$want')"
	fi
}

check "GET"        200 "$URL/index.html"
check "HEAD"       200 -I "$URL/index.html"
check "404"        404 "$URL/abc.html"
check "../"        403 --path-as-is "$URL/../../etc/passwd"
check "dir"        200 "$URL/"
check "304"        304 -H "If-Modified-Since: Fri, 01 Jan 2100 00:00:00 GMT" "$URL/index.html"
check "IMS cu"     200 -H "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT" "$URL/index.html"
check "POST"       501 -X POST "$URL/index.html"

# file khong co quyen doc
touch "$DOCROOT/.noperm.html" && chmod 000 "$DOCROOT/.noperm.html"
check "403 file"   403 "$URL/.noperm.html"
rm -f "$DOCROOT/.noperm.html"

# CGI
check      "cgi"           200 "$URL/cgi-bin/hello.cgi"
check_body "cgi query"     "query: a=1" "$URL/cgi-bin/hello.cgi?a=1"
check_body "cgi env"       "REQUEST_METHOD=GET" "$URL/cgi-bin/env.cgi"
check_body "cgi path_info" "PATH_INFO=/x/y" "$URL/cgi-bin/env.cgi/x/y"
check      "cgi HEAD"      200 -I "$URL/cgi-bin/hello.cgi"
check      "cgi 404"       404 "$URL/cgi-bin/nothere.cgi"
check      "cgi noexec"    403 "$URL/cgi-bin/noexec.cgi"
check      "cgi ../"       403 --path-as-is "$URL/cgi-bin/../../etc/passwd"
check      "cgi loi"       500 "$URL/cgi-bin/bad.cgi"

# ~user, chi test khi co ~/sws/index.html
if [ -f "$HOME/sws/index.html" ]; then
	check "~user"      200 "$URL/~$(id -un)/index.html"
else
	echo "SKIP  ~user (khong co $HOME/sws/index.html)"
fi
check "~user khong ton tai" 404 "$URL/~khongcouser123/"

echo "$PASS pass, $FAIL fail"
[ "$FAIL" -eq 0 ]
