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
 * ノイズ対策: 有効なインパルス応答長を算出
 * ピークから指定dB以下に減衰した点で打ち切り、ノイズテールを除外する
 * T10算出を確実にするための工夫（ノイズフロアが残響曲線を平坦化するのを防ぐ）
 */
int get_effective_ir_length(int16_t *ir, int len, double cutoff_db) {
    if (len <= 0) return len;
    int16_t peak = 0;
    for (int i = 0; i < len; i++) {
        int16_t a = (ir[i] >= 0) ? ir[i] : -ir[i];
        if (a > peak) peak = a;
    }
    if (peak == 0) return len;
    double threshold = peak * pow(10.0, cutoff_db / 20.0);  /* 例: -40dB → 約1/100 */
    /* 後ろから走査し、thresholdを超える最後の位置を返す */
    for (int i = len - 1; i >= 0; i--) {
        int16_t s = ir[i];
        double mag = (s == -32768) ? 32768.0 : (s >= 0 ? (double)s : -(double)s);
        if (mag > threshold) {
            return i + 1;  /* このサンプルまで使用 */
        }
    }
    return len;  /* 全部使う */
}

/**
 * Schroeder積分で残響曲線を計算
 * E(t) = ∫[t to ∞] h^2(τ) dτ
 * インパルス応答の2乗の後方累積積分
 */
void schroeder_integral(int16_t *ir, int len, double *decay_curve) {
    // 後ろから累積積分
    double sum = 0.0;
    for (int i = len - 1; i >= 0; i--) {
        double sample = (double)ir[i] / 32768.0;
        sum += sample * sample;
        decay_curve[i] = sum;
    }

    // 最大値で正規化してdBに変換
    double max_energy = decay_curve[0];
    if (max_energy > 0) {
        for (int i = 0; i < len; i++) {
            if (decay_curve[i] > 0) {
                decay_curve[i] = 10.0 * log10(decay_curve[i] / max_energy);
            } else {
                decay_curve[i] = -100.0; // 十分に小さい値
            }
        }
    }
}

/**
 * 線形回帰で減衰時間を計算
 * start_db から end_db の区間で傾きを求め、T10/T20（そのdB範囲の減衰時間）を算出
 * 戻り値: T10 または T20（秒）、エラー時は-1
 */
double calculate_decay_time(double *decay_curve, int len, int fs, 
                            double start_db, double end_db, int *start_idx, int *end_idx) {
    // start_db から end_db までの区間を探す
    *start_idx = -1;
    *end_idx = -1;

    for (int i = 0; i < len; i++) {
        if (*start_idx < 0 && decay_curve[i] <= start_db && decay_curve[i] >= end_db) {
            *start_idx = i;
        }
        if (decay_curve[i] <= end_db) {
            *end_idx = i;
            break;
        }
    }

    if (*start_idx < 0 || *end_idx < 0 || *end_idx <= *start_idx) {
        return -1.0;
    }

    // 線形回帰（最小二乗法）
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0;
    int n = *end_idx - *start_idx + 1;

    for (int i = *start_idx; i <= *end_idx; i++) {
        double x = (double)i / fs; // 時間（秒）
        double y = decay_curve[i]; // dB
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }

    // 傾き a = (n*Σxy - Σx*Σy) / (n*Σx² - (Σx)²)
    double denominator = n * sum_x2 - sum_x * sum_x;
    if (fabs(denominator) < 1e-10) {
        return -1.0;
    }

    double slope = (n * sum_xy - sum_x * sum_y) / denominator;

    // 減衰していない場合はエラー
    if (slope >= 0) {
        return -1.0;
    }

    // T10 または T20: そのdB範囲の減衰時間 = |end_db - start_db| / |slope|
    double db_range = fabs(start_db - end_db);
    double t_decay = db_range / (-slope);
    return t_decay;
}

int main(int argc, char *argv[]) {
    const char *ir_file = (argc > 1) ? argv[1] : "impulse_response.wav";

    printf("インパルス応答から残響時間を算出中...\n");
    printf("入力ファイル: %s\n", ir_file);

    // 1. WAVファイルを読み込む
    int16_t *ir_samples = NULL;
    int fs, num_samples;
    if (read_wav(ir_file, &ir_samples, &fs, &num_samples) < 0) {
        fprintf(stderr, "エラー: WAVファイルの読み込みに失敗\n");
        return 1;
    }

    printf("読み込み完了: %d サンプル, fs = %d Hz (%.3f 秒)\n", 
           num_samples, fs, (double)num_samples / fs);

    // 2. ノイズ対策: ノイズテールを打ち切り有効長を決定（T10算出を確実にする工夫）
    int eff_len = get_effective_ir_length(ir_samples, num_samples, -40.0);
    if (eff_len < num_samples) {
        printf("ノイズ対策: IRを %.3f 秒まで打ち切り（全長 %.3f 秒）\n",
               (double)eff_len / fs, (double)num_samples / fs);
    }

    // 3. Schroeder積分で残響曲線を計算（インパルス応答の2乗積分）
    double *decay_curve = (double *)malloc((size_t)eff_len * sizeof(double));
    schroeder_integral(ir_samples, eff_len, decay_curve);

    // 4. T10を算出（残響曲線の -5 dB から -15 dB の区間）
    int t10_start, t10_end;
    double t10 = calculate_decay_time(decay_curve, eff_len, fs, -5.0, -15.0, &t10_start, &t10_end);
    double rt60_t10 = (t10 > 0) ? t10 * 6.0 : -1.0;  /* T10から60dB減衰時間を近似: RT60 = T10 × 6 */

    // 5. T20を算出（残響曲線の -5 dB から -25 dB の区間）
    int t20_start, t20_end;
    double t20 = calculate_decay_time(decay_curve, eff_len, fs, -5.0, -25.0, &t20_start, &t20_end);
    double rt60_t20 = (t20 > 0) ? t20 * 3.0 : -1.0; // T20からRT60を外挿

    // 5. 結果を表示
    printf("\n=== 残響時間解析結果 ===\n");
    
    if (t10 > 0) {
        printf("T10: %.3f 秒 (区間: %.3f - %.3f 秒)\n", 
               t10, (double)t10_start / fs, (double)t10_end / fs);
        printf("RT60 (T10から): %.3f 秒\n", rt60_t10);
    } else {
        printf("T10: 計算できませんでした（ノイズレベルが高い可能性）\n");
    }

    if (t20 > 0) {
        printf("T20: %.3f 秒 (区間: %.3f - %.3f 秒)\n", 
               t20, (double)t20_start / fs, (double)t20_end / fs);
        printf("RT60 (T20から): %.3f 秒\n", rt60_t20);
    } else {
        printf("T20: 計算できませんでした（ノイズレベルが高い可能性）\n");
    }

    // 6. 残響曲線をファイルに出力（オプション）
    if (argc > 2) {
        const char *curve_file = argv[2];
        FILE *fp = fopen(curve_file, "w");
        if (fp) {
            fprintf(fp, "# 時間(秒)\tエネルギー(dB)\n");
            for (int i = 0; i < eff_len; i++) {
                fprintf(fp, "%.6f\t%.2f\n", (double)i / fs, decay_curve[i]);
            }
            fclose(fp);
            printf("\n残響曲線を %s に保存しました\n", curve_file);
        }
    }

    // メモリ解放
    free(ir_samples);
    free(decay_curve);

    return 0;
}
