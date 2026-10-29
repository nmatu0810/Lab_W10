# Lab W10 - sws

Web server HTTP/1.0 (GET, HEAD, If-Modified-Since, CGI, ~user).

```
make
./sws -d -p 8080 www
./test.sh 8080
```

## Phan cong

| Nguoi | Branch | File |
|---|---|---|
| 1 | network | main.c, net.c |
| 2 | request | request.c, response.c |
| 3 | serve | serve.c |
| 4 | cgi | cgi.c, test.sh |

Moi nguoi tao branch rieng, chi sua file cua minh. Khong sua sws.h, Makefile khi chua bao nhom. Xong thi tao pull request vao main.
