#include <asm-generic/errno-base.h>
#include <asm-generic/socket.h>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <ostream>
#include <pthread.h>
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
void *acceptThread(void *arg);

const int NUM_CHILD = 2;
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
int g_lock_id = -1;

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
void *acceptThread(void *arg) {
  char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
  struct sockaddr_storage from;
  int acc, soc;
  socklen_t len;

  // 引数の取得
  soc = *(int *)arg;
  // スレッドのデタッチ
  // デタッチすると、そのスレッドは親スレッドに合流することができなくなるが、終了時にそのスレッドが占有していたリソースはすぐに開放される
  pthread_detach(pthread_self());
  while (1) {

    std::cerr << "<" << (int)pthread_self() << ">ロック獲得開始" << std::endl;
    pthread_mutex_lock(&g_lock);
    g_lock_id = pthread_self();
    std::cerr << "<" << (int)pthread_self() << ">ロック獲得" << std::endl;
    len = (socklen_t)sizeof(from);
    // 接続受付
    acc = accept(soc, (struct sockaddr *)&from, &len);
    if (acc == -1) {
      if (errno != EINTR) {
        perror("accept");
      }
      std::cerr << "<" << (int)pthread_self() << ">ロック開放" << std::endl;
      // アンロック
      g_lock_id = -1;
      pthread_mutex_unlock(&g_lock);
    } else {
      getnameinfo((struct sockaddr *)&from, len, hbuf, sizeof(hbuf), sbuf,
                  sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
      std::cerr << "accept:" << hbuf << ":" << sbuf << std::endl;
      std::cerr << "<" << (int)pthread_self() << ">ロック開放" << std::endl;
      // アンロック
      g_lock_id = -1;
      pthread_mutex_unlock(&g_lock);
      //送受信ループ
      sendRecvLoop(acc);
      // アクセプトソケットクローズ
      close(acc);
      acc = 0;
    }
  }
  pthread_exit(0);
  return 0;
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
      std::cerr << "<" << (int)pthread_self() << ">recv:EOF" << std::endl;
      break;
    }

    // 文字列化・表示
    buf[len] = '\0';
    ptr = strpbrk(buf, "\r\n");
    if (ptr != nullptr) {
      *ptr = '\0';
    }
    std::cerr << "<" << (int)pthread_self() << ">[client]" << std::endl;

    /***
        応答文字列作成
        一般的な入門書では strcat()
    が使われるが、バッファサイズが指定できないためバッファオーバーランのバグが起きやすい
        そのため自作関数でバッファサイズを超える場合はコピーしないようにしている
    ***/
    mystrlcat(buf, ":OK\r\n", sizeof(buf));
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
  int i, soc;
  pthread_t thread_id;
  // 引数にポート番号が指定されているか
  if (argc <= 1) {
    std::cerr << "server8 port" << std::endl;
    return EX_USAGE;
  }
  // サーバーソケットの準備
  soc = serverSocket(argv[1]);
  if (soc == -1) {
    std::cerr << "serverSocket(" << argv[1] << "):error" << std::endl;
    return EX_UNAVAILABLE;
  }

  // 子スレッドの作成
  for (i = 0; i < NUM_CHILD; i++) {
    // スレッド生成
    if (pthread_create(&thread_id, nullptr, acceptThread, &soc) != 0) {
      perror("pthread_create");
    } else {
      std::cerr << "pthread_create:create:thread_id=" << thread_id << std::endl;
    }
  }
  std::cerr << "ready for accept" << std::endl;
  while (1) {
    sleep(10);
    std::cerr << "<<" << getpid() << ">>ロック状態:" << g_lock_id << std::endl;
  }
  // ソケットクローズ
  close(soc);
  // mutex 破棄
  pthread_mutex_destroy(&g_lock);
  return EX_OK;
}
