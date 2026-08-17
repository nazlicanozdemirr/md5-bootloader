/**
 * @file md5.c
 * @brief STM32 üzerinde kullanılmak üzere RFC 1321 uyumlu MD5 hesaplaması.
 */

#include "md5.h"
#include <string.h>

#define F(x,y,z) (((x) & (y)) | ((~(x)) & (z)))
#define G(x,y,z) (((x) & (z)) | ((y) & (~(z))))
#define H(x,y,z) ((x) ^ (y) ^ (z))
#define I(x,y,z) ((y) ^ ((x) | (~(z))))

#define ROTLEFT(a,b) (((a) << (b)) | ((a) >> (32u - (b))))

#define FF(a, b, c, d, x, s, ac) do { \
  (a) += F((b), (c), (d)) + (x) + (uint32_t)(ac); \
  (a) = ROTLEFT((a), (s)); \
  (a) += (b); \
} while(0)
#define GG(a, b, c, d, x, s, ac) do { \
  (a) += G((b), (c), (d)) + (x) + (uint32_t)(ac); \
  (a) = ROTLEFT((a), (s)); \
  (a) += (b); \
} while(0)
#define HH(a, b, c, d, x, s, ac) do { \
  (a) += H((b), (c), (d)) + (x) + (uint32_t)(ac); \
  (a) = ROTLEFT((a), (s)); \
  (a) += (b); \
} while(0)
#define II(a, b, c, d, x, s, ac) do { \
  (a) += I((b), (c), (d)) + (x) + (uint32_t)(ac); \
  (a) = ROTLEFT((a), (s)); \
  (a) += (b); \
} while(0)

#define S11  7u
#define S12 12u
#define S13 17u
#define S14 22u
#define S21  5u
#define S22  9u
#define S23 14u
#define S24 20u
#define S31  4u
#define S32 11u
#define S33 16u
#define S34 23u
#define S41  6u
#define S42 10u
#define S43 15u
#define S44 21u

#define ROTATE_STATE(a, b, c, d) do { \
  uint32_t _t = (a); (a) = (d); (d) = (c); (c) = (b); (b) = _t; \
} while(0)

static const uint32_t K[64] = {
  0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
  0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
  0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
  0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
  0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
  0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
  0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
  0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
  0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
  0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
  0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
  0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
  0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
  0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
  0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
  0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

/**
 * @brief MD5 round ve adım indeksine karşılık gelen bit döndürme miktarını döndürür.
 */
static inline uint32_t md5_shift(unsigned r, unsigned i)
{
  const uint8_t S[16] = {
    S11, S12, S13, S14,  S21, S22, S23, S24,
    S31, S32, S33, S34,  S41, S42, S43, S44
  };
  return (uint32_t)S[(r << 2) + (i & 3u)];
}

/**
 * @brief 64 baytlık bir blok üzerinde tek MD5 dönüşüm turunu gerçekleştirir.
 */
static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t x[16];
  uint32_t i;

  for (i = 0; i < 16; i++) {
    x[i] = (uint32_t)block[i*4]
         | ((uint32_t)block[i*4+1] << 8)
         | ((uint32_t)block[i*4+2] << 16)
         | ((uint32_t)block[i*4+3] << 24);
  }

  for (i = 0; i < 16; i++) {
    FF(a, b, c, d, x[i], md5_shift(0, i), K[i]);
    ROTATE_STATE(a, b, c, d);
  }
  for (i = 0; i < 16; i++) {
    GG(a, b, c, d, x[(5u * i + 1u) % 16u], md5_shift(1, i), K[16u + i]);
    ROTATE_STATE(a, b, c, d);
  }
  for (i = 0; i < 16; i++) {
    HH(a, b, c, d, x[(3u * i + 5u) % 16u], md5_shift(2, i), K[32u + i]);
    ROTATE_STATE(a, b, c, d);
  }
  for (i = 0; i < 16; i++) {
    II(a, b, c, d, x[(7u * i) % 16u], md5_shift(3, i), K[48u + i]);
    ROTATE_STATE(a, b, c, d);
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

static void md5_encode(uint8_t *out, const uint32_t *in, uint32_t len)
{
  uint32_t i, j;
  for (i = 0, j = 0; j < len; i++, j += 4) {
    out[j]   = (uint8_t)(in[i] & 0xffu);
    out[j+1] = (uint8_t)((in[i] >> 8) & 0xffu);
    out[j+2] = (uint8_t)((in[i] >> 16) & 0xffu);
    out[j+3] = (uint8_t)((in[i] >> 24) & 0xffu);
  }
}

/**
 * @brief MD5 context yapısını başlangıç sabitleriyle başlatır.
 */
void MD5_Init(MD5_Context_t *ctx)
{
  if (!ctx) return;
  ctx->state[0] = 0x67452301u;
  ctx->state[1] = 0xefcdab89u;
  ctx->state[2] = 0x98badcfeu;
  ctx->state[3] = 0x10325476u;
  ctx->count[0] = 0;
  ctx->count[1] = 0;
}

/**
 * @brief MD5 context'ine yeni veri bloğu ekler.
 */
void MD5_Update(MD5_Context_t *ctx, const uint8_t *data, uint32_t len)
{
  uint32_t i, index, partLen;

  if (!ctx || !data) return;

  index = (uint32_t)((ctx->count[0] >> 3) & 0x3Fu);
  ctx->count[0] += (len << 3);
  if (ctx->count[0] < (len << 3))
    ctx->count[1]++;
  ctx->count[1] += (len >> 29);

  partLen = 64u - index;
  if (len >= partLen) {
    memcpy(&ctx->buffer[index], data, partLen);
    md5_transform(ctx->state, ctx->buffer);
    for (i = partLen; i + 63 < len; i += 64)
      md5_transform(ctx->state, &data[i]);
    index = 0;
  } else {
    i = 0;
  }
  if (i < len)
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

/**
 * @brief MD5 hesaplamasını tamamlayıp 16 baytlık özeti üretir.
 */
void MD5_Final(MD5_Context_t *ctx, uint8_t out[MD5_DIGEST_LEN])
{
  uint8_t bits[8];
  uint32_t index, padLen;

  if (!ctx || !out) return;

  md5_encode(bits, ctx->count, 8);
  index = (uint32_t)((ctx->count[0] >> 3) & 0x3Fu);
  padLen = (index < 56) ? (56 - index) : (120 - index);
  {
    uint8_t padding[64];
    uint32_t k;
    padding[0] = 0x80;
    for (k = 1; k < 64; k++) padding[k] = 0;
    MD5_Update(ctx, padding, padLen);
  }
  MD5_Update(ctx, bits, 8);
  md5_encode(out, ctx->state, MD5_DIGEST_LEN);
}
