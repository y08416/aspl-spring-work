# aspl-spring-work

ASPL（音声信号処理）の春学期課題用リポジトリ。音声測定・解析に使う信号を生成・処理する C プログラム群です。

## 概要

外部ライブラリに依存せず、WAV ファイル形式で各種テスト信号を出力・処理します。

### ツール一覧

#### 信号生成

| プログラム | 説明 |
|-----------|------|
| **tsp_gen** | TSP（Time Stretched Pulse）信号の生成。周波数領域で位相を設計し IFFT で時間領域に変換して WAV 出力。インパルス応答測定などに使用。 |
| **white_noise** | ホワイトノイズの生成。48 kHz・指定秒数の WAV ファイルを出力。 |

#### インパルス応答算出

| プログラム | 説明 |
|-----------|------|
| **tsp_to_ir** | TSP信号とその応答からインパルス応答を算出。周波数領域で逆フィルタ（down-TSP）を適用してインパルス応答を抽出。 |
| **adaptive_filter** | 白色信号とその応答から適応フィルタ（NLMS）を用いてインパルス応答を算出。 |

#### 解析

| プログラム | 説明 |
|-----------|------|
| **ir_analyze** | インパルス応答から残響時間（T10, T20, RT60）を算出。Schroeder積分により残響曲線を計算し、線形回帰で減衰時間を求める。 |

### ビルド方法

各ソースを個別にコンパイル：

```bash
# 信号生成
gcc -o tsp_gen tsp_gen.c -lm
gcc -o white_noise white_noise.c

# インパルス応答算出
gcc -o tsp_to_ir tsp_to_ir.c -lm
gcc -o adaptive_filter adaptive_filter.c -lm

# 解析
gcc -o ir_analyze ir_analyze.c -lm
```

### 使用方法

#### 1. TSP信号からインパルス応答を算出

```bash
# デフォルトファイル名を使用
./tsp_to_ir

# またはファイル名を指定
./tsp_to_ir tsp_signal.wav tsp_response.wav impulse_response.wav
```

#### 2. 適応フィルタでインパルス応答を算出

```bash
# デフォルトファイル名を使用（フィルタ長は1秒分）
./adaptive_filter

# またはファイル名とフィルタ長を指定
./adaptive_filter white_noise_180s.wav white_noise_response.wav impulse_response_adaptive.wav 48000
```

#### 3. 残響時間を解析

```bash
# デフォルトファイル名を使用
./ir_analyze

# またはファイル名を指定（残響曲線も出力）
./ir_analyze impulse_response.wav decay_curve.txt
```

### 出力仕様

- サンプリング周波数: 48 kHz
- チャンネル: モノラル
- ビット深度: 16 bit
- 形式: WAV（PCM）
