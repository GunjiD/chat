#include <asm-generic/errno-base.h>
#include <asm-generic/socket.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <ostream>
#include <sched.h>
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
  int acc, status;
  pid_t pid;
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
      std::cerr << "accept:" << hbuf << ":" << sbuf << std::endl;
      pid = fork();
      if (pid == 0) {
        /* 子プロセス */
        /* サーバーソケットクローズ */
        close(soc);
        //送受信ループ
        sendRecvLoop(acc);
        // アクセプトソケットクローズ
        close(acc);
        _exit(1);
      } else if (pid > 0) {
        // fork():成功：親プロセス
        // アクセプトソケットクローズ
        close(acc);
        acc = -1;
      } else {
        // fork():失敗
        perror("fork");
        // アクセプトソケットクローズ
        close(acc);
        acc = -1;
      }
      // シグナルでキャッチできなかった子プロセス終了のチェック
      pid = waitpid(-1, &status, WNOHANG);
      if (pid > 0) {
        // 子プロセス終了有り
        std::cerr << "accept_loop:waitpid:pid=" << pid << ",status=" << status
                  << std::endl;
        std::cerr << "  WIFEXITED:" << WIFEXITED(status)
                  << ",WEXITSTATUS:" << WEXITSTATUS(status)
                  << ",WIFSIGNALED:" << WIFSIGNALED(status)
                  << ",WTERMSIG:" << WTERMSIG(status)
                  << ",WIFSTOPPED:" << WIFSTOPPED(status)
                  << ",WSTOPSIG:" << WSTOPSIG(status) << std::endl;
      }
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
      std::cerr << getpid() << "recv:EOF" << std::endl;
      logData << "<" << getpid() << ">recv:EOF" << std::endl;
      break;
    }

    // 文字列化・表示
    buf[len] = '\0';
    ptr = strpbrk(buf, "\r\n");
    if (ptr != nullptr) {
      *ptr = '\0';
    }
    std::cerr << "<" << getpid() << ">[client]" << buf << std::endl;

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

void sigChldHandler(int sig) {
  int status;
  pid_t pid;
  // 子プロセスの終了を待つ
  pid = wait(&status);
  std::cerr << "sigChildHandler:wait:pid=" << pid << ",status=" << status
            << std::endl;
  std::cerr << "accept_loop:waitpid:pid=" << pid << ",status=" << status
            << std::endl;
  std::cerr << "  WIFEXITED:" << WIFEXITED(status)
            << ",WEXITSTATUS:" << WEXITSTATUS(status)
            << ",WIFSIGNALED:" << WIFSIGNALED(status)
            << ",WTERMSIG:" << WTERMSIG(status)
            << ",WIFSTOPPED:" << WIFSTOPPED(status)
            << ",WSTOPSIG:" << WSTOPSIG(status) << std::endl;
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
    std::cerr << "server port" << std::endl;
    return EX_USAGE;
  }
  // 子プロセス終了シグナルのセット
  signal(SIGCHLD, sigChldHandler);
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
