/**
 * @file md5.h
 * @brief STM32 için hafif, RFC 1321 uyumlu MD5 arayüzü.
 */

#ifndef __MD5_H
#define __MD5_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MD5_DIGEST_LEN 16u

typedef struct {
  uint32_t state[4];
  uint32_t count[2];
  uint8_t  buffer[64];
} MD5_Context_t;

/**
 * @brief MD5 context yapısını başlangıç değerleriyle sıfırlar.
 */
void MD5_Init(MD5_Context_t *ctx);

/**
 * @brief MD5 hesabına yeni veri bloğu ekler.
 */
void MD5_Update(MD5_Context_t *ctx, const uint8_t *data, uint32_t len);

/**
 * @brief MD5 hesabını tamamlar ve özeti 16 baytlık diziye yazar.
 */
void MD5_Final(MD5_Context_t *ctx, uint8_t out[MD5_DIGEST_LEN]);

#ifdef __cplusplus
}
#endif

#endif
