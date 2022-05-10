# chat

## chat サーバーを作る

### ソケット通信の流れ

- socket() ソケット生成
- bind() ソケット登録
- listen() ソケット接続準備
- accept() ソケット接続待機<-接続要求
- send()/recv() 送受信
- close() ソケット切断

## メモ

- NULL のとこ nullptr で書き換えても良さそう
- ソケットの作成とか基底クラス作って継承させてクライアントとサーバーで分けてもいいかも？
- デバッグのためにエラー吐いたら関数名返すとか。なんかそういうのあってよさそう
- C++ だと std::thread が実装されているのでこれを使うのが良い
- select() は一般的だが poll(), epoll() は Linux にしかないため Windows で動かすことができない

## 参考資料  

- ソケットプログラミングの流れ
  http://research.nii.ac.jp/~ichiro/syspro98/server.html
