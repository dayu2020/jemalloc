#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/bit_util.h"

void
cache_bin_info_init(cache_bin_info_t *info,
    cache_bin_sz_t ncached_max) {
	size_t stack_size = (size_t)ncached_max * sizeof(void *);
	assert(stack_size < ((size_t)1 << (sizeof(cache_bin_sz_t) * 8)));
	info->ncached_max = (cache_bin_sz_t)ncached_max;
}

void
cache_bin_info_compute_alloc(cache_bin_info_t *infos, szind_t ninfos,
    size_t *size, size_t *alignment) {
	/* For the total bin stack region (per tcache), reserve 2 more slots so
	 * that
	 * 1) the empty position can be safely read on the fast path before
	 *    checking "is_empty"; and
	 * 2) the cur_ptr can go beyond the empty position by 1 step safely on
	 * the fast path (i.e. no overflow).
	 */
	*size = sizeof(void *) * 2;
	for (szind_t i = 0; i < ninfos; i++) {
		*size += infos[i].ncached_max * sizeof(void *);
	}

	/*
	 * Align to at least PAGE, to minimize the # of TLBs needed by the
	 * smaller sizes; also helps if the larger sizes don't get used at all.
	 */
	*alignment = PAGE;
}

void
cache_bin_preincrement(cache_bin_info_t *infos, szind_t ninfos, void *alloc,
    size_t *cur_offset) {
	if (config_debug) {
		size_t computed_size;
		size_t computed_alignment;

		/* Pointer should be as aligned as we asked for. */
		cache_bin_info_compute_alloc(infos, ninfos, &computed_size,
		    &computed_alignment);
		assert(((uintptr_t)alloc & (computed_alignment - 1)) == 0);
	}
	/*
	 * Leave a noticeable mark pattern on the boundaries, in case a bug
	 * starts leaking those.  Make it look like the junk pattern but be
	 * distinct from it.
	 */
	uintptr_t preceding_ptr_junk = (uintptr_t)0x7a7a7a7a7a7a7a7aULL;
	*(uintptr_t *)((uintptr_t)alloc + *cur_offset) = preceding_ptr_junk;
	*cur_offset += sizeof(void *);
}

void
cache_bin_postincrement(cache_bin_info_t *infos, szind_t ninfos, void *alloc,
    size_t *cur_offset) {
	/* Note: a7 vs. 7a above -- this tells you which pointer leaked. */
	uintptr_t trailing_ptr_junk = (uintptr_t)0xa7a7a7a7a7a7a7a7ULL;
	*(uintptr_t *)((uintptr_t)alloc + *cur_offset) = trailing_ptr_junk;
	*cur_offset += sizeof(void *);
}


void
cache_bin_init(cache_bin_t *bin, cache_bin_info_t *info, void *alloc,
    size_t *cur_offset) {
	/*
	 * The full_position points to the lowest available space.  Allocations
	 * will access the slots toward higher addresses (for the benefit of
	 * adjacent prefetch).
	 */
	void *stack_cur = (void *)((uintptr_t)alloc + *cur_offset);
	void *full_position = stack_cur;
	uint16_t bin_stack_size = info->ncached_max * sizeof(void *);

	*cur_offset += bin_stack_size;
	void *empty_position = (void *)((uintptr_t)alloc + *cur_offset);

	/* Init to the empty position. */
	bin->stack_head = (void **)empty_position;
	bin->low_bits_low_water = (uint16_t)(uintptr_t)bin->stack_head;
	bin->low_bits_full = (uint16_t)(uintptr_t)full_position;
	bin->low_bits_empty = (uint16_t)(uintptr_t)empty_position;
	assert(cache_bin_diff(bin, bin->low_bits_full,
	    (uint16_t)(uintptr_t) bin->stack_head) == bin_stack_size);
	assert(cache_bin_ncached_get(bin, info) == 0);
	assert(cache_bin_empty_position_get(bin, info) == empty_position);
}

bool
cache_bin_still_zero_initialized(cache_bin_t *bin) {
	return bin->stack_head == NULL;
}
