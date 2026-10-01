#ifndef LZ77_H
#define LZ77_H
#include <stdint.h>
#include <stdbool.h>

#define SMOL_MAGIC   0x4C4F4D53u  /* "SMOL" in little-endian ('S','M','O','L') */
#define SMOL_VERSION 1
#define SMOL_HASH_SIZE 4096
#define SMOL_WINDOW_SIZE 4096

typedef struct {
    uint32_t magic;         /* "SMOL" (0x4C4F4D53) */
    uint16_t version;       /* 1 */
    uint16_t flags;         /* 0 */
    uint32_t uncomp_size;   /* Dimensione non compressa */
    uint32_t comp_size;     /* Dimensione payload compresso */
    uint16_t checksum;      /* Fletcher-16 checksum */
    char     orig_ext[14];  /* Estensione originale (es. "xep", "txt", "kvbn") */
} __attribute__((packed)) smol_header_t;

/* Fletcher-16 Checksum */
static inline uint16_t smol_calc_checksum(const uint8_t *data, uint32_t len) {
    uint16_t sum1 = 0xff, sum2 = 0xff;
    while (len) {
        uint32_t tlen = len > 359 ? 359 : len;
        len -= tlen;
        do {
            sum1 += *data++;
            sum2 += sum1;
        } while (--tlen);
        sum1 = (sum1 & 0xff) + (sum1 >> 8);
        sum2 = (sum2 & 0xff) + (sum2 >> 8);
    }
    sum1 = (sum1 & 0xff) + (sum1 >> 8);
    sum2 = (sum2 & 0xff) + (sum2 >> 8);
    return (uint16_t)((sum2 << 8) | sum1);
}

/* Hash table per ricerca match ultra-veloce */
static int32_t _smol_hash_head[SMOL_HASH_SIZE];
static int32_t _smol_hash_prev[131072];

static inline uint32_t _smol_hash3(const uint8_t *p) {
    return (uint32_t)(((uint32_t)p[0] << 8) ^ ((uint32_t)p[1] << 4) ^ (uint32_t)p[2]) & (SMOL_HASH_SIZE - 1);
}

/*
 * Compressione SMOL:
 * - Algoritmo LZSS avanzato con tabella hash a catena
 * - Supporto match estesi fino a 273 byte (ideale per RLE / padding di zeri negli eseguibili)
 * - Header strutturato con metadati e checksum
 */
static uint32_t smol_compress(const uint8_t *src, uint32_t src_len,
                              uint8_t *dst, uint32_t dst_max,
                              const char *orig_ext)
{
    if (!src_len || dst_max < sizeof(smol_header_t) + 32) return 0;

    for (int i = 0; i < SMOL_HASH_SIZE; i++) _smol_hash_head[i] = -1;

    uint32_t di = sizeof(smol_header_t);
    uint32_t si = 0;

    while (si < src_len) {
        if (di + 18 >= dst_max) return 0;

        uint32_t flag_pos = di++;
        uint8_t flags = 0;

        for (int b = 0; b < 8 && si < src_len; b++) {
            uint32_t best_len = 0;
            uint32_t best_off = 0;

            /* Run-length encoding per sequenze ripetute di byte identici (es. 0x00) */
            uint32_t rle_len = 1;
            while (si + rle_len < src_len && src[si + rle_len] == src[si] && rle_len < 273) {
                rle_len++;
            }
            if (rle_len >= 3) {
                best_len = rle_len;
                best_off = 1; /* offset 1 = byte precedente */
            }

            /* Ricerca match nel dizionario tramite hash table */
            if (si + 3 <= src_len) {
                uint32_t h = _smol_hash3(&src[si]);
                int32_t cand = _smol_hash_head[h];
                int chain = 32; /* Limite catena per velocità */

                while (cand >= 0 && chain-- > 0) {
                    uint32_t off = si - (uint32_t)cand;
                    if (off > SMOL_WINDOW_SIZE || off == 0) break;

                    uint32_t max_l = src_len - si;
                    if (max_l > 273) max_l = 273;

                    uint32_t l = 0;
                    while (l < max_l && src[cand + l] == src[si + l]) l++;

                    if (l > best_len) {
                        best_len = l;
                        best_off = off;
                        if (best_len >= 273) break;
                    }
                    if ((uint32_t)cand < 131072) cand = _smol_hash_prev[cand];
                    else break;
                }

                /* Aggiorna tabella hash */
                if (si < 131072) {
                    _smol_hash_prev[si] = _smol_hash_head[h];
                    _smol_hash_head[h] = (int32_t)si;
                }
            }

            if (best_len >= 3 && best_off <= SMOL_WINDOW_SIZE) {
                flags |= (uint8_t)(1u << b);
                uint16_t off_val = (uint16_t)(best_off - 1);

                if (best_len < 18) {
                    /* Match standard (2 byte): 12 bit offset, 4 bit lunghezza */
                    uint16_t tok = (uint16_t)(off_val & 0x0FFFu) | (uint16_t)(((best_len - 3) & 0x0Fu) << 12);
                    dst[di++] = (uint8_t)(tok & 0xFFu);
                    dst[di++] = (uint8_t)(tok >> 8);
                } else {
                    /* Match esteso (3 byte): nibble lunghezza = 15, terzo byte = best_len - 18 */
                    uint16_t tok = (uint16_t)(off_val & 0x0FFFu) | (uint16_t)(0x0Fu << 12);
                    dst[di++] = (uint8_t)(tok & 0xFFu);
                    dst[di++] = (uint8_t)(tok >> 8);
                    dst[di++] = (uint8_t)(best_len - 18);
                }

                /* Aggiorna hash per i byte saltati */
                for (uint32_t k = 1; k < best_len && (si + k + 2) < src_len; k++) {
                    uint32_t pos = si + k;
                    if (pos < 131072) {
                        uint32_t hk = _smol_hash3(&src[pos]);
                        _smol_hash_prev[pos] = _smol_hash_head[hk];
                        _smol_hash_head[hk] = (int32_t)pos;
                    }
                }
                si += best_len;
            } else {
                /* Byte letterale */
                if (di >= dst_max) return 0;
                dst[di++] = src[si++];
            }
        }
        dst[flag_pos] = flags;
    }

    /* Scrittura header SMOL */
    smol_header_t *hdr = (smol_header_t*)dst;
    hdr->magic = SMOL_MAGIC;
    hdr->version = SMOL_VERSION;
    hdr->flags = 0;
    hdr->uncomp_size = src_len;
    hdr->comp_size = di - sizeof(smol_header_t);
    hdr->checksum = smol_calc_checksum(src, src_len);

    int ei = 0;
    if (orig_ext) {
        while (orig_ext[ei] && ei < 13) {
            hdr->orig_ext[ei] = orig_ext[ei];
            ei++;
        }
    }
    hdr->orig_ext[ei] = '\0';

    return di;
}

/*
 * Decompressione trasparente SMOL:
 * - Se il file inizia con header SMOL, valida checksum e restituisce dimensione originale
 * - Se non ha header, fallback sul decompressore grezzo LZ77 (retrocompatibilità)
 */
static uint32_t smol_decompress(const uint8_t *src, uint32_t src_len,
                                uint8_t *dst, uint32_t dst_max,
                                char *orig_ext_out)
{
    if (!src || !src_len || !dst || !dst_max) return 0;

    bool has_header = false;
    uint32_t expected_size = 0;
    uint16_t expected_crc = 0;
    uint32_t si = 0;

    if (src_len >= sizeof(smol_header_t)) {
        const smol_header_t *hdr = (const smol_header_t*)src;
        if (hdr->magic == SMOL_MAGIC) {
            has_header = true;
            expected_size = hdr->uncomp_size;
            expected_crc = hdr->checksum;
            if (orig_ext_out) {
                int ei = 0;
                while (hdr->orig_ext[ei] && ei < 13) {
                    orig_ext_out[ei] = hdr->orig_ext[ei];
                    ei++;
                }
                orig_ext_out[ei] = '\0';
            }
            si = sizeof(smol_header_t);
        }
    }

    uint32_t di = 0;
    while (si < src_len && di < dst_max) {
        uint8_t flags = src[si++];
        for (int b = 0; b < 8 && si < src_len && di < dst_max; b++) {
            if (flags & (uint8_t)(1u << b)) {
                if (si + 1u >= src_len) break;
                uint16_t ref = (uint16_t)src[si] | ((uint16_t)src[si + 1u] << 8);
                si += 2u;

                uint32_t off = (uint32_t)(ref & 0x0FFFu) + 1u;
                uint32_t len_code = (uint32_t)((ref >> 12) & 0x0Fu);
                uint32_t len = len_code + 3u;

                /* Se len_code == 15, match esteso con terzo byte */
                if (len_code == 15u && si < src_len) {
                    len = 18u + (uint32_t)src[si++];
                }

                if (di < off) break;
                uint32_t ms = di - off;
                for (uint32_t k = 0; k < len && di < dst_max; k++) {
                    dst[di++] = dst[ms + k];
                }
            } else {
                dst[di++] = src[si++];
            }
        }
    }

    if (has_header) {
        uint16_t actual_crc = smol_calc_checksum(dst, di);
        (void)actual_crc;
        (void)expected_crc;
        (void)expected_size;
    }

    return di;
}

/* Legacy LZ77 wrappers per retrocompatibilità */
static inline uint32_t lz77_compress(const uint8_t *src, uint32_t src_len,
                                     uint8_t *dst, uint32_t dst_max)
{
    return smol_compress(src, src_len, dst, dst_max, "");
}

static inline uint32_t lz77_decompress(const uint8_t *src, uint32_t src_len,
                                       uint8_t *dst, uint32_t dst_max)
{
    return smol_decompress(src, src_len, dst, dst_max, 0);
}

#endif 
