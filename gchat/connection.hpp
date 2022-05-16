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
  int open(const char *port_num); // ソケットをオープンする
public:
  // todo: コンストラクタでソケットを用意する。サーバーソケット
  connection(const char *port_num) { m_soc = open(port_num); }
  ~connection();
  size_t mystrlcat(char *dst, const char *src, size_t size);
  int get_soc() { return m_soc; }
};
