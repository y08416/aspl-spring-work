# aspl-spring-work

ASPL（音声信号処理）の春学期課題用リポジトリ。音声測定・解析に使う信号を生成する C プログラム群です。

## 概要

外部ライブラリに依存せず、WAV ファイル形式で各種テスト信号を出力します。

### ツール一覧

| プログラム | 説明 |
|-----------|------|
| **tsp_gen** | TSP（Time Stretched Pulse）信号の生成。周波数領域で位相を設計し IFFT で時間領域に変換して WAV 出力。インパルス応答測定などに使用。 |
| **white_noise** | ホワイトノイズの生成。48 kHz・指定秒数の WAV ファイルを出力。 |
| **down_gen** | ダウンサンプル等の処理を行うツール。 |

### ビルド方法

各ソースを個別にコンパイル：

```bash
gcc -o tsp_gen tsp_gen.c -lm
gcc -o white_noise white_noise.c
gcc -o down_gen down_gen.c -lm   # 必要に応じて -lm を付与
```

### 出力仕様

- サンプリング周波数: 48 kHz
- チャンネル: モノラル
- ビット深度: 16 bit
- 形式: WAV（PCM）
