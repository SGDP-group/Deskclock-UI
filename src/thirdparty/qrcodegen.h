#ifndef QRCODEGEN_H
#define QRCODEGEN_H

#include <stdbool.h>
#include <stdint.h>

#define qrcodegen_VERSION_MIN 1
#define qrcodegen_VERSION_MAX 40
#define qrcodegen_BUFFER_LEN_MAX 3917 /* supports version 40; here small */

typedef enum {
    qrcodegen_Ecc_LOW = 0,
    qrcodegen_Ecc_MEDIUM,
    qrcodegen_Ecc_QUARTILE,
    qrcodegen_Ecc_HIGH
} qrcodegen_Ecc;

typedef enum {
    qrcodegen_Mask_AUTO = -1
} qrcodegen_Mask;

bool qrcodegen_encodeText(const char * text, uint8_t * tempBuffer, uint8_t * qrcode,
                          qrcodegen_Ecc ecl, int minVersion, int maxVersion,
                          qrcodegen_Mask mask, bool boostEcl);

int  qrcodegen_getSize(const uint8_t * qrcode);
bool qrcodegen_getModule(const uint8_t * qrcode, int x, int y);

#endif /* QRCODEGEN_H */
