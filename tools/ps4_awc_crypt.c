#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define DELTA 0x9e3779b9
static const uint32_t KEY[4] = {0xf34b1262, 0xcfb0fe66, 0xec500648, 0xb90ace74};

static void btea_encrypt(uint32_t *v, size_t n, const uint32_t *k) {
    uint32_t rounds = 6 + 52 / n;
    uint32_t sum = 0;
    uint32_t z = v[n - 1];
    for (uint32_t r = 0; r < rounds; r++) {
        sum += DELTA;
        uint32_t e = (sum >> 2) & 3;
        for (size_t p = 0; p < n; p++) {
            uint32_t y = (p < n - 1) ? v[p + 1] : v[0];
            uint32_t mx = (((z >> 5 ^ y << 2) + (y >> 3 ^ z << 4)) ^ ((sum ^ y) + (k[(p & 3) ^ e] ^ z)));
            v[p] += mx;
            z = v[p];
        }
    }
}

static void btea_decrypt(uint32_t *v, size_t n, const uint32_t *k) {
    uint32_t rounds = 6 + 52 / n;
    uint32_t sum = rounds * DELTA;
    uint32_t y = v[0];
    for (uint32_t r = 0; r < rounds; r++) {
        uint32_t e = (sum >> 2) & 3;
        for (size_t p = n; p > 0; p--) {
            size_t idx = p - 1;
            uint32_t z = (idx > 0) ? v[idx - 1] : v[n - 1];
            uint32_t mx = (((z >> 5 ^ y << 2) + (y >> 3 ^ z << 4)) ^ ((sum ^ y) + (k[(idx & 3) ^ e] ^ z)));
            v[idx] -= mx;
            y = v[idx];
        }
        sum -= DELTA;
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: ps4_awc_crypt <-e|-d> <input.awc> <output.awc>\n");
        printf("  -e: Encrypt PC AWC for PS4/PS5\n");
        printf("  -d: Decrypt PS4/PS5 AWC to PC format\n");
        return 1;
    }

    int encrypt = (strcmp(argv[1], "-e") == 0);
    const char *in_path = argv[2];
    const char *out_path = argv[3];

    FILE *fin = fopen(in_path, "rb");
    if (!fin) {
        fprintf(stderr, "Error: cannot open input file '%s'\n", in_path);
        return 1;
    }

    fseek(fin, 0, SEEK_END);
    long fsize = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    if (fsize < 16) {
        fprintf(stderr, "Error: file too small (%ld bytes)\n", fsize);
        fclose(fin);
        return 1;
    }

    size_t pad = (size_t)((4 - (fsize % 4)) % 4);
    size_t alloc_size = (size_t)fsize + pad;

    uint8_t *buf = (uint8_t *)calloc(1, alloc_size);
    if (!buf) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(fin);
        return 1;
    }

    if (fread(buf, 1, fsize, fin) != (size_t)fsize) {
        fprintf(stderr, "Error: failed to read input file\n");
        free(buf);
        fclose(fin);
        return 1;
    }
    fclose(fin);

    size_t n_words = alloc_size / 4;
    uint32_t *words = (uint32_t *)buf;

    if (encrypt) {
        // Ensure magic is 'ADAT' (0x54414441)
        if (memcmp(buf, "ADAT", 4) != 0) {
            fprintf(stderr, "Warning: magic is not ADAT (got %c%c%c%c)\n",
                    buf[0], buf[1], buf[2], buf[3]);
        }
        // Set flags byte for PS4 encrypted container:
        // byte 4 = 1 (ver), byte 5 = 0, byte 6 = 0x08, byte 7 = 0xff
        buf[4] = 0x01;
        buf[5] = 0x00;
        buf[6] = 0x08; // PS4 encrypted flag
        buf[7] = 0xff;

        printf("[+] Encrypting %zu bytes (%zu words, pad=%zu) with PS4 XXTEA...\n",
               alloc_size, n_words, pad);
        btea_encrypt(words, n_words, KEY);
    } else {
        printf("[+] Decrypting %zu bytes (%zu words) with PS4 XXTEA...\n",
               alloc_size, n_words);
        btea_decrypt(words, n_words, KEY);

        if (memcmp(buf, "ADAT", 4) != 0) {
            fprintf(stderr, "Error: decrypted data does not start with ADAT!\n");
            free(buf);
            return 1;
        }
        // If decrypted, change flag byte 6 from 0x08 to 0x0a (unencrypted with indices) or 0x01
        buf[6] = 0x0a;
        printf("[+] Verified decrypted ADAT header successfully.\n");
    }

    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        fprintf(stderr, "Error: cannot open output file '%s'\n", out_path);
        free(buf);
        return 1;
    }

    fwrite(buf, 1, alloc_size, fout);
    fclose(fout);
    free(buf);

    printf("[+] Successfully wrote '%s' (%zu bytes)\n", out_path, alloc_size);
    return 0;
}
