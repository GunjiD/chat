#include <asm-generic/errno-base.h>
#include <asm-generic/socket.h>
#include <bits/types/struct_timeval.h>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string>
#include <sys/param.h>
#include <sys/select.h>
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
#include <type_traits>
#include <unistd.h>

// 最大同時接続処理
const int MAX_CHILD = 20;

int sendRecv(int acc, int child_no);
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

// アクセプトループ
void acceptLoop(int soc) {
  char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
  int child[MAX_CHILD];
  struct timeval timeout;
  struct sockaddr_storage from;
  int acc, child_no, width, i, count, pos, ret;
  socklen_t len;
  fd_set mask;

  // child 配列の初期化
  for (i = 0; i < MAX_CHILD; i++) {
    child[i] = -1;
  }
  child_no = 0;

  while (1) {
    // select() 用のマスクの作成
    FD_ZERO(&mask);
    FD_SET(soc, &mask);
    width = soc + 1;
    count = 0;
    for (i = 0; i < child_no; i++) {
      if (child[i] != -1) {
        FD_SET(child[i], &mask);
        if (child[i] + 1 > width) {
          width = child[i] + 1;
          count++;
        }
      }
    }
    std::cerr << "<<child count:" << count << ">>" << std::endl;
    // select() 用のタイムアウト値のセット
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    switch (select(width, (fd_set *)&mask, nullptr, nullptr, &timeout)) {
    case -1:
      // エラー
      perror("select");
      break;
    case 0:
      // タイムアウト
      break;
    default:
      // レディ有り
      if (FD_ISSET(soc, &mask)) {
        // サーバーソケットレディ
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
          std::cerr << "accept:" << hbuf << ":" << sbuf << std::endl;
          /***
              child の空きを検索
              空きがあれば pos に保存した値の位置に acc を代入
              空きがない場合はこれ以上格納できないため、ソケットをクローズ
           ***/
          pos = -1;
          for (i = 0; i < child_no; i++) {
            if (child[i] == -1) {
              pos = i;
              break;
            }
          }
          if (pos == -1) {
            // 空きが無い場合
            if (child_no + 1 >= MAX_CHILD) {
              // child にこれ以上格納できない
              std::cerr << "child is full : cannot accept" << std::endl;
              close(acc);
            } else {
              child_no++;
              pos = child_no - 1;
            }
          }
          if (pos != -1) {
            // child に格納
            child[pos] = acc;
          }
        }
      }
      // アクセプトしたソケットがレディ
      for (i = 0; i < child_no; i++) {
        if (child[i] != -1) {
          if (FD_ISSET(child[i], &mask)) {
            // 送受信
            ret = sendRecv(child[i], i);
            if (ret == -1) {
              /***
                  エラーまたは切断した場合クローズし、 child を空にする
               ***/
              close(child[i]);
              child[i] = -1;
            }
          }
        }
      }
      break;
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

int sendRecv(int acc, int child_no) {
  char buf[512], *ptr;
  ssize_t len;
  std::string result = "";

  // todo: ファイルが開けなかったときのエラー処理を入れる
  std::ofstream logData("chat.log");

  // 受信
  /***
      ブロッキングモードの場合
      受信するまで recv は戻ってこないため、受信があるまでは関数が返らない
   ***/
  len = recv(acc, buf, sizeof(buf), 0);
  if (len == -1) {
    // エラー
    perror("recv");
    return -1;
  }
  if (len == 0) {
    // エンド・オブ・ファイル
    std::cerr << "recv:EOF" << std::endl;
    logData << "recv:EOF" << std::endl;
    return -1;
  }

  // 文字列化・表示
  buf[len] = '\0';
  ptr = strpbrk(buf, "\r\n");
  if (ptr != nullptr) {
    *ptr = '\0';
  }
  std::cerr << "[child" << child_no << "]" << buf << std::endl;

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
    return -1;
  }
  // サーバーの応答を記録
  logData << std::string(buf);

  return 0;
}

std::string api(const char *name) {

  std::string str, fileData, tmp;
  str = "";
  fileData = "";

  tmp = std::string(name);

  // / が存在しない、かつ / が先頭にない場合は空文字を return する
  if (tmp.find("/") != std::string::npos and tmp.find("/") != 0)
    return "";

  // todo: API 単位で分離するほうが良さそう
  if (tmp.find("log") != std::string::npos) {
    std::ifstream logData("chat.log");
    if (!logData) {
      return "";
    }

    fileData += "会話ログを表示します\n";

    while (std::getline(logData, str)) {
      fileData += str;
      fileData += "\n";
    }

    fileData += "会話ログの表示を終了します\n";

    return fileData;
  }

  return "";
}

int main(int argc, char *argv[]) {
  int soc;
  // 引数にポート番号が指定されているか
  if (argc <= 1) {
    std::cerr << "server2 port" << std::endl;
    return EX_USAGE;
  }
  // サーバーソケットの準備
  soc = serverSocket(argv[1]);
  if (soc == -1) {
    std::cerr << "serverSocket(" << argv[1] << "):error" << std::endl;
    return EX_UNAVAILABLE;
  }
  std::cerr << "ready for accept" << std::endl;
  /***
      アクセプトループ
      Ctl + C などで割り込まない限りはループが止まらないので close しない
  ***/
  acceptLoop(soc);
  // ソケットクローズ
  close(soc);
  return EX_OK;
}
