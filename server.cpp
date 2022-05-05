#include <asm-generic/errno-base.h>
#include <asm-generic/socket.h>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ucontext.h>
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

void sendRecvLoop(int acc);
std::string api(const char *name);

// todo: シグナルの捕捉を追加する
int serverSocket(const char *portnm) {
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
  fprintf(stderr, "port=%s\n", sbuf);

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

// アクセプトループ
void acceptLoop(int soc) {
  char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
  struct sockaddr_storage from;
  int acc;
  socklen_t len;
  while (1) {
    len = (socklen_t)sizeof(from);
    // 接続受付
    acc = accept(soc, (struct sockaddr *)&from, &len);
    if (acc == -1) {
      if (errno != EINTR) {
        perror("accept");
      }
    } else {
      getnameinfo((struct sockaddr *)&from, len, hbuf, sizeof(hbuf), sbuf,
                  sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
      fprintf(stderr, "accept:%s:%s\n", hbuf, sbuf);
      //送受信ループ
      sendRecvLoop(acc);
      // アクセプトソケットクローズ
      close(acc);
      acc = 0;
    }
  }
}

size_t mystrlcat(char *dst, const char *src, size_t size) {
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

void sendRecvLoop(int acc) {
  char buf[512], *ptr;
  ssize_t len;
  std::string result = "";

  // todo: ファイルが開けなかったときのエラー処理を入れる
  std::ofstream logData("chat.log");

  for (;;) {
    // 受信
    /***
        ブロッキングモードの場合
        受信するまで recv は戻ってこないため、受信があるまでは関数が返らない
     ***/
    len = recv(acc, buf, sizeof(buf), 0);
    if (len == -1) {
      // エラー
      perror("recv");
      break;
    }
    if (len == 0) {
      // エンド・オブ・ファイル
      fprintf(stderr, "recv:EOF\n");
      logData << "recv:EOF" << std::endl;
      break;
    }

    // 文字列化・表示
    buf[len] = '\0';
    ptr = strpbrk(buf, "\r\n");
    if (ptr != nullptr) {
      *ptr = '\0';
    }
    fprintf(stderr, "[client]%s\n", buf);

    // クライアントからの送信を記録
    logData << "[client]" + std::string(buf) << std::endl;

    /***
        応答文字列作成
        一般的な入門書では strcat()
    が使われるが、バッファサイズが指定できないためバッファオーバーランのバグが起きやすい
        そのため自作関数でバッファサイズを超える場合はコピーしないようにしている
    ***/
    mystrlcat(buf, ":OK\r\n", sizeof(buf));
    len = strlen(buf);

    // APIをチェック。結果を string で返す
    result = api(buf);
    mystrlcat(buf, result.c_str(), sizeof(buf));
    len = strlen(buf);

    // 応答
    len = send(acc, buf, len, 0);
    if (len == -1) {
      // エラー
      perror("send");
      break;
    }
    // サーバーの応答を記録
    logData << std::string(buf);
  }
}

std::string api(const char *name) {

  std::string str = "";
  std::string fileData = "";
  // api ではない時
  if (name[0] == '/') {
    std::cerr << "APIが叩かれました" << std::endl;

    std::ifstream logData("chat.log");
    if (!logData) {
      return "";
    }

    while (std::getline(logData, str)) {
      fileData += str;
      fileData += "\n";
      //      std::cerr << "getline の結果 = " << str.c_str() << std::endl;
    }

    //    std::cerr << "filedata を出力します\n" << fileData << std::endl;

    return fileData;
  }

  return "";
}

int main(int argc, char *argv[]) {
  int soc;
  // 引数にポート番号が指定されているか
  if (argc <= 1) {
    fprintf(stderr, "server port\n");
    return EX_USAGE;
  }
  // サーバーソケットの準備
  soc = serverSocket(argv[1]);
  if (soc == -1) {
    fprintf(stderr, "serverSocket(%s):error\n", argv[1]);
    return EX_UNAVAILABLE;
  }
  fprintf(stderr, "ready for accept\n");
  /***
      アクセプトループ
      Ctl + C などで割り込まない限りはループが止まらないので close しない
  ***/
  acceptLoop(soc);
  // ソケットクローズ
  close(soc);
  return EX_OK;
}
