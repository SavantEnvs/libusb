/* Mayhem-layer superset of tests/fuzz/fuzz_control_setup.c (same target name, same code paths).
 *
 * Upstream's harness only builds a setup packet with libusb_fill_control_setup() and frees a transfer; that
 * header inline parses nothing. This one keeps that and also uses the same setup fields and payload to drive a
 * control-transfer round trip through the real libusb_get_string_descriptor_ascii() on a fake handle: the setup
 * fields choose the string index, buffer length and language ID, and the payload is the descriptor the
 * "device" answers with (a BOS read is answered from the same payload). The control-transfer stub only
 * mocks the USB bus. */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "libusbi.h"

static const uint8_t *g_resp;
static size_t g_resp_len;

int libusb_control_transfer(libusb_device_handle *dev_handle,
                            uint8_t bmRequestType, uint8_t bRequest,
                            uint16_t wValue, uint16_t wIndex,
                            unsigned char *data, uint16_t wLength,
                            unsigned int timeout) {
    (void)dev_handle; (void)bmRequestType; (void)bRequest; (void)timeout;
    if (wValue == (uint16_t)((LIBUSB_DT_STRING << 8) | 0)) {
        /* Language ID table: one entry, taken from the setup packet's wIndex. */
        uint8_t langs[4] = { 4, LIBUSB_DT_STRING, (uint8_t)(wIndex & 0xff), (uint8_t)(wIndex >> 8) };
        size_t n = wLength < sizeof(langs) ? wLength : sizeof(langs);
        memcpy(data, langs, n);
        return (int)n;
    }
    size_t n = g_resp_len < wLength ? g_resp_len : wLength;
    if (n)
        memcpy(data, g_resp, n);
    return (int)n;
}

/* libusb builds with -Werror=missing-prototypes, so the entry point
 * needs a declaration of its own. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    struct libusb_transfer *transfer;
    struct usbi_transfer *itransfer;
    unsigned char *buffer;
    unsigned char *ptr;
    uint8_t bmRequestType, bRequest;
    uint16_t wValue, wIndex, wLength;
    size_t payload_len, priv_size;

    /* The first 8 bytes become the setup fields, the rest is payload. */
    if (size < 8)
        return 0;

    bmRequestType = data[0];
    bRequest = data[1];
    wValue = (uint16_t)(data[2] | (data[3] << 8));
    wIndex = (uint16_t)(data[4] | (data[5] << 8));
    wLength = (uint16_t)(data[6] | (data[7] << 8));
    payload_len = size - 8;

    buffer = malloc(LIBUSB_CONTROL_SETUP_SIZE + payload_len);
    if (!buffer)
        return 0;
    memset(buffer, 0, LIBUSB_CONTROL_SETUP_SIZE);
    if (payload_len)
        memcpy(buffer + LIBUSB_CONTROL_SETUP_SIZE, data + 8, payload_len);

    transfer = libusb_alloc_transfer(0);
    if (!transfer) {
        free(buffer);
        return 0;
    }

    libusb_fill_control_setup(buffer, bmRequestType, bRequest, wValue, wIndex,
                              wLength);

    itransfer = LIBUSB_TRANSFER_TO_USBI_TRANSFER(transfer);
    usbi_mutex_destroy(&itransfer->lock);
    priv_size = PTR_ALIGN(usbi_backend.transfer_priv_size);
    ptr = (unsigned char *)itransfer - priv_size;
    free(ptr);
    free(buffer);

    /* String descriptor read: index from wValue, caller buffer 1..256 bytes from wLength, answer = payload
     * (exact-size copy so ASan sees reads past the end). */
    {
        struct libusb_device dev;
        struct libusb_device_handle handle;
        int outlen = 1 + (wLength & 0xff);
        unsigned char *out = malloc((size_t)outlen);
        uint8_t *resp = malloc(payload_len ? payload_len : 1);
        if (!out || !resp) {
            free(out);
            free(resp);
            return 0;
        }
        if (payload_len)
            memcpy(resp, data + 8, payload_len);
        memset(&dev, 0, sizeof(dev));
        memset(&handle, 0, sizeof(handle));
        handle.dev = &dev;
        g_resp = resp;
        g_resp_len = payload_len;
        (void)libusb_get_string_descriptor_ascii(&handle, (uint8_t)(wValue & 0xff), out, outlen);
        {
            /* GET_DESCRIPTOR(BOS) over the same fake bus: header read, full read, parse_bos(). */
            struct libusb_bos_descriptor *bos = NULL;
            if (libusb_get_bos_descriptor(&handle, &bos) == LIBUSB_SUCCESS)
                libusb_free_bos_descriptor(bos);
        }
        g_resp = NULL;
        g_resp_len = 0;
        free(out);
        free(resp);
    }
    return 0;
}
