/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_REGION_H__
#define __I915_GEM_REGION_H__

#include <linux/types.h>

#include "gt/intel_gt_types.h"

struct intel_memory_region;
struct drm_i915_gem_object;
struct sg_table;

struct sg_table *
i915_gem_object_get_pages_buddy(struct drm_i915_gem_object *obj,
				unsigned int *page_sizes);
int i915_gem_object_put_pages_buddy(struct drm_i915_gem_object *obj,
				    struct sg_table *pages,
				    bool dirty);

void i915_gem_object_init_memory_region(struct drm_i915_gem_object *obj,
					struct intel_memory_region *mem);
void i915_gem_object_release_memory_region(struct drm_i915_gem_object *obj);

struct drm_i915_gem_object *
i915_gem_object_create_region(struct intel_memory_region *mem,
			      resource_size_t size,
			      unsigned int flags);

static inline struct drm_i915_gem_object *
intel_gt_object_create_lmem(struct intel_gt *gt,
			    resource_size_t size,
			    unsigned int flags)
{
	return i915_gem_object_create_region(gt->lmem, size, flags);
}

/* i915_modparams.force_alloc_contig flags */
#define ALLOC_CONTIGUOUS_SMEM BIT(0)
#define ALLOC_CONTIGUOUS_LMEM BIT(1)
#define ALLOC_CONTIGUOUS_FLAGS (ALLOC_CONTIGUOUS_SMEM | ALLOC_CONTIGUOUS_LMEM)

#endif
