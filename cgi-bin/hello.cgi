#!/bin/sh
echo "Content-Type: text/html"
echo
echo "<html><body><h1>Hello CGI</h1><p>query: $QUERY_STRING</p></body></html>"
