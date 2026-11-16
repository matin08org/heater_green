#ifndef ASSET_MANIFEST_H
#define ASSET_MANIFEST_H
#include <stdint.h>
#define ASSET_MAGIC 0x41535434UL
static inline uint32_t AssetManifest_CRC32(const volatile uint8_t *p,uint32_t length)
{
    uint32_t crc=0xFFFFFFFFU;
    while (length--) {
        crc^=*p++;
        for (unsigned bit=0;bit<8U;bit++) crc=(crc>>1)^((crc&1U)?0xEDB88320U:0U);
    }
    return ~crc;
}
static inline int AssetManifest_Valid(const volatile uint32_t *header,
    const volatile uint8_t *body,uint32_t expected_size,uint32_t expected_crc)
{
    if (!header || !body || !expected_size || expected_size>0x1FFF00U) return 0;
    if (header[0]!=ASSET_MAGIC || header[1]!=expected_size ||
        header[2]!=expected_crc || header[3]!=1U) return 0;
    return AssetManifest_CRC32(body,expected_size)==expected_crc;
}
#endif
