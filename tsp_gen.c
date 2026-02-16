#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// WAVヘッダ構造体（44バイト）
#pragma pack(push, 1)
typedef struct {
    char riff[4];           // "RIFF"
    int chunk_size;         // ファイルサイズ - 8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    int fmt_size;           // 16 (PCM)
    short audio_format;     // 1 (PCM)
    short num_channels;     // 1 (Mono)
    int sample_rate;        // 48000
    int byte_rate;          // sample_rate * channels * bits/8
    short block_align;      // channels * bits/8
    short bits_per_sample;  // 16
    char data[4];           // "data"
    int data_size;          // 波形データサイズ (N * 2 bytes)
} WavHeader;
#pragma pack(pop)

/**
 * 簡易IFFT (Radix-2)
 * 外部ライブラリ無しで動作させるための実装
 */
void simple_ifft(double complex *x, int n) {
    // ビット反転並べ替え
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double complex t = x[i]; x[i] = x[j]; x[j] = t;
        }
    }
    // クーリー・テューキー
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len; // 逆変換なのでマイナス
        double complex wlen = cos(ang) + I * sin(ang);
        for (int i = 0; i < n; i += len) {
            double complex w = 1.0;
            for (int j = 0; j < len / 2; j++) {
                double complex u = x[i + j];
                double complex v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    // 正規化
    for (int i = 0; i < n; i++) x[i] /= n;
}

int main() {
    // --- パラメータ ---
    int N = 262144;           // 信号長 (2^18)
    int J = N / 2;            // 実行長 (信号長の半分)
    int fs = 48000;           // サンプリング周波数
    int n0 = N / 4;           // シフト量 (中央に寄せるためのオフセット)
    const char *filename = "tsp_signal.wav";

    printf("TSP信号を生成中...\n");
    printf("N = %d, J = %d, fs = %d Hz\n", N, J, fs);

    // メモリ確保
    double complex *H = (double complex *)malloc(sizeof(double complex) * N);
    if (!H) { fprintf(stderr, "Memory error\n"); return 1; }

    // 1. 周波数領域での設計
    for (int k = 0; k <= N / 2; k++) {
        /*
         * 位相の式: θ(k) = -2πJ(k/N)^2 (up-TSPの基本)
         * 調整項: -2πk*n0 / N (時間領域でn0サンプルの巡回シフト)
         */
        double theta = -2.0 * M_PI * J * pow((double)k / N, 2) - (2.0 * M_PI * k * n0 / N);
        
        // H(k) = exp(j * theta)
        H[k] = cos(theta) + I * sin(theta);

        // 共役対称性を適用（実数信号にするため負の周波数側を埋める）
        if (k > 0 && k < N / 2) {
            H[N - k] = conj(H[k]);
        }
    }
    // Nyquist周波数の虚数部は0
    H[N / 2] = creal(H[N / 2]) + 0 * I;

    // 2. 逆フーリエ変換 (IFFT) で時間領域へ
    simple_ifft(H, N);

    // 3. 最大値で正規化（クリッピング防止）
    double max_amp = 0;
    for (int i = 0; i < N; i++) {
        if (fabs(creal(H[i])) > max_amp) max_amp = fabs(creal(H[i]));
    }

    // 4. WAVファイルとして書き出し
    FILE *fp = fopen(filename, "wb");
    if (!fp) { perror("File error"); return 1; }

    WavHeader head;
    memcpy(head.riff, "RIFF", 4);
    head.chunk_size = 36 + N * 2;
    memcpy(head.wave, "WAVE", 4);
    memcpy(head.fmt, "fmt ", 4);
    head.fmt_size = 16;
    head.audio_format = 1;
    head.num_channels = 1;
    head.sample_rate = fs;
    head.bits_per_sample = 16;
    head.byte_rate = fs * 2;
    head.block_align = 2;
    memcpy(head.data, "data", 4);
    head.data_size = N * 2;

    fwrite(&head, sizeof(WavHeader), 1, fp);

    for (int i = 0; i < N; i++) {
        // -1.0〜1.0 の実数部を 16bit整数 (-32768〜32767) に変換
        // マージンとして0.9を掛けています
        double sample = creal(H[i]) / max_amp * 0.9;
        short pcm = (short)(sample * 32767.0);
        fwrite(&pcm, 2, 1, fp);
    }

    fclose(fp);
    free(H);

    printf("完了: %s を保存しました。\n", filename);
    return 0;
}