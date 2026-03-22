#include "qrcodegen.h"
#include <string.h>

/* Minimal placeholder implementation:
 * - Encodes nothing; produces a fixed 21x21 QR-like pattern.
 * - Good enough for local UI rendering while real library is absent.
 */

bool qrcodegen_encodeText(const char * text, uint8_t * tempBuffer, uint8_t * qrcode,
                          qrcodegen_Ecc ecl, int minVersion, int maxVersion,
                          qrcodegen_Mask mask, bool boostEcl) {
    (void)text; (void)tempBuffer; (void)ecl; (void)minVersion; (void)maxVersion; (void)mask; (void)boostEcl;
    memset(qrcode, 0, qrcodegen_BUFFER_LEN_MAX);
    /* Store size in first byte for this stub. */
    qrcode[0] = 21;
    return true;
}

int qrcodegen_getSize(const uint8_t * qrcode) {
    return qrcode ? qrcode[0] : 0;
}

bool qrcodegen_getModule(const uint8_t * qrcode, int x, int y) {
    (void)qrcode;
    /* Simple checkerboard */
    return ((x ^ y) & 1) == 0;
}
