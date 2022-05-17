#include "server.hpp"
#include "connection.hpp"

// アクセプトループ
void server::accept_loop() {
  char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
  struct sockaddr_storage from;

  socklen_t len;

  while (1) {
    len = (socklen_t)sizeof(from);
    // 接続受付
    m_acc = accept(connection::get_soc(), (struct sockaddr *)&from, &len);
    if (m_acc == -1) {
      if (errno != EINTR) {
        perror("accept");
      }
    } else {
      getnameinfo((struct sockaddr *)&from, len, hbuf, sizeof(hbuf), sbuf,
                  sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
      std::cerr << "accept:" << hbuf << ":" << sbuf << std::endl;
      //送受信ループ
      send_recv_loop(m_acc);
      // アクセプトソケットクローズ
      close(m_acc);
      m_acc = 0;
    }
  }
}

void send_recv_loop(int acc) {
  char buf[512], *ptr;
  ssize_t len;
  std::string result = "";

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
      std::cerr << "recv:EOF" << std::endl;
      break;
    }

    // 文字列化・表示
    buf[len] = '\0';
    ptr = strpbrk(buf, "\r\n");
    if (ptr != nullptr) {
      *ptr = '\0';
    }
    std::cerr << "[client]" << buf << std::endl;

    /***
       応答文字列作成
       一般的な入門書では strcat()
   が使われるが、バッファサイズが指定できないためバッファオーバーランのバグが起きやすい
       そのため自作関数でバッファサイズを超える場合はコピーしないようにしている
   ***/
    connection::mystrlcat(buf, ":OK\r\n", sizeof(buf));
    len = strlen(buf);

    // 応答
    len = send(acc, buf, len, 0);
    if (len == -1) {
      // エラー
      perror("send");
      break;
    }
  }
}

int main(int argc, char *argv[]) {
  int soc;
  // 引数にポート番号が指定されているか
  if (argc <= 1) {
    std::cerr << "server port" << std::endl;
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
