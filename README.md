# Lab W10 - sws

Web server HTTP/1.0 (GET, HEAD, If-Modified-Since, CGI, ~user).

```
make
./sws -d -p 8080 -c cgi-bin www
./test.sh 8080
```

## Phan cong

| Nguoi | Branch | File |
|---|---|---|
| 1 | network | main.c, net.c |
| 2 | request | request.c, response.c |
| 3 | serve | serve.c |
| 4 | cgi | cgi.c, cgi-bin/, test.sh |

Moi nguoi tao branch rieng, chi sua file cua minh. Khong sua sws.h, Makefile khi chua bao nhom. Xong thi tao pull request vao main.

## CGI

URI `/cgi-bin/<script>[/path/info]` chay file `<dir -c>/<script>`. Script phai co quyen chay,
in header (it nhat `Content-Type`) roi dong trong roi body. Server them `HTTP/1.0 200 OK`,
`Date`, `Server`. Bien moi truong: `GATEWAY_INTERFACE`, `REQUEST_METHOD`, `QUERY_STRING`,
`SCRIPT_NAME`, `SCRIPT_FILENAME`, `PATH_INFO`, `REMOTE_ADDR`, `SERVER_NAME`, `SERVER_PORT`,
`SERVER_PROTOCOL`, `SERVER_SOFTWARE`, `DOCUMENT_ROOT`.

Loi: script khong ton tai 404, khong co quyen chay hoac co `..` 403, script khong in header 500.
