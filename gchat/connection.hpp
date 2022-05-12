#pragma once

#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <unistd.h>

#include <iostream>

class connection {
private:
  int soc; // ソケットのディスクリプターを格納する

public:
  // todo: コンストラクタでソケットを用意する。サーバーソケット
  socket(const char *port_num) { soc = open(port_num); }
  ~socket();
  size_t mystrlcat(char *dst, const char *src, size_t size);
  int open(const char *portnm); // ソケットをオープンする
};
