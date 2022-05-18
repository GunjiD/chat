#pragma once

#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <unistd.h>

#include <iostream>
#include <memory>

// ソケットのオープン、クローズを行うクラス
class connection {
private:
  int m_soc; // ソケットのディスクリプターを格納する
  int open(const char *host_name,
           const char *port_num); // ソケットをオープンする
public:
  // todo: コンストラクタでソケットを用意する。サーバーソケット
  //       IP アドレスの設定を行えるようにする
  connection(const char *host_name, const char *port_num) {
    m_soc = open(host_name, port_num);
  }
  ~connection();
  static size_t mystrlcat(char *dst, const char *src, size_t size);
  int get_soc() { return m_soc; }
};
