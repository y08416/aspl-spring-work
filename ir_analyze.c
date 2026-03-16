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

    if (memcmp(header.riff, "RIFF", 4) != 0 || 
        memcmp(header.wave, "WAVE", 4) != 0 ||
        memcmp(header.fmt, "fmt ", 4) != 0 ||
        memcmp(header.data, "data", 4) != 0) {
        fprintf(stderr, "エラー: 無効なWAVファイル\n");
        fclose(fp);
        return -1;
    }

    *fs = header.sample_rate;
    *num_samples = header.data_size / 2;

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
 * 有効なインパルス応答長を算出
 */
int get_effective_ir_length(int16_t *ir, int len, double cutoff_db) {
    if (len <= 0) return len;
    int16_t peak = 0;
    for (int i = 0; i < len; i++) {
        int16_t a = (ir[i] >= 0) ? ir[i] : -ir[i];
        if (a > peak) peak = a;
    }
    if (peak == 0) return len;
    double threshold = peak * pow(10.0, cutoff_db / 20.0);
    for (int i = len - 1; i >= 0; i--) {
        int16_t s = ir[i];
        double mag = (s == -32768) ? 32768.0 : (s >= 0 ? (double)s : -(double)s);
        if (mag > threshold) {
            return i + 1;
        }
    }
    return len;
}

/**
 * Schroeder積分で残響曲線を計算（ピーク正規化版）
 */
void schroeder_integral(int16_t *ir, int len, double *decay_curve, int peak_idx) {
    // 後方累積積分を実行 [cite: 58-65]
    double sum = 0.0;
    for (int i = len - 1; i >= 0; i--) {
        double sample = (double)ir[i] / 32768.0;
        sum += sample * sample;
        decay_curve[i] = sum;
    }

    // ピーク位置のエネルギーで正規化することで、直接音到達時を0dBとする [cite: 67-75]
    double max_energy = decay_curve[peak_idx];
    if (max_energy > 0) {
        for (int i = 0; i < len; i++) {
            if (i < peak_idx) {
                // ピーク前はエネルギーの立ち上がり区間のため、便宜上0dB固定または計算対象外とする
                decay_curve[i] = 0.0;
            } else if (decay_curve[i] > 0) {
                decay_curve[i] = 10.0 * log10(decay_curve[i] / max_energy);
            } else {
                decay_curve[i] = -100.0;
            }
        }
    }
}

/**
 * 線形回帰で減衰時間を計算 [cite: 82-132]
 */
double calculate_decay_time(double *decay_curve, int len, int fs, 
                            double start_db, double end_db, int *start_idx, int *end_idx) {
    *start_idx = -1;
    *end_idx = -1;

    for (int i = 0; i < len; i++) {
        if (*start_idx < 0 && decay_curve[i] <= start_db) {
            *start_idx = i;
        }
        if (*start_idx >= 0 && decay_curve[i] <= end_db) {
            *end_idx = i;
            break;
        }
    }

    if (*start_idx < 0 || *end_idx < 0 || *end_idx <= *start_idx) {
        return -1.0;
    }

    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0;
    int n = *end_idx - *start_idx + 1;

    for (int i = *start_idx; i <= *end_idx; i++) {
        double x = (double)i / fs;
        double y = decay_curve[i];
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }

    double denominator = n * sum_x2 - sum_x * sum_x;
    if (fabs(denominator) < 1e-10) return -1.0;

    double slope = (n * sum_xy - sum_x * sum_y) / denominator;
    if (slope >= 0) return -1.0;

    return fabs(start_db - end_db) / (-slope);
}

int main(int argc, char *argv[]) {
    const char *ir_file = (argc > 1) ? argv[1] : "impulse_response.wav";

    printf("インパルス応答から残響時間を算出中...\n");
    printf("入力ファイル: %s\n", ir_file);

    int16_t *ir_samples = NULL;
    int fs, num_samples;
    if (read_wav(ir_file, &ir_samples, &fs, &num_samples) < 0) return 1;

    // 1. ピーク検出（直接音の到達時間を特定）
    int peak_idx = 0;
    double max_amp = 0;
    for (int i = 0; i < num_samples; i++) {
        double amp = fabs((double)ir_samples[i]);
        if (amp > max_amp) {
            max_amp = amp;
            peak_idx = i;
        }
    }
    printf("ピーク位置: %.3f 秒 (%d サンプル)\n", (double)peak_idx / fs, peak_idx);

    // 2. 有効長の決定
    int eff_len = get_effective_ir_length(ir_samples, num_samples, -40.0);

    // 3. 残響曲線の計算（ピーク位置基準）
    double *decay_curve = (double *)malloc((size_t)eff_len * sizeof(double));
    schroeder_integral(ir_samples, eff_len, decay_curve, peak_idx);

    // 4. T10 / T20 の算出 [cite: 139-153]
    int t10_s, t10_e, t20_s, t20_e;
    double t10 = calculate_decay_time(decay_curve, eff_len, fs, -5.0, -15.0, &t10_s, &t10_e);
    double t20 = calculate_decay_time(decay_curve, eff_len, fs, -5.0, -25.0, &t20_s, &t20_e);

    printf("\n=== 解析結果（ピーク補正済） ===\n");
    if (t10 > 0) {
        printf("T10 実測値: %.3f s\n", t10); // T10そのものの秒数
        printf("RT60 (from T10): %.3f s\n", t10 * 6.0);
    }
    if (t20 > 0) {
        printf("T20 実測値: %.3f s\n", t20); // T20そのものの秒数
        printf("RT60 (from T20): %.3f s\n", t20 * 3.0);
    }

    // 5. ファイル出力（gnuplot用）
    if (argc > 2) {
        FILE *fp = fopen(argv[2], "w");
        if (fp) {
            for (int i = peak_idx; i < eff_len; i++) {
                fprintf(fp, "%.6f\t%.2f\n", (double)(i - peak_idx) / fs, decay_curve[i]);
            }
            fclose(fp);
            printf("\n残響曲線を %s に保存（ピーク位置を0秒として出力）\n", argv[2]);
        }
    }

    free(ir_samples); free(decay_curve);
    return 0;
}