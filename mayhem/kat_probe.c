/* Known-answer probe for libusb's descriptor/error APIs; built with normal flags, run by test.sh. */
#include <stdio.h>
#include <string.h>
#include <libusb.h>

int main(void)
{
	/* SuperSpeed USB device capability: LTM, speeds 0x0e, functionality 2, U1 10us, U2 255us */
	unsigned char ss[10] = { 10, LIBUSB_DT_DEVICE_CAPABILITY, LIBUSB_BT_SS_USB_DEVICE_CAPABILITY,
		0x02, 0x0e, 0x00, 0x02, 0x0a, 0xff, 0x00 };
	/* USB 2.0 extension: bmAttributes = 0x00000006 */
	unsigned char u2[7] = { 7, LIBUSB_DT_DEVICE_CAPABILITY, LIBUSB_BT_USB_2_0_EXTENSION,
		0x06, 0x00, 0x00, 0x00 };
	/* Container ID: UUID 00..0f */
	unsigned char cid[20] = { 20, LIBUSB_DT_DEVICE_CAPABILITY, LIBUSB_BT_CONTAINER_ID, 0 };
	struct libusb_ss_usb_device_capability_descriptor *dss = NULL;
	struct libusb_usb_2_0_extension_descriptor *d20 = NULL;
	struct libusb_container_id_descriptor *dcid = NULL;
	int i, r;

	for (i = 0; i < 16; i++)
		cid[4 + i] = (unsigned char)i;

	r = libusb_get_ss_usb_device_capability_descriptor(NULL, (struct libusb_bos_dev_capability_descriptor *)ss, &dss);
	printf("ss rc=%d\n", r);
	if (r == 0) {
		printf("ss attr=%u speeds=%u func=%u u1=%u u2=%u\n", dss->bmAttributes, dss->wSpeedSupported,
			dss->bFunctionalitySupport, dss->bU1DevExitLat, dss->bU2DevExitLat);
		libusb_free_ss_usb_device_capability_descriptor(dss);
	}

	r = libusb_get_usb_2_0_extension_descriptor(NULL, (struct libusb_bos_dev_capability_descriptor *)u2, &d20);
	printf("u2 rc=%d\n", r);
	if (r == 0) {
		printf("u2 attr=%u\n", d20->bmAttributes);
		libusb_free_usb_2_0_extension_descriptor(d20);
	}

	r = libusb_get_container_id_descriptor(NULL, (struct libusb_bos_dev_capability_descriptor *)cid, &dcid);
	printf("cid rc=%d\n", r);
	if (r == 0) {
		printf("cid uuid=");
		for (i = 0; i < 16; i++)
			printf("%02x", dcid->ContainerID[i]);
		printf("\n");
		libusb_free_container_id_descriptor(dcid);
	}

	/* truncated SS capability must be rejected */
	ss[0] = 5;
	r = libusb_get_ss_usb_device_capability_descriptor(NULL, (struct libusb_bos_dev_capability_descriptor *)ss, &dss);
	printf("ss-short rc=%d\n", r);

	printf("err %d=%s\n", LIBUSB_ERROR_TIMEOUT, libusb_error_name(LIBUSB_ERROR_TIMEOUT));
	printf("err %d=%s\n", LIBUSB_ERROR_NO_DEVICE, libusb_error_name(LIBUSB_ERROR_NO_DEVICE));
	printf("strerror=%s\n", libusb_strerror(LIBUSB_ERROR_ACCESS));
	printf("version=%u.%u.%u\n", libusb_get_version()->major, libusb_get_version()->minor, libusb_get_version()->micro);
	return 0;
}
