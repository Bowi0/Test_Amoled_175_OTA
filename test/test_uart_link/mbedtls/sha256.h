#ifndef TEST_SHA256_H
#define TEST_SHA256_H
#include <cstddef>
#include <cstdint>
struct mbedtls_sha256_context { uint8_t digest[32]; size_t offset; };
void mbedtls_sha256_init(mbedtls_sha256_context *);
int mbedtls_sha256_starts(mbedtls_sha256_context *, int);
int mbedtls_sha256_update(mbedtls_sha256_context *, const unsigned char *, size_t);
int mbedtls_sha256_finish(mbedtls_sha256_context *, unsigned char *);
void mbedtls_sha256_free(mbedtls_sha256_context *);
#endif
