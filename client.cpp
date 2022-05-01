#include <cstddef>
#include <cstdio>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <iostream>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

// 起動時にホスト名とサービス名を指定して接続し、文字列の送受信を行なうものである
// todo: NULL は nullptr で置き変えてよさそう

// サーバーにソケット接続

int clientSocket(const char *hostnm, const char *portnm) {
  char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
  struct addrinfo hints, *res0;
  int soc, errcode;

  // アドレス情報のヒントをゼロクリア
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  // アドレス情報の決定
  errcode = getaddrinfo(hostnm, portnm, &hints, &res0);
  if (errcode != 0) {
    fprintf(stderr, "getaddrinfo():%s\n", gai_strerror(errcode));
    return -1;
  }
  errcode = getnameinfo(res0->ai_addr, res0->ai_addrlen, nbuf, sizeof(nbuf),
                        sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
  if (errcode != 0) {
    fprintf(stderr, "getnameinfo():%s\n", gai_strerror(errcode));
    freeaddrinfo(res0);
    return -1;
  }
  // コネクトするアドレスとポートを表示
  fprintf(stderr, "addr=%s\n", nbuf);
  fprintf(stderr, "port=%s\n", sbuf);

  // ソケットの生成
  soc = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol);
  if (soc == -1) {
    perror("socket");
    freeaddrinfo(res0);
    return -1;
  }
  // コネクト
  errcode = connect(soc, res0->ai_addr, res0->ai_addrlen);
  if (errcode == -1) {
    perror("connect");
    close(soc);
    freeaddrinfo(res0);
    return -1;
  }
  freeaddrinfo(res0);

  return soc;
}
