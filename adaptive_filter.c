#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

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
 * WAVファイルを読み込む
 */
int read_wav(const char *filename, int16_t **samples, int *fs, int *num_samples) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "エラー: %s を開けません\n", filename);
        return -1;
    }

    WavHeader header;
    if (fread(&header, sizeof(WavHeader), 1, fp) != 1) {
        fprintf(stderr, "エラー: WAVヘッダの読み込みに失敗\n");
        fclose(fp);
        return -1;
    }

    // WAV形式のチェック
    if (memcmp(header.riff, "RIFF", 4) != 0 || 
        memcmp(header.wave, "WAVE", 4) != 0 ||
        memcmp(header.fmt, "fmt ", 4) != 0 ||
        memcmp(header.data, "data", 4) != 0) {
        fprintf(stderr, "エラー: 無効なWAVファイル\n");
        fclose(fp);
        return -1;
    }

    *fs = header.sample_rate;
    *num_samples = header.data_size / 2; // 16bit = 2 bytes

    *samples = (int16_t *)malloc(*num_samples * sizeof(int16_t));
    if (!*samples) {
        fprintf(stderr, "エラー: メモリ確保に失敗\n");
        fclose(fp);
        return -1;
    }

    if (fread(*samples, sizeof(int16_t), *num_samples, fp) != *num_samples) {
        fprintf(stderr, "エラー: データの読み込みに失敗\n");
        free(*samples);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

/**
 * WAVファイルに書き込む
 */
int write_wav(const char *filename, int16_t *samples, int num_samples, int fs) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "エラー: %s を開けません\n", filename);
        return -1;
    }

    WavHeader head;
    memcpy(head.riff, "RIFF", 4);
    head.chunk_size = 36 + num_samples * 2;
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
    head.data_size = num_samples * 2;

    fwrite(&head, sizeof(WavHeader), 1, fp);
    fwrite(samples, sizeof(int16_t), num_samples, fp);

    fclose(fp);
    return 0;
}

/**
 * NLMS適応フィルタ
 * 入力: x[n] (白色信号)
 * 出力: y[n] (録音信号)
 * 出力: h[n] (推定されたインパルス応答)
 */
void nlms_adaptive_filter(double *x, double *y, int x_len, int filter_len, 
                          double *h, double mu, double beta) {
    // フィルタ係数を初期化
    for (int i = 0; i < filter_len; i++) {
        h[i] = 0.0;
    }

    // 入力バッファ（遅延線）
    double *x_buf = (double *)calloc(filter_len, sizeof(double));

    // NLMSアルゴリズム
    for (int n = 0; n < x_len; n++) {
        // 入力バッファを更新（シフト）
        for (int i = filter_len - 1; i > 0; i--) {
            x_buf[i] = x_buf[i - 1];
        }
        x_buf[0] = x[n];

        // フィルタ出力を計算
        double y_hat = 0.0;
        for (int i = 0; i < filter_len; i++) {
            y_hat += h[i] * x_buf[i];
        }

        // 誤差を計算
        double e = y[n] - y_hat;

        // 入力ベクトルのパワーを計算
        double x_power = beta;
        for (int i = 0; i < filter_len; i++) {
            x_power += x_buf[i] * x_buf[i];
        }

        // フィルタ係数を更新（NLMS）
        if (x_power > 1e-10) {
            double step = mu / x_power;
            for (int i = 0; i < filter_len; i++) {
                h[i] += step * e * x_buf[i];
            }
        }
    }

    free(x_buf);
}

int main(int argc, char *argv[]) {
    const char *input_file = (argc > 1) ? argv[1] : "white_noise_180s.wav";
    const char *output_file = (argc > 2) ? argv[2] : "white_noise_response.wav";
    const char *ir_output = (argc > 3) ? argv[3] : "impulse_response_adaptive.wav";
    int filter_len = (argc > 4) ? atoi(argv[4]) : 48000; // デフォルト1秒分

    printf("適応フィルタでインパルス応答を算出中...\n");
    printf("入力信号: %s\n", input_file);
    printf("出力信号: %s\n", output_file);
    printf("フィルタ長: %d サンプル (%.3f 秒)\n", filter_len, (double)filter_len / 48000.0);

    // 1. 入力信号（白色信号）を読み込む
    int16_t *input_samples = NULL;
    int fs_input, input_len;
    if (read_wav(input_file, &input_samples, &fs_input, &input_len) < 0) {
        fprintf(stderr, "エラー: 入力信号の読み込みに失敗\n");
        return 1;
    }
    printf("入力信号: %d サンプル, fs = %d Hz\n", input_len, fs_input);

    // 2. 出力信号（録音信号）を読み込む
    int16_t *output_samples = NULL;
    int fs_output, output_len;
    if (read_wav(output_file, &output_samples, &fs_output, &output_len) < 0) {
        fprintf(stderr, "エラー: 出力信号の読み込みに失敗\n");
        free(input_samples);
        return 1;
    }
    printf("出力信号: %d サンプル, fs = %d Hz\n", output_len, fs_output);

    if (fs_input != fs_output) {
        fprintf(stderr, "エラー: サンプリング周波数が一致しません\n");
        free(input_samples);
        free(output_samples);
        return 1;
    }

    // 信号長を統一（短い方に合わせる）
    int min_len = (input_len < output_len) ? input_len : output_len;
    printf("処理長: %d サンプル (%.3f 秒)\n", min_len, (double)min_len / fs_input);

    // 3. 信号をdouble配列に変換
    double *x = (double *)malloc(min_len * sizeof(double));
    double *y = (double *)malloc(min_len * sizeof(double));
    for (int i = 0; i < min_len; i++) {
        x[i] = (double)input_samples[i] / 32768.0;
        y[i] = (double)output_samples[i] / 32768.0;
    }

    // 4. NLMS適応フィルタを実行
    double *h = (double *)calloc(filter_len, sizeof(double));
    double mu = 0.1;      // ステップサイズ
    double beta = 1e-6;   // 正則化パラメータ

    printf("\n適応フィルタを実行中...\n");
    nlms_adaptive_filter(x, y, min_len, filter_len, h, mu, beta);
    printf("完了\n");

    // 5. 最大値で正規化してWAV出力
    double max_amp = 0;
    for (int i = 0; i < filter_len; i++) {
        double amp = fabs(h[i]);
        if (amp > max_amp) max_amp = amp;
    }

    int16_t *ir_samples = (int16_t *)malloc(filter_len * sizeof(int16_t));
    for (int i = 0; i < filter_len; i++) {
        double sample = h[i] / max_amp * 0.9;
        ir_samples[i] = (int16_t)(sample * 32767.0);
    }

    if (write_wav(ir_output, ir_samples, filter_len, fs_input) < 0) {
        fprintf(stderr, "エラー: WAVファイルの書き込みに失敗\n");
        free(input_samples);
        free(output_samples);
        free(x);
        free(y);
        free(h);
        free(ir_samples);
        return 1;
    }

    printf("\n完了: %s を保存しました。\n", ir_output);
    printf("インパルス応答長: %d サンプル (%.3f 秒)\n", filter_len, (double)filter_len / fs_input);

    // メモリ解放
    free(input_samples);
    free(output_samples);
    free(x);
    free(y);
    free(h);
    free(ir_samples);

    return 0;
}
