#ifndef BARE_ARM_ASM_BYTEORDER_H
#define BARE_ARM_ASM_BYTEORDER_H

#ifdef __ARMEB__

#define __BIG_ENDIAN
#define __BIG_ENDIAN_BITFIELD

#define __cpu_to_be16(x) (x)
#define __be16_to_cpu(x) (x)

#define __cpu_to_be32(x) (x)
#define __be32_to_cpu(x) (x)

#define __cpu_to_le16(x) __builtin_bswap16(x)
#define __le16_to_cpu(x) __builtin_bswap16(x)

#define __cpu_to_le32(x) __builtin_bswap32(x)
#define __le32_to_cpu(x) __builtin_bswap32(x)

#else

#define __LITTLE_ENDIAN
#define __LITTLE_ENDIAN_BITFIELD

#define __cpu_to_be16(x) __builtin_bswap16(x)
#define __be16_to_cpu(x) __builtin_bswap16(x)

#define __cpu_to_be32(x) __builtin_bswap32(x)
#define __be32_to_cpu(x) __builtin_bswap32(x)

#define __cpu_to_le16(x) (x)
#define __le16_to_cpu(x) (x)

#define __cpu_to_le32(x) (x)
#define __le32_to_cpu(x) (x)

#endif

#endif /* BARE_ARM_ASM_BYTEORDER_H */
