#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
 * WAVファイルを読み込む
 * 戻り値: 読み込んだサンプル数、エラー時は-1
 */
int read_wav(const char *filename, int16_t **samples, int *fs) {
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
    int num_samples = header.data_size / 2; // 16bit = 2 bytes

    *samples = (int16_t *)malloc(num_samples * sizeof(int16_t));
    if (!*samples) {
        fprintf(stderr, "エラー: メモリ確保に失敗\n");
        fclose(fp);
        return -1;
    }

    if (fread(*samples, sizeof(int16_t), num_samples, fp) != num_samples) {
        fprintf(stderr, "エラー: データの読み込みに失敗\n");
        free(*samples);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return num_samples;
}

/**
 * 簡易FFT (Radix-2)
 */
void simple_fft(double complex *x, int n) {
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
        double ang = 2.0 * M_PI / len;
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
}

/**
 * 簡易IFFT (Radix-2)
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

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "使用方法: %s tsp_signal.wav response1.wav [response2.wav ...] impulse_response.wav\n", argv[0]);
        fprintf(stderr, "応答ファイルは1つ以上指定してください。\n");
        return 1;
    }

    const char *tsp_file = argv[1];
    const char *output_file = argv[argc - 1];
    int num_response_files = argc - 3;

    printf("TSP信号からインパルス応答を算出中...\n");
    printf("TSP信号: %s\n", tsp_file);
    printf("TSP応答: %d ファイル", num_response_files);
    for (int i = 0; i < num_response_files; i++) printf(" %s%s", argv[2 + i], (i < num_response_files - 1) ? "," : "");
    printf("\n出力: %s\n", output_file);

    // 1. TSP信号を読み込む
    int16_t *tsp_samples = NULL;
    int fs_tsp;
    int tsp_len = read_wav(tsp_file, &tsp_samples, &fs_tsp);
    if (tsp_len < 0) {
        fprintf(stderr, "エラー: TSP信号の読み込みに失敗\n");
        return 1;
    }
    printf("TSP信号: %d サンプル, fs = %d Hz\n", tsp_len, fs_tsp);

    // 2. 複数のTSP応答を読み込み、時間領域で平均化
    int16_t **response_buffers = (int16_t **)malloc(num_response_files * sizeof(int16_t *));
    int *response_lengths = (int *)malloc(num_response_files * sizeof(int));
    int fs_response = 0;
    int min_response_len = 0;

    for (int f = 0; f < num_response_files; f++) {
        int fs_file;
        int len = read_wav(argv[2 + f], &response_buffers[f], &fs_file);
        if (len < 0) {
            fprintf(stderr, "エラー: TSP応答 %s の読み込みに失敗\n", argv[2 + f]);
            for (int j = 0; j < f; j++) free(response_buffers[j]);
            free(response_buffers);
            free(response_lengths);
            free(tsp_samples);
            return 1;
        }
        if (f == 0) {
            fs_response = fs_file;
        } else if (fs_response != fs_file) {
            fprintf(stderr, "エラー: 応答ファイルのサンプリング周波数が一致しません (%s: %d Hz)\n", argv[2 + f], fs_file);
            for (int j = 0; j <= f; j++) free(response_buffers[j]);
            free(response_buffers);
            free(response_lengths);
            free(tsp_samples);
            return 1;
        }
        response_lengths[f] = len;
        if (f == 0 || len < min_response_len) min_response_len = len;
    }

    if (fs_tsp != fs_response) {
        fprintf(stderr, "エラー: サンプリング周波数が一致しません (TSP: %d, 応答: %d)\n", fs_tsp, fs_response);
        for (int f = 0; f < num_response_files; f++) free(response_buffers[f]);
        free(response_buffers);
        free(response_lengths);
        free(tsp_samples);
        return 1;
    }

    // 時間領域で平均（最短長に揃える）
    int response_len = min_response_len;
    double *response_sum = (double *)calloc(response_len, sizeof(double));
    for (int f = 0; f < num_response_files; f++) {
        for (int i = 0; i < response_len; i++) {
            response_sum[i] += (double)response_buffers[f][i] / 32768.0;
        }
    }
    for (int i = 0; i < response_len; i++) {
        response_sum[i] /= num_response_files;
    }

    int16_t *response_samples = (int16_t *)malloc(response_len * sizeof(int16_t));
    for (int i = 0; i < response_len; i++) {
        double s = response_sum[i] * 32768.0;
        if (s > 32767.0) s = 32767.0;
        if (s < -32768.0) s = -32768.0;
        response_samples[i] = (int16_t)s;
    }

    printf("TSP応答: %d ファイルを平均、%d サンプル, fs = %d Hz\n", num_response_files, response_len, fs_response);

    // 一時バッファ解放
    for (int f = 0; f < num_response_files; f++) free(response_buffers[f]);
    free(response_buffers);
    free(response_lengths);
    free(response_sum);

    // 3. 信号長を統一（2のべき乗に拡張）
    int N = 1;
    int max_len = (tsp_len > response_len) ? tsp_len : response_len;
    while (N < max_len) N <<= 1;
    printf("FFT長: %d\n", N);

    // 4. TSP信号をFFT
    double complex *TSP = (double complex *)calloc(N, sizeof(double complex));
    for (int i = 0; i < tsp_len; i++) {
        TSP[i] = (double)tsp_samples[i] / 32768.0;
    }
    simple_fft(TSP, N);

    // 5. TSP応答をFFT（2周期目を切り出す想定）
    // 実際の測定では2周期再生して2周期目を切り出す必要がある
    double complex *RESPONSE = (double complex *)calloc(N, sizeof(double complex));
    int start_idx = (response_len >= tsp_len * 2) ? tsp_len : 0; // 2周期目があれば使用
    int copy_len = (response_len - start_idx < N) ? response_len - start_idx : N;
    for (int i = 0; i < copy_len; i++) {
        RESPONSE[i] = (double)response_samples[start_idx + i] / 32768.0;
    }
    simple_fft(RESPONSE, N);

    // 6. 逆フィルタを計算（down-TSP）
    // down-TSP: exp(+j * 2πJ * (k/N)^2)
    int J = tsp_len / 2; // TSP信号の実効長を推定
    double complex *INV_FILTER = (double complex *)malloc(N * sizeof(double complex));
    
    for (int k = 0; k <= N / 2; k++) {
        double theta = 2.0 * M_PI * J * pow((double)k / N, 2);
        INV_FILTER[k] = cos(theta) + I * sin(theta);
        
        // 共役対称性
        if (k > 0 && k < N / 2) {
            INV_FILTER[N - k] = conj(INV_FILTER[k]);
        }
    }
    INV_FILTER[N / 2] = creal(INV_FILTER[N / 2]) + 0 * I;

    // 7. 周波数領域で除算（逆フィルタ適用）
    double complex *IR_FREQ = (double complex *)malloc(N * sizeof(double complex));
    for (int k = 0; k < N; k++) {
        double tsp_mag = cabs(TSP[k]);
        if (tsp_mag > 1e-10) {
            // H(k) = Y(k) / S(k) = Y(k) * INV_FILTER(k)
            IR_FREQ[k] = RESPONSE[k] * INV_FILTER[k];
        } else {
            IR_FREQ[k] = 0.0;
        }
    }

    // 8. IFFTで時間領域に戻す
    simple_ifft(IR_FREQ, N);

    // 9. 最大値で正規化してWAV出力
    double max_amp = 0;
    for (int i = 0; i < N; i++) {
        double amp = fabs(creal(IR_FREQ[i]));
        if (amp > max_amp) max_amp = amp;
    }

    int16_t *ir_samples = (int16_t *)malloc(N * sizeof(int16_t));
    for (int i = 0; i < N; i++) {
        double sample = creal(IR_FREQ[i]) / max_amp * 0.9;
        ir_samples[i] = (int16_t)(sample * 32767.0);
    }

    if (write_wav(output_file, ir_samples, N, fs_tsp) < 0) {
        fprintf(stderr, "エラー: WAVファイルの書き込みに失敗\n");
        free(tsp_samples);
        free(response_samples);
        free(TSP);
        free(RESPONSE);
        free(INV_FILTER);
        free(IR_FREQ);
        free(ir_samples);
        return 1;
    }

    printf("完了: %s を保存しました。\n", output_file);
    printf("インパルス応答長: %d サンプル (%.3f 秒)\n", N, (double)N / fs_tsp);

    // メモリ解放
    free(tsp_samples);
    free(response_samples);
    free(TSP);
    free(RESPONSE);
    free(INV_FILTER);
    free(IR_FREQ);
    free(ir_samples);

    return 0;
}
