/* Mayhem-layer superset of tests/fuzz/fuzz_bos_descriptor.c (same target name, same code paths).
 *
 * Besides the three dev-cap parsers the upstream harness calls on a malloc'd capability, this one also
 * reaches the SuperSpeedPlus and Platform dev-cap parsers, parse_bos() (through libusb_get_bos_descriptor()
 * on a fake handle whose control transfers return the fuzz bytes), the dev-cap dispatch over the parsed BOS,
 * and the SuperSpeed endpoint-companion parser. No device is needed; the control-transfer stub only mocks
 * the USB bus. */

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
    (void)dev_handle; (void)bmRequestType; (void)bRequest; (void)wValue;
    (void)wIndex; (void)timeout;
    size_t n = g_resp_len < wLength ? g_resp_len : wLength;
    if (n)
        memcpy(data, g_resp, n);
    return (int)n;
}

static void parse_dev_cap(struct libusb_bos_dev_capability_descriptor *devcap) {
    struct libusb_usb_2_0_extension_descriptor *d20 = NULL;
    struct libusb_ss_usb_device_capability_descriptor *dss = NULL;
    struct libusb_ssplus_usb_device_capability_descriptor *dssp = NULL;
    struct libusb_container_id_descriptor *dcid = NULL;
    struct libusb_platform_descriptor *dplat = NULL;
    uint8_t type = devcap->bDevCapabilityType;

    devcap->bDevCapabilityType = LIBUSB_BT_USB_2_0_EXTENSION;
    (void)libusb_get_usb_2_0_extension_descriptor(NULL, devcap, &d20);
    libusb_free_usb_2_0_extension_descriptor(d20);

    devcap->bDevCapabilityType = LIBUSB_BT_SS_USB_DEVICE_CAPABILITY;
    (void)libusb_get_ss_usb_device_capability_descriptor(NULL, devcap, &dss);
    libusb_free_ss_usb_device_capability_descriptor(dss);

    devcap->bDevCapabilityType = LIBUSB_BT_SUPERSPEED_PLUS_CAPABILITY;
    (void)libusb_get_ssplus_usb_device_capability_descriptor(NULL, devcap, &dssp);
    libusb_free_ssplus_usb_device_capability_descriptor(dssp);

    devcap->bDevCapabilityType = LIBUSB_BT_CONTAINER_ID;
    (void)libusb_get_container_id_descriptor(NULL, devcap, &dcid);
    libusb_free_container_id_descriptor(dcid);

    devcap->bDevCapabilityType = LIBUSB_BT_PLATFORM_DESCRIPTOR;
    (void)libusb_get_platform_descriptor(NULL, devcap, &dplat);
    libusb_free_platform_descriptor(dplat);

    devcap->bDevCapabilityType = type;
}

/* libusb builds with -Werror=missing-prototypes, so the entry point
 * needs a declaration of its own. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!data)
        return 0;

    /* (1) Each dev-cap parser on a capability whose payload is the fuzz bytes (bLength must fit one byte). */
    uint8_t payload_len = (size > 252) ? 252 : (uint8_t)size;
    struct libusb_bos_dev_capability_descriptor *devcap =
        malloc(sizeof(*devcap) + payload_len);
    if (!devcap)
        return 0;
    devcap->bLength = (uint8_t)(3u + payload_len);
    devcap->bDescriptorType = LIBUSB_DT_DEVICE_CAPABILITY;
    devcap->bDevCapabilityType = 0;
    if (payload_len)
        memcpy(devcap->dev_capability_data, data, payload_len);
    parse_dev_cap(devcap);
    free(devcap);

    /* (2) The whole BOS descriptor as a device would return it: header read, full read, parse_bos(),
     * then every parsed capability through the dev-cap parsers. */
    struct libusb_device dev;
    struct libusb_device_handle handle;
    struct libusb_bos_descriptor *bos = NULL;
    memset(&dev, 0, sizeof(dev));
    memset(&handle, 0, sizeof(handle));
    handle.dev = &dev;
    g_resp = data;
    g_resp_len = size;
    if (libusb_get_bos_descriptor(&handle, &bos) == LIBUSB_SUCCESS && bos) {
        for (uint8_t i = 0; i < bos->bNumDeviceCaps; i++)
            parse_dev_cap(bos->dev_capability[i]);
        libusb_free_bos_descriptor(bos);
    }
    g_resp = NULL;
    g_resp_len = 0;

    /* (3) SuperSpeed endpoint companion lookup in an endpoint's extra bytes (exact-size copy for ASan). */
    if (size <= 4096) {
        struct libusb_endpoint_descriptor ep;
        struct libusb_ss_endpoint_companion_descriptor *comp = NULL;
        uint8_t *extra = malloc(size ? size : 1);
        if (!extra)
            return 0;
        if (size)
            memcpy(extra, data, size);
        memset(&ep, 0, sizeof(ep));
        ep.extra = extra;
        ep.extra_length = (int)size;
        (void)libusb_get_ss_endpoint_companion_descriptor(NULL, &ep, &comp);
        libusb_free_ss_endpoint_companion_descriptor(comp);
        free(extra);
    }
    return 0;
}
