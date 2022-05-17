#pragma once

#include "connection.hpp"

class server : public connection {
private:
  int m_acc;

public:
  // connection クラスに受け取ったポート番号を渡す
  server(const char *port_num) : connection(port_num) {}
  ~server();
  void accept_loop();
  void send_recv_loop(int acc);
};
