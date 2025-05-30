/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmscan

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_VMSCAN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_VMSCAN_H

#include <trace/hooks/vendor_hooks.h>

struct shrink_control;
struct shrinker;

DECLARE_RESTRICTED_HOOK(android_rvh_set_balance_anon_file_reclaim,
			TP_PROTO(bool *balance_anon_file_reclaim),
			TP_ARGS(balance_anon_file_reclaim), 1);
DECLARE_HOOK(android_vh_kswapd_per_node,
	TP_PROTO(int nid, bool *skip, bool run),
	TP_ARGS(nid, skip, run));
DECLARE_HOOK(android_vh_page_referenced_check_bypass,
	TP_PROTO(struct page *page, unsigned long nr_to_scan, int lru, bool *bypass),
	TP_ARGS(page, nr_to_scan, lru, bypass));
DECLARE_HOOK(android_vh_page_trylock_get_result,
	TP_PROTO(struct page *page, bool *trylock_fail),
	TP_ARGS(page, trylock_fail));
DECLARE_HOOK(android_vh_handle_failed_page_trylock,
	TP_PROTO(struct list_head *page_list),
	TP_ARGS(page_list));
DECLARE_HOOK(android_vh_page_trylock_set,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_HOOK(android_vh_page_trylock_clear,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_HOOK(android_vh_shrink_node_memcgs,
	TP_PROTO(struct mem_cgroup *memcg, bool *skip),
	TP_ARGS(memcg, skip));
DECLARE_HOOK(android_vh_tune_scan_type,
	TP_PROTO(char *scan_type),
	TP_ARGS(scan_type));
DECLARE_HOOK(android_vh_tune_swappiness,
	TP_PROTO(int *swappiness),
	TP_ARGS(swappiness));
DECLARE_HOOK(android_vh_shrink_slab_bypass,
	TP_PROTO(gfp_t gfp_mask, int nid, struct mem_cgroup *memcg, int priority, bool *bypass),
	TP_ARGS(gfp_mask, nid, memcg, priority, bypass));
DECLARE_HOOK(android_vh_do_shrink_slab,
	TP_PROTO(struct shrinker *shrinker, struct shrink_control *shrinkctl, int priority),
	TP_ARGS(shrinker, shrinkctl, priority));
DECLARE_HOOK(android_vh_tune_memcg_scan_type,
	TP_PROTO(struct mem_cgroup *memcg, char *scan_type),
	TP_ARGS(memcg, scan_type));
DECLARE_HOOK(android_vh_tune_inactive_ratio,
	TP_PROTO(unsigned long *inactive_ratio, int file),
	TP_ARGS(inactive_ratio, file))
DECLARE_HOOK(android_vh_check_page_look_around_ref,
	TP_PROTO(struct page *page, int *skip),
	TP_ARGS(page, skip));
DECLARE_HOOK(android_vh_vmscan_kswapd_done,
	TP_PROTO(int node_id, unsigned int highest_zoneidx, unsigned int alloc_order,
		unsigned int reclaim_order),
	TP_ARGS(node_id, highest_zoneidx, alloc_order, reclaim_order));
#endif /* _TRACE_HOOK_VMSCAN_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
