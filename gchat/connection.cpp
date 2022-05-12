#include "connection.h"

size_t connection::mystrlcat(char *dst, const char *src, size_t size) {
  const char *ps;
  char *pd, *pde;
  size_t dlen, lest;

  for (pd = dst, lest = size; *pd != '\0' && lest != 0; pd++, lest--)
    ;

  dlen = pd - dst;
  if (size - dlen == 0) {
    return (dlen + strlen(src));
  }
  pde = dst + size - 1;
  for (ps = src; *ps != '\0' && pd < pde; pd++, ps++) {
    *pd = *ps;
  }
  for (; pd <= pde; pd++) {
    *pd = '\0';
  }
  while (*ps++)
    ;
  return (dlen + (ps - src - 1));
}

int connection::open(const char *portnm) {
  char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
  struct addrinfo hints, *res0;
  int soc, opt, errcode;
  socklen_t opt_len;

  // アドレス情報のヒントをゼロクリア

  memset(&hints, 0, sizeof(0));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  // アドレス情報の決定
  errcode = getaddrinfo(NULL, portnm, &hints, &res0);
  if (errcode != 0) {
    std::cerr << "getaddrinfo():" << gai_strerror(errcode) << std::endl;
    return -1;
  }
  errcode = getnameinfo(res0->ai_addr, res0->ai_addrlen, nbuf, sizeof(nbuf),
                        sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
  if (errcode != 0) {
    std::cerr << "getnameinfo():" << gai_strerror(errcode) << std::endl;
    freeaddrinfo(res0);
    return -1;
  }
  std::cerr << "port=" << sbuf << std::endl;

  // ソケットの生成
  soc = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol);
  if (soc == -1) {
    perror("socket");
    freeaddrinfo(res0);
    return -1;
  }

  // ソケットオプション(再利用フラグ)設定
  opt = 1;
  opt_len = sizeof(opt);
  if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, &opt, opt_len) == -1) {
    perror("setsockopt");
    close(soc);
    freeaddrinfo(res0);
    return -1;
  }

  // ソケットにアドレスを指定
  if (bind(soc, res0->ai_addr, res0->ai_addrlen) == -1) {
    perror("bind");
    close(soc);
    freeaddrinfo(res0);
    return -1;
  }

  // アクセスバックログの指定
  if (listen(soc, SOMAXCONN) == -1) {
    perror("listen");
    close(soc);
    freeaddrinfo(res0);
    return -1;
  }
  freeaddrinfo(res0);
  return soc;
}
