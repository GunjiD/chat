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

i### thread

#### mutex

マルチスレッドでの排他処理で使われる.
pthread_mutex_t で mutex の管理用変数を宣言する

#### cond

条件変数
pthread_cond_t で cond の管理用変数を宣言する  
todo: 平行並列処理の本を読んでおく  

## 参考資料  

- ソケットプログラミングの流れ
  http://research.nii.ac.jp/~ichiro/syspro98/server.html
