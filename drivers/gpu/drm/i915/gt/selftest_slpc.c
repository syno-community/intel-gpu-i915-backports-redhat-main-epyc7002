// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "gt/uc/intel_guc_print.h"

#define NUM_STEPS 5
#define H2G_DELAY 50000
#define delay_for_h2g() usleep_range(H2G_DELAY, H2G_DELAY + 10000)
#define FREQUENCY_REQ_UNIT	DIV_ROUND_CLOSEST(GT_FREQUENCY_MULTIPLIER, \
						  GEN9_FREQ_SCALER)

static inline struct intel_guc *slpc_to_guc(struct intel_guc_slpc *slpc)
{
	return container_of(slpc, struct intel_guc, slpc);
}

static int slpc_set_min_freq(struct intel_guc_slpc *slpc, u32 freq)
{
	int ret;

	ret = intel_guc_slpc_set_min_freq(slpc, freq);
	if (ret)
		pr_err("Could not set min frequency to [%u]\n", freq);
	else /* Delay to ensure h2g completes */
		delay_for_h2g();

	return ret;
}

static int slpc_set_max_freq(struct intel_guc_slpc *slpc, u32 freq)
{
	int ret;

	ret = intel_guc_slpc_set_max_freq(slpc, freq);
	if (ret)
		pr_err("Could not set maximum frequency [%u]\n",
		       freq);
	else /* Delay to ensure h2g completes */
		delay_for_h2g();

	return ret;
}

static int live_slpc_clamp_min(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_gt *gt;
	struct intel_guc_slpc *slpc;
	struct intel_rps *rps;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	intel_wakeref_t wakeref;
	u32 slpc_min_freq, slpc_max_freq, saved_min_freq;
	int err = 0;
	unsigned int i;

	for_each_gt(gt, i915, i) {

		pr_info("Running on GT: %d", gt->info.id);

		slpc = &gt->uc.guc.slpc;
		rps = &gt->rps;

		if (!intel_uc_uses_guc_slpc(&gt->uc))
			return 0;

		if (igt_spinner_init(&spin, gt))
			return -ENOMEM;

		if (intel_guc_slpc_get_max_freq(slpc, &slpc_max_freq)) {
			pr_err("Could not get SLPC max freq\n");
			return -EIO;
		}

		if (intel_guc_slpc_get_min_freq(slpc, &slpc_min_freq)) {
			pr_err("Could not get SLPC min freq\n");
			return -EIO;
		}

		if (slpc_min_freq == slpc_max_freq) {
			/* Servers will have min/max clamped to RP0 */
			if (slpc->min_is_rpmax) {
				err = slpc_set_min_freq(slpc, slpc->min_freq);
				if (err) {
					pr_err("Unable to update min freq on server part");
					return err;
				}

				saved_min_freq = slpc_min_freq;

				/* New temp min freq = RPn */
				slpc_min_freq = slpc->min_freq;
			} else {
				pr_err("Min/Max are fused to the same value");
				return -EINVAL;
			}
		}

		intel_gt_pm_wait_for_idle(gt);
		wakeref = intel_gt_pm_get(gt);
		for_each_engine(engine, gt, id) {
			struct i915_request *rq;
			u32 step, min_freq, req_freq;
			u32 act_freq, max_act_freq;

			if (!intel_engine_can_store_dword(engine))
				continue;

			st_engine_heartbeat_disable(engine);

			rq = igt_spinner_create_request(&spin,
							engine->kernel_context,
							MI_NOOP);
			if (IS_ERR(rq)) {
				st_engine_heartbeat_enable(engine);
				err = PTR_ERR(rq);
				break;
			}

			i915_request_add(rq);

			if (!igt_wait_for_spinner(&spin, rq)) {
				pr_err("%s: SLPC spinner did not start\n",
				       engine->name);
				igt_spinner_end(&spin);
				st_engine_heartbeat_enable(engine);
				intel_gt_set_wedged(engine->gt);
				err = -EIO;
				break;
			}

			/* Go from min to max in 5 steps */
			step = (slpc_max_freq - slpc_min_freq) / NUM_STEPS;
			max_act_freq = slpc_min_freq;
			for (min_freq = slpc_min_freq; min_freq < slpc_max_freq;
						min_freq += step) {
				err = slpc_set_min_freq(slpc, min_freq);
				if (err)
					break;

				/* Wait for GuC to detect business and raise
				* requested frequency if necessary.
				*/
				delay_for_h2g();

				req_freq = intel_rps_read_punit_req_frequency(rps);

				/* GuC requests freq in multiples of 50/3 MHz */
				if (req_freq < (min_freq - FREQUENCY_REQ_UNIT)) {
					pr_err("SWReq is %d, should be at least %d\n", req_freq,
					       min_freq - FREQUENCY_REQ_UNIT);
					err = -EINVAL;
				}

				act_freq =  intel_rps_read_actual_frequency(rps);
				if (act_freq > max_act_freq)
					max_act_freq = act_freq;

				if (err)
					break;
			}

			pr_info("Max actual frequency for %s was %d\n",
				engine->name, max_act_freq);

			/* Actual frequency should rise above min */
			if (max_act_freq == slpc_min_freq) {
				u32 throttle_reasons = intel_uncore_read(gt->uncore, GT0_PERF_LIMIT_REASONS);
				guc_dbg(slpc_to_guc(slpc), "Perf Limit Reasons: 0x%x", throttle_reasons);
				if (!(throttle_reasons & GT0_PERF_LIMIT_REASONS_MASK)) {
					err = -EINVAL;
					pr_err("Actual freq did not rise above min!");
				}
			}

			igt_spinner_end(&spin);
			st_engine_heartbeat_enable(engine);

			if (err)
				break;
		}

		/* Restore min/max frequencies */
		slpc_set_max_freq(slpc, slpc_max_freq);
		slpc_set_min_freq(slpc, slpc_min_freq);

		if (igt_flush_test(gt->i915))
			err = -EIO;

		intel_gt_pm_put(gt, wakeref);
		igt_spinner_fini(&spin);
		intel_gt_pm_wait_for_idle(gt);

		/* Don't continue with next tile if err is set */
		if (err)
			break;

		if (slpc->min_is_rpmax)
			slpc_set_min_freq(slpc, saved_min_freq);
	}

	return err;
}

static int live_slpc_clamp_max(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_gt *gt;
	struct intel_guc_slpc *slpc;
	struct intel_rps *rps;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	intel_wakeref_t wakeref;
	int err = 0;
	u32 slpc_min_freq, slpc_max_freq, saved_min_freq;
	unsigned int i;

	for_each_gt(gt, i915, i) {

		pr_info("Running on GT: %d", gt->info.id);

		slpc = &gt->uc.guc.slpc;
		rps = &gt->rps;

		if (!intel_uc_uses_guc_slpc(&gt->uc))
			return 0;

		if (igt_spinner_init(&spin, gt))
			return -ENOMEM;

		if (intel_guc_slpc_get_max_freq(slpc, &slpc_max_freq)) {
			pr_err("Could not get SLPC max freq\n");
			return -EIO;
		}

		if (intel_guc_slpc_get_min_freq(slpc, &slpc_min_freq)) {
			pr_err("Could not get SLPC min freq\n");
			return -EIO;
		}

		if (slpc_min_freq == slpc_max_freq) {
			/* Servers will have min/max clamped to RP0 */
			if (slpc->min_is_rpmax) {
				err = slpc_set_min_freq(slpc, slpc->min_freq);
				if (err) {
					pr_err("Unable to update min freq on server part");
					return err;
				}

				saved_min_freq = slpc_min_freq;

				/* New temp min freq = RPn */
				slpc_min_freq = slpc->min_freq;
			} else {
				pr_err("Min/Max are fused to the same value");
				return -EINVAL;
			}
		}

		intel_gt_pm_wait_for_idle(gt);
		wakeref = intel_gt_pm_get(gt);
		for_each_engine(engine, gt, id) {
			struct i915_request *rq;
			u32 max_freq, req_freq;
			u32 act_freq, max_act_freq;
			u32 step;

			if (!intel_engine_can_store_dword(engine))
				continue;

			st_engine_heartbeat_disable(engine);

			rq = igt_spinner_create_request(&spin,
							engine->kernel_context,
							MI_NOOP);
			if (IS_ERR(rq)) {
				st_engine_heartbeat_enable(engine);
				err = PTR_ERR(rq);
				break;
			}

			i915_request_add(rq);

			if (!igt_wait_for_spinner(&spin, rq)) {
				pr_err("%s: SLPC spinner did not start\n",
				       engine->name);
				igt_spinner_end(&spin);
				st_engine_heartbeat_enable(engine);
				intel_gt_set_wedged(engine->gt);
				err = -EIO;
				break;
			}

			/* Go from max to min in 5 steps */
			step = (slpc_max_freq - slpc_min_freq) / NUM_STEPS;
			max_act_freq = slpc_min_freq;
			for (max_freq = slpc_max_freq; max_freq > slpc_min_freq;
						max_freq -= step) {
				err = slpc_set_max_freq(slpc, max_freq);
				if (err)
					break;

				/* Wait for GuC to detect business and raise
				 * requested frequency if necessary.
				*/
				delay_for_h2g();

				/* Verify that SWREQ indeed was set to specific value */
				req_freq = intel_rps_read_punit_req_frequency(rps);

				/* GuC requests freq in multiples of 50/3 MHz */
				if (req_freq > (max_freq + FREQUENCY_REQ_UNIT)) {
					pr_err("SWReq is %d, should be at most %d\n", req_freq,
					       max_freq + FREQUENCY_REQ_UNIT);
					err = -EINVAL;
				}

				act_freq =  intel_rps_read_actual_frequency(rps);
				if (act_freq > max_act_freq)
					max_act_freq = act_freq;

				if (err)
					break;
			}

			pr_info("Max actual frequency for %s was %d\n",
				engine->name, max_act_freq);

			/* Actual frequency should rise above min */
			if (max_act_freq == slpc_min_freq) {
				u32 throttle_reasons = intel_uncore_read(gt->uncore, GT0_PERF_LIMIT_REASONS);
				guc_dbg(slpc_to_guc(slpc), "Perf Limit Reasons: 0x%x", throttle_reasons);
				if (!(throttle_reasons & GT0_PERF_LIMIT_REASONS_MASK)) {
					err = -EINVAL;
					pr_err("Actual freq did not rise above min!");
				}
			}

			igt_spinner_end(&spin);
			st_engine_heartbeat_enable(engine);

			if (igt_flush_test(gt->i915)) {
				err = -EIO;
				break;
			}

			if (err)
				break;
		}

		/* Restore min/max freq */
		slpc_set_max_freq(slpc, slpc_max_freq);
		slpc_set_min_freq(slpc, slpc_min_freq);

		intel_gt_pm_put(gt, wakeref);
		igt_spinner_fini(&spin);
		intel_gt_pm_wait_for_idle(gt);

		/* Don't continue with next tile if err is set */
		if (err)
			break;

		if (slpc->min_is_rpmax)
			slpc_set_min_freq(slpc, saved_min_freq);
	}

	return err;
}

int intel_slpc_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_slpc_clamp_max),
		SUBTEST(live_slpc_clamp_min),
	};

	struct intel_gt *gt;
	unsigned int i;

	for_each_gt(gt, i915, i) {
		if (intel_gt_is_wedged(to_gt(i915)))
			return 0;
	}

	return i915_live_subtests(tests, i915);
}
