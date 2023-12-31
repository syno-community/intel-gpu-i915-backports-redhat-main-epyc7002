/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2019 - 2022 Intel Corporation.
 */

#ifndef __INTEL_IAF_PLATFORM_H
#define __INTEL_IAF_PLATFORM_H

#define IAF_VERSION 1

#if IS_ENABLED(CONFIG_AUXILIARY_BUS)
#include <linux/auxiliary_bus.h>
#endif
#include <linux/types.h>

/**
 * enum product_type - Product type identifying the parent
 * @IAF_UNDEFINED: no product type hints
 * @IAF_PONTEVECCHIO: parent is a PVC
 * @IAF_PRODUCTS: end of the list
 *
 * i.e. Pontevecchio is the first product.  Other products would
 * be next generation GPU, NIC, etc.
 */
enum product_type {
	IAF_UNDEFINED,
	IAF_PONTEVECCHIO,
	IAF_PRODUCTS
};

/**
 * enum iaf_dev_event - events generated to the parent device
 * @IAF_DEV_REMOVE: Notify parent that device is being removed
 * @IAF_DEV_EVENTS: end of list
 *
 * Connectivity events, possible errors, etc.
 */
enum iaf_dev_event {
	IAF_DEV_REMOVE,
	IAF_DEV_EVENTS
};

/**
 * enum iaf_parent_event - Events generated by the parent device
 * @IAF_PARENT_PCIE_ERR: Parent had a PCI error
 * @IAF_PARENT_MAPPING_GET: Notify IAF of buffer mapping
 * @IAF_PARENT_MAPPING_PUT: Notify IAF of buffer unmapping
 *
 * These are examples.
 */
enum iaf_parent_event {
	IAF_PARENT_PCIE_ERR,
	IAF_PARENT_MAPPING_GET,
	IAF_PARENT_MAPPING_PUT,
};

/**
 * struct sd2sd_info - Subdevice to subdevice connectivity info
 * @bandwidth: in Gbps units not factoring in width or quality degredation
 * @latency: in 1/10 hops units
 */
struct sd2sd_info {
	u16 bandwidth;
	u16 latency;
};

/**
 * struct query_info - connectivity query response information
 * @src_cnt: Requester subdevice count
 * @dst_cnt: Destination path count
 * @sd2sd: array of src/dst bandwidth/latency information
 *
 * Query info will be a variably sized data structure allocated by the
 * IAF driver.  The access will be indexed by
 *    (src_index * dst_cnt) + dst_index
 *
 * The caller will need to free the buffer when done.
 */
struct query_info {
	u8 src_cnt;
	u8 dst_cnt;
	struct sd2sd_info sd2sd[];
};

/**
 * struct iaf_ops - Communication path from parent to IAF instance
 * @connectivity_query: Query a device for fabric_id connectivity
 * @parent_event: Any events needed by the IAF device
 *
 * connectivity_query() returns:
 *   a populated query_info on success,
 *   an ERR_PTR() on failure
 *
 */
struct iaf_ops {
	struct query_info *(*connectivity_query)(void *handle, u32 fabric_id);
	int (*parent_event)(void *handle, enum iaf_parent_event event);
};

struct dpa_space {
	u32 pkg_offset;
	u16 pkg_size;
};

/**
 * struct iaf_pdata - Platform specific data that needs to be shared
 * @version: PSD version information
 * @parent: Handle to use when calling the parent device
 * @product: a product hint for any necessary special case requirements
 * @index: unique device index. This will be part of the device name
 * @dpa: Device physical address offset and size
 * @sd_cnt: parent subdevice count
 * @socket_id: device socket information
 * @slot: PCI/CXL slot number
 * @aux_dev: Auxiliary bus device
 * @resources: Array of resources (Assigned by i915, the IRQ/MEM for the device)
 * @num_resources: number of resources in resources array
 * @register_dev: Register an IAF instance and ops with the parent device
 * @unregister_dev: Unregister an IAF instance from the parent device
 * @dev_event: Notify parent that an event has occurred
 */
struct iaf_pdata {
	u16 version;
	void *parent;
	enum product_type product;
	u16 index;
	struct dpa_space dpa;
	u8 sd_cnt;
	u8 socket_id;
	u8 slot;

#if IS_ENABLED(CONFIG_AUXILIARY_BUS)
	struct auxiliary_device aux_dev;
	struct resource *resources;
	u32 num_resources;
#endif

	int (*register_dev)(void *parent, void *handle, u32 fabric_id,
			    const struct iaf_ops *ops);
	void (*unregister_dev)(void *parent, const void *handle);
	int (*dev_event)(void *parent, const void *handle,
			 const enum iaf_dev_event event, void *event_data);
};

#endif /* __INTEL_IAF_PLATFORM_H */
