#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <asm-generic/socket.h>
#include <bits/types/struct_timeval.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <ostream>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/param.h>
#include <sys/poll.h>
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
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <type_traits>
#include <unistd.h>

// 最大同時接続処理
const int MAX_CHILD = 20;

const int MAXQUEUESZ = 4096;
const int MAXSENDER = 2;
#define QUEUE_NEXT(i_) (((i_) + 1) % MAXQUEUESZ)

struct queueData {
  int acc;
  char buf[512];
  ssize_t len;
};

struct queue {
  int front;
  int last;
  struct queueData data[MAXQUEUESZ];
  pthread_mutex_t mutex;
  pthread_cond_t cond;
};

struct queue gQueue[MAXSENDER];

void *sendThread(void *arg);

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
  struct sockaddr_storage from;
  int acc, count, qi, i, epollfd, nfds;
  socklen_t flen;
  struct epoll_event ev, events[MAX_CHILD];

  epollfd = epoll_create(MAX_CHILD + 1);
  if (epollfd == -1) {
    perror("epoll_create");
    return;
  }

  // EPOLL 用のデータの作成
  ev.data.fd = soc;
  ev.events = EPOLLIN;

  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, soc, &ev) == -1) {
    perror("epoll_ctl");
    close(acc);
    close(epollfd);
    return;
  }
  count = 0;

  while (1) {
    std::cerr << "<<child count:" << count << ">>" << std::endl;

    nfds = epoll_wait(epollfd, events, MAX_CHILD + 1, 10 * 1000);
    switch (nfds) {
    case -1:
      // エラー
      perror("epoll_wait");
      break;
    case 0:
      // タイムアウト
      break;
    default:
      // ソケットがレディ
      for (i = 0; i < nfds; i++) {
        if (events[i].data.fd == soc) {
          // サーバーソケットレディ
          flen = static_cast<socklen_t>(sizeof(from));
          // 接続受付
          acc = accept(soc, (struct sockaddr *)&from, &flen);
          if (acc == -1) {
            if (errno != EINTR) {
              perror("accept");
            }
          } else {
            getnameinfo((struct sockaddr *)&from, flen, hbuf, sizeof(hbuf),
                        sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
            std::cerr << "accept:" << hbuf << ":" << sbuf << std::endl;
            // 空きがない
            if (count + 1 >= MAX_CHILD) {
              // これ以上接続できない
              std::cerr << "connection is full : cannot accept" << std::endl;
              // クローズ
              close(acc);
            } else {
              ev.data.fd = acc;
              ev.events = EPOLLIN;
              if (epoll_ctl(epollfd, EPOLL_CTL_ADD, acc, &ev) == -1) {
                perror("epoll_ctl");
                close(acc);
                close(epollfd);
                return;
              }
              count++;
            }
          }
        } else {
          qi = events[i].data.fd % MAXSENDER;
          gQueue[qi].data[gQueue[qi].last].acc = events[i].data.fd;
          gQueue[qi].data[gQueue[qi].last].len =
              recv(gQueue[qi].data[gQueue[qi].last].acc,
                   gQueue[qi].data[gQueue[qi].last].buf,
                   sizeof(gQueue[qi].data[gQueue[qi].last].buf), 0);

          // 受信
          switch (gQueue[qi].data[gQueue[qi].last].len) {
            // エラー
          case -1:
            perror("recv");
            // エンド・オブ・ファイル
          case 0:
            std::cerr << "[child" << events[i].data.fd << "]recv:EOF"
                      << std::endl;
            // エラーまたは切断
            if (epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, &ev) ==
                -1) {
              perror("epoll_ctl");
              close(events[i].data.fd);
              close(epollfd);
              return;
            }
            // クローズ
            close(events[i].data.fd);
            count--;
            break;
          default:
            pthread_mutex_lock(&gQueue[qi].mutex);
            gQueue[qi].last = QUEUE_NEXT(gQueue[qi].last);
            pthread_cond_signal(&gQueue[qi].cond);
            pthread_mutex_unlock(&gQueue[qi].mutex);
            break;
          }
        }
      }
      break;
    }
  }
  close(epollfd);
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

void *sendThread(void *arg) {
  char *ptr;
  ssize_t len;
  int i, qi;
  qi = reinterpret_cast<intptr_t>(arg);

  while (1) {
    pthread_mutex_lock(&gQueue[qi].mutex);
    if (gQueue[qi].last != gQueue[qi].front) {
      i = gQueue[qi].front;
      gQueue[qi].front = QUEUE_NEXT(gQueue[qi].front);
      pthread_mutex_unlock(&gQueue[qi].mutex);
    } else {
      pthread_cond_wait(&gQueue[qi].cond, &gQueue[qi].mutex);
      pthread_mutex_unlock(&gQueue[qi].mutex);
      continue;
    }
    // 文字列化・表示
    gQueue[qi].data[i].buf[gQueue[qi].data[i].len] = '\0';
    if ((ptr = strpbrk(gQueue[qi].data[i].buf, "\r\n")) != nullptr) {
      *ptr = '\0';
    }
    std::cerr << "[child" << gQueue[qi].data[i].acc << "]"
              << gQueue[qi].data[i].buf << std::endl;
    gQueue[qi].data[i].len = strlen(gQueue[qi].data[i].buf);
    // 応答文字列作成
    mystrlcat(gQueue[qi].data[i].buf, ":OK\r\n",
              sizeof(gQueue[qi].data[i].buf));
    gQueue[qi].data[i].len = strlen(gQueue[qi].data[i].buf);
    // 応答
    len = send(gQueue[qi].data[i].acc, gQueue[qi].data[i].buf,
               gQueue[qi].data[i].len, 0);
    if (len == -1) {
      // エラー
      perror("send");
    }
  }
  pthread_exit(0);
  // NOT REACHED
  return 0;
}

int main(int argc, char *argv[]) {
  int soc, i;
  pthread_t id;
  // 引数にポート番号が指定されているか
  if (argc <= 1) {
    std::cerr << "server2 port" << std::endl;
    return EX_USAGE;
  }
  for (i = 0; i < MAXSENDER; i++) {
    pthread_mutex_init(&gQueue[i].mutex, nullptr);
    pthread_cond_init(&gQueue[i].cond, nullptr);
    pthread_create(&id, nullptr, sendThread, (void *)i);
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
  pthread_join(id, nullptr);
  // ソケットクローズ
  close(soc);
  return EX_OK;
}
