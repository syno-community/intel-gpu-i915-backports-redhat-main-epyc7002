/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_PMT_TELEM_H
#define _INTEL_PMT_TELEM_H

#include <linux/pm_runtime.h>

/* Telemetry types */
#define PMT_TELEM_TELEMETRY	0
#define PMT_TELEM_CRASHLOG	1

struct telem_endpoint;

struct telem_header {
	u8	access_type;
	u16	size;
	u32	guid;
	u32	base_offset;
};

struct telem_endpoint_info {
	struct pci_dev		*pdev;
	struct telem_header	header;
};

struct pci_dev;

/**
 * pmt_telem_read() - Read qwords from counter sram using sample id
 * @pdev:   PCI device inside the Intel vsec
 * @guid:   GUID of the telemetry space
 * @pos:    Instance of the guid in case of multiple instances
 * @id:     The beginning sample id of the metric(s) to be read
 * @count:  Number of qwords requested
 * @data:   Allocated qword buffer
 * * -ENODEV     - The device is not present.
 * * -EINVAL     - The offset is out out bounds
 */
int pmt_telem_read64(struct pci_dev *pdev, u32 guid, u16 pos, u16 id, u16 count, u64 *data);

/* TELEMETRY API */

/**
 * pmt_telem_get_next_endpoint() - Get next device id for a telemetry endpoint
 * @start:  starting devid to look from
 *
 * This functions can be used in a while loop predicate to retrieve the devid
 * of all available telemetry endpoints. Functions pmt_telem_get_next_endpoint()
 * and pmt_telem_register_endpoint() can be used inside of the loop to examine
 * endpoint info and register to receive a pointer to the endpoint. The pointer
 * is then usable in the telemetry read calls to access the telemetry data.
 *
 * Return:
 * * devid       - devid of the next present endpoint from start
 * * 0           - when no more endpoints are present after start
 */
int pmt_telem_get_next_endpoint(int start);

/**
 * pmt_telem_register_endpoint() - Register a telemetry endpoint
 * @devid: device id/handle of the telemetry endpoint
 *
 * Increments the kref usage counter for the endpoint.
 *
 * Return:
 * * endpoint    - On success returns pointer to the telemetry endpoint
 * * -ENXIO      - telemetry endpoint not found
 */
struct telem_endpoint *pmt_telem_register_endpoint(int devid);

/**
 * pmt_telem_unregister_endpoint() - Unregister a telemetry endpoint
 * @ep:   ep structure to populate.
 *
 * Decrements the kref usage counter for the endpoint.
 */
void pmt_telem_unregister_endpoint(struct telem_endpoint *ep);

/**
 * pmt_telem_get_endpoint_info() - Get info for an endpoint from its devid
 * @devid:  device id/handle of the telemetry endpoint
 * @info:   Endpoint info structure to be populated
 *
 * Return:
 * * 0           - Success
 * * -ENXIO      - telemetry endpoint not found for the devid
 * * -EINVAL     - @info is NULL
 */
int pmt_telem_get_endpoint_info(int devid,
				struct telem_endpoint_info *info);

/**
 * pmt_telem_find_and_register_endpoint() - Get a telemetry endpoint from
 * pci_dev device, guid and pos
 * @pdev:   PCI device inside the Intel vsec
 * @guid:   GUID of the telemetry space
 * @pos:    Instance of the guid in case of multiple instances
 *
 * Return:
 * * endpoint    - On success returns pointer to the telemetry endpoint
 * * -ENXIO      - telemetry endpoint not found
 */
struct telem_endpoint *pmt_telem_find_and_register_endpoint(struct pci_dev *pcidev,
				u32 guid, u16 pos);

/**
 * pmt_telem_read() - Read qwords from counter sram using sample id
 * @ep:     Telemetry endpoint to be read
 * @id:     The beginning sample id of the metric(s) to be read
 * @data:   Allocated qword buffer
 * @count:  Number of qwords requested
 *
 * Callers must ensure reads are aligned. When the call returns -ENODEV,
 * the device has been removed and callers should unregister the telemetry
 * endpoint.
 *
 * If calling from atomic context must take a runtime_pm reference on
 * the device by calling pmt_telem_runtime_pm_get first. When done call
 * pmt_telem_runtim_pm_put to release the reference.
 *
 * Return:
 * * 0           - Success
 * * -ENODEV	 - The device is not present.
 * * -EINVAL     - The offset is out bounds
 * * -EPIPE	 - The device was removed during the read. Data written
 *		   but should be considered invalid.
 */
int pmt_telem_read(struct telem_endpoint *ep, u32 id, u64 *data,
		   u32 count);

void pmt_telem_runtime_pm_get(struct telem_endpoint *ep);
void pmt_telem_runtime_pm_put(struct telem_endpoint *ep);
#endif
