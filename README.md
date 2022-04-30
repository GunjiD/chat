# chat

## chat サーバーを作る

### ソケット通信の流れ

- socket() ソケット生成
- bind() ソケット登録
- listen() ソケット接続準備
- accept() ソケット接続待機<-接続要求
- send()/recv() 送受信
- close() ソケット切断

## 参考資料  

- ソケットプログラミングの流れ
  http://research.nii.ac.jp/~ichiro/syspro98/server.html

