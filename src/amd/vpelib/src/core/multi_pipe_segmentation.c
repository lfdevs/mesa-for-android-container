/* Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "vpe_assert.h"
#include "vpe_priv.h"
#include "common.h"
#include "multi_pipe_segmentation.h"
#include "vpe20_resource.h"

#define MPS_INITIAL_SECTION_SIZE 2

/* ===== START HELPER FUNCTIONS =============================================================== */

static inline int32_t get_section_width(const struct vpe_mps_section *section)
{
    return section->end_x - section->start_x;
}

static bool is_value_in_range(int32_t value, int32_t start, int32_t end)
{
    return (value >= start) && (value <= end);
}

// Return amount of rect that exists horizontally between two x coordinates
static uint32_t get_rect_width_in_range(
    const struct vpe_rect *rect, int32_t left_x, int32_t right_x)
{
    if (rect->x <= left_x && (rect->x + (int32_t)rect->width) >= right_x)
        return right_x - left_x;

    bool     left_edge_inside  = is_value_in_range(rect->x, left_x, right_x);
    bool     right_edge_inside = is_value_in_range(rect->x + (int32_t)rect->width, left_x, right_x);
    uint32_t stride;

    if (left_edge_inside && right_edge_inside)
        stride = rect->width;
    else if (left_edge_inside)
        stride = right_x - rect->x;
    else if (right_edge_inside)
        stride = (rect->x + rect->width) - left_x;
    else
        stride = 0;

    return stride;
}

// Limit total segment width to keep dst rect within its max width
// (which will be limited by keeping src rect within max seg width)
static uint32_t change_right_seg_edge_to_limit_dst_rect_width(
    const struct vpe_rect *rect, int32_t seg_left_x, int32_t seg_right_x, uint32_t max_dst_stride)
{
    bool left_edge_inside = is_value_in_range(rect->x, seg_left_x, seg_right_x);

    uint32_t stride          = get_rect_width_in_range(rect, seg_left_x, seg_right_x);
    int32_t  new_seg_right_x = seg_right_x;

    // NOTE: the case where rect isn't within this segment is handled by get_rect_width_in_range()
    if (stride > max_dst_stride) {
        if (left_edge_inside) {
            new_seg_right_x = rect->x + max_dst_stride;
        } else {
            new_seg_right_x = seg_left_x + max_dst_stride;
        }
        VPE_ASSERT(new_seg_right_x < seg_right_x); // Sanity check that we removed from total width
    }
    return new_seg_right_x;
}

static uint16_t get_num_rects_at_x(
    int32_t x, const struct vpe_rect *dst_rects, uint16_t num_input_streams)
{
    uint16_t num_rects = 0;

    for (int i = 0; i < num_input_streams; i++)
        if ((x >= dst_rects[i].x) && (x < (int32_t)(dst_rects[i].x + dst_rects[i].width)))
            num_rects++;

    return num_rects;
}

// return total number of rects that exist within the same recout_align block as this x coordinate
// ex. if x = 21 and recout_align = 16, return all rects with any pixels in x e [16, 32)
static uint16_t get_num_rects_at_x_recout_align(
    int32_t x, const struct vpe_rect *dst_rects, uint16_t num_input_streams, uint32_t recout_align)
{
    if (recout_align == VPE_NO_ALIGNMENT)
        return get_num_rects_at_x(x, dst_rects, num_input_streams);

    int32_t lower_bound = recout_align * (x / recout_align);
    int32_t upper_bound = lower_bound + recout_align;

    uint16_t num_rects = 0;

    for (int i = 0; i < num_input_streams; i++)
        if (get_rect_width_in_range(&dst_rects[i], lower_bound, upper_bound) != 0)
            num_rects++;

    return num_rects;
}

// Helper func to determine when one section ends and the next one starts - checks number of streams
// at x and if its not equal to the number in the current section, updates next_section_start
static void check_num_rects_at_x_and_update_next_section_x(uint16_t prev_section_num_streams,
    int32_t x, const struct vpe_rect *dst_rects, uint16_t num_input_streams,
    uint32_t recout_width_alignment, int32_t *next_section_start)
{
    uint16_t next_x_num_rects;

    next_x_num_rects =
        get_num_rects_at_x_recout_align(x, dst_rects, num_input_streams, recout_width_alignment);

    if (next_x_num_rects != prev_section_num_streams)
        *next_section_start = min(*next_section_start, x);
}

// Pass in end x of the last section, and how many streams were in it. If start_x == combined_dst.x,
// calculate number of streams at start_x, then find run with that value
static uint32_t calculate_next_section_start(int32_t start_x, uint16_t prev_section_num_streams,
    const struct vpe_rect *dst_rects, const struct vpe_rect *combined_dst,
    uint16_t num_input_streams, uint32_t recout_width_alignment)
{
    int32_t next_section_start = combined_dst->x + combined_dst->width;

    // To find end of this section (i.e. next x value where we go from one/no streams to multiple,
    // or vice versa) check left and right edge of every input rect, and set next_section_start to
    // smallest x where num_streams(x) != prev_section_num_streams
    for (int i = 0; i < num_input_streams; i++) {
        if (recout_width_alignment == VPE_NO_ALIGNMENT) {
            // If unaligned, we can just check the left and right edges of each stream
            if (dst_rects[i].x > start_x)
                check_num_rects_at_x_and_update_next_section_x(prev_section_num_streams,
                    dst_rects[i].x, dst_rects, num_input_streams, recout_width_alignment,
                    &next_section_start);

            if (dst_rects[i].x + (int32_t)dst_rects[i].width > start_x)
                check_num_rects_at_x_and_update_next_section_x(prev_section_num_streams,
                    dst_rects[i].x + dst_rects[i].width, dst_rects, num_input_streams,
                    recout_width_alignment, &next_section_start);
        } else {
            // If aligned, we need to take the left and right edge of each stream, then check the
            // closest aligned x value below, at, and above that edge
            int32_t dst_x_floor =
                recout_width_alignment * (dst_rects[i].x / recout_width_alignment);
            int32_t dst_x_before = dst_x_floor - recout_width_alignment;
            int32_t dst_x_ceil   = dst_x_floor + recout_width_alignment;
            int32_t dst_width_floor =
                recout_width_alignment *
                ((dst_rects[i].x + dst_rects[i].width) / recout_width_alignment);
            int32_t dst_width_before = dst_width_floor - recout_width_alignment;
            int32_t dst_width_ceil   = dst_width_floor + recout_width_alignment;

            if (dst_x_floor > start_x)
                check_num_rects_at_x_and_update_next_section_x(prev_section_num_streams,
                    dst_x_floor, dst_rects, num_input_streams, recout_width_alignment,
                    &next_section_start);

            if (dst_x_before > start_x)
                check_num_rects_at_x_and_update_next_section_x(prev_section_num_streams,
                    dst_x_before, dst_rects, num_input_streams, recout_width_alignment,
                    &next_section_start);

            if (dst_x_ceil > start_x)
                check_num_rects_at_x_and_update_next_section_x(prev_section_num_streams, dst_x_ceil,
                    dst_rects, num_input_streams, recout_width_alignment, &next_section_start);

            if (dst_width_floor > start_x)
                check_num_rects_at_x_and_update_next_section_x(prev_section_num_streams,
                    dst_width_floor, dst_rects, num_input_streams, recout_width_alignment,
                    &next_section_start);

            if (dst_width_before > start_x)
                check_num_rects_at_x_and_update_next_section_x(prev_section_num_streams,
                    dst_width_before, dst_rects, num_input_streams, recout_width_alignment,
                    &next_section_start);

            if (dst_width_ceil > start_x)
                check_num_rects_at_x_and_update_next_section_x(prev_section_num_streams,
                    dst_width_ceil, dst_rects, num_input_streams, recout_width_alignment,
                    &next_section_start);
        }
    }

    return next_section_start;
}

static uint32_t get_minimum_seg_size(bool prev_section, const uint32_t recout_width_alignment)
{
    return prev_section ? VPE_MIN_VIEWPORT_SIZE
                        : max(VPE_MIN_VIEWPORT_SIZE, recout_width_alignment);
}

static uint32_t get_optimal_seg_size(
    struct vpe_mps_section *section, uint32_t min_seg_size, uint32_t recout_width_alignment)
{
    uint32_t optimal_seg_size = 0;
    if (section->num_streams < 2)
        optimal_seg_size = max(OPTIMAL_MIN_PERFORMACE_MODE_SIZE, min_seg_size);
    else
        optimal_seg_size = min_seg_size;

    if (recout_width_alignment != VPE_NO_ALIGNMENT)
        optimal_seg_size =
            recout_width_alignment * int_divide_with_ceil(optimal_seg_size, recout_width_alignment);

    return optimal_seg_size;
}

// for section[i], grab pixels from the sections to the right of it until it reaches the minimum seg
// size. If the section to the right is too small, merge the two together
static void get_pixels_from_right_sections(struct vpe_vector *section_vector, int i,
    uint32_t minimum_seg_size, bool *rerun_optimization, const uint32_t recout_width_alignment)
{
    struct vpe_mps_section *section      = vpe_vector_get(section_vector, i);
    struct vpe_mps_section *next_section = vpe_vector_get(section_vector, i + 1);

    if (get_section_width(section) >= (int32_t)minimum_seg_size)
        return; // already at minimum size (or larger

    // if section has more streams than next_section - next_section efficiency will decrease. take
    // as few pixels as we can
    uint32_t pixels_required       = minimum_seg_size - get_section_width(section);
    uint16_t num_sections_to_erase = 0;

    while (get_section_width(section) < (int32_t)minimum_seg_size && next_section != NULL) {
        uint32_t next_section_width = get_section_width(next_section);
        if (next_section->num_streams > section->num_streams) {
            // if next section has more streams, we need to combine the two sections
            section->num_streams = next_section->num_streams;
            section->end_x       = next_section->end_x;

            num_sections_to_erase++;
            *rerun_optimization = true;
            break;
        } else {
            // if next section is a higher-efficiency section (aka less streams) only take as many
            // pixels as required
            uint32_t minimum_next_section_size =
                pixels_required +
                get_minimum_seg_size((uint16_t)(i + 1 + num_sections_to_erase) ==
                                         (uint16_t)(section_vector->num_elements - 1),
                    recout_width_alignment);
            if (next_section_width >= minimum_next_section_size) {
                section->end_x += pixels_required;
                next_section->start_x += pixels_required;

                if (next_section_width == pixels_required) {
                    num_sections_to_erase++;
                }
                break;
            } else {
                section->end_x = next_section->end_x;
                pixels_required -= next_section_width;
                num_sections_to_erase++;
                next_section = vpe_vector_get(section_vector, i + 1 + num_sections_to_erase);
            }
        }
    }
    if (pixels_required != 0)
        *rerun_optimization = true;

    vpe_vector_erase(section_vector, i + 1, num_sections_to_erase);
}

// return number of sections removed, as caller will need to update its indexing variable
static uint16_t get_pixels_from_left_sections(struct vpe_vector *section_vector, int i,
    uint32_t minimum_seg_size, const uint32_t recout_width_alignment)
{
    struct vpe_mps_section *section      = vpe_vector_get(section_vector, i);
    struct vpe_mps_section *prev_section = vpe_vector_get(section_vector, i - 1);

    uint16_t num_sections_to_erase = 0;
    uint32_t pixels_required       = minimum_seg_size - get_section_width(section);

    while ((get_section_width(section) < (int32_t)minimum_seg_size) && (prev_section != NULL)) {
        uint32_t prev_section_width = prev_section->end_x - prev_section->start_x;
        if (prev_section->num_streams > section->num_streams) {
            // at this point just need to combine the two sections
            section->num_streams = prev_section->num_streams;
            section->start_x     = prev_section->start_x;
            num_sections_to_erase++;
            break;
        } else {
            uint32_t minimum_prev_section_size_to_give_pixels =
                pixels_required + get_minimum_seg_size(false, recout_width_alignment);
            if (prev_section_width >= minimum_prev_section_size_to_give_pixels) {
                section->start_x -= pixels_required;
                prev_section->end_x -= pixels_required;

                if (prev_section_width == pixels_required)
                    num_sections_to_erase++;
                break;
            } else {
                section->start_x = prev_section->start_x;
                pixels_required -= prev_section_width;
                num_sections_to_erase++;
                prev_section = vpe_vector_get(section_vector, i - 1 - num_sections_to_erase);
            }
        }
    }
    vpe_vector_erase(section_vector, i - num_sections_to_erase, num_sections_to_erase);
    return num_sections_to_erase;
}

static void fill_mps_cmd_packet(struct vpe_priv *vpe_priv, struct vpe_mps_ctx *mps_ctx,
    const struct vpe_rect *dst_rects, struct vpe_mps_command *command, int32_t seg_left_x,
    int32_t seg_right_x, uint16_t num_input_streams)
{
    uint8_t inputs_added = 0;

    for (uint16_t i = 0; i < num_input_streams; i++) {
        if (get_rect_width_in_range(&dst_rects[i], seg_left_x, seg_right_x) != 0) {
            command->mps_idx[command->num_inputs]    = (int16_t)i;
            command->stream_idx[command->num_inputs] = mps_ctx->stream_idx[i];
            command->start_x[command->num_inputs]    = seg_left_x;
            command->end_x[command->num_inputs]      = seg_right_x;
            command->is_bg_gen[command->num_inputs]  = false;
            command->num_inputs++;
            inputs_added++;
        }
    }
    if (inputs_added == 0) {
        command->mps_idx[command->num_inputs]    = -1;
        command->stream_idx[command->num_inputs] = 0;
        command->start_x[command->num_inputs]    = seg_left_x;
        command->end_x[command->num_inputs]      = seg_right_x;
        command->is_bg_gen[command->num_inputs]  = true;
        command->num_inputs++;
    }
}

static uint16_t get_num_streams_in_segment(const struct vpe_rect *dst_rects,
    uint16_t num_input_streams, int32_t seg_left_x, int32_t seg_right_x)
{
    uint16_t num_streams_in_segment = 0;
    for (int i = 0; i < num_input_streams; i++) {
        if (get_rect_width_in_range(&dst_rects[i], seg_left_x, seg_right_x) != 0) {
            num_streams_in_segment++;
        }
    }
    return num_streams_in_segment;
}

static enum vpe_status align_seg_right_x(
    int32_t seg_left_x, int32_t *seg_right_x, uint32_t recout_width_alignment)
{
    enum vpe_status status = VPE_STATUS_OK;

    if (recout_width_alignment != VPE_NO_ALIGNMENT) {
        // use int division to floor seg_width to a multiple of required alignment width
        uint32_t seg_width     = *seg_right_x - seg_left_x;
        uint32_t new_seg_width = recout_width_alignment * (seg_width / recout_width_alignment);
        if (new_seg_width != seg_width) {
            status = VPE_STATUS_REPEAT_ITEM; // If we need to adjust seg, re-check that all
                                             // contraints are met again
            *seg_right_x = seg_left_x + new_seg_width;
        }
    }

    return status;
}

static enum vpe_status enforce_minimum_viewport_size_for_rect_in_section(
    const struct vpe_rect *dst_rects, double *scaling_ratios, uint32_t recout_width_alignment,
    struct vpe_vector *section_vector, uint16_t num_inputs)
{
    uint16_t        i, section_idx;
    int32_t         dst_in_range;
    int32_t         src_in_range;
    enum vpe_status status = VPE_STATUS_REPEAT_ITEM;
    bool            is_last_section;

    while (status == VPE_STATUS_REPEAT_ITEM) {
        status = VPE_STATUS_OK;
        for (section_idx = 0; section_idx < section_vector->num_elements; section_idx++) {
            struct vpe_mps_section *section      = vpe_vector_get(section_vector, section_idx);
            struct vpe_mps_section *next_section = vpe_vector_get(section_vector, section_idx + 1);

            is_last_section = ((int16_t)section_idx == (int16_t)(section_vector->num_elements - 1));

            for (i = 0; i < num_inputs; i++) {
                dst_in_range =
                    get_rect_width_in_range(&dst_rects[i], section->start_x, section->end_x);

                if (dst_in_range == 0)
                    continue;

                src_in_range = (int32_t)((double)dst_in_range / scaling_ratios[i]);

                if (is_last_section) {
                    // Remove all of this stream from this segment if either src or dst rect too
                    // small
                    if ((dst_in_range < VPE_MIN_VIEWPORT_SIZE) ||
                        (src_in_range < VPE_MIN_VIEWPORT_SIZE)) {
                        VPE_ASSERT(false);
                        status = VPE_STATUS_ERROR; // Error: we haven't left enough source rect
                        break;
                    }
                } else {
                    bool left_edge_inside =
                        is_value_in_range(dst_rects[i].x, section->start_x, section->end_x);
                    bool right_edge_inside =
                        is_value_in_range(dst_rects[i].x + (int32_t)dst_rects[i].width,
                            section->start_x, section->end_x);

                    // Remove all of this stream from this segment if either src or dst rect too
                    // small
                    if ((dst_in_range < VPE_MIN_VIEWPORT_SIZE) ||
                        (src_in_range < VPE_MIN_VIEWPORT_SIZE)) {
                        status = VPE_STATUS_REPEAT_ITEM;

                        if (right_edge_inside) {
                            continue; // rects that originate from the last section should be dealt
                                      // with by that section
                        }

                        if (!right_edge_inside) {
                            if (next_section->num_streams > section->num_streams) {
                                int32_t pixels_to_give = dst_in_range;
                                if (recout_width_alignment != VPE_NO_ALIGNMENT) {
                                    pixels_to_give =
                                        recout_width_alignment *
                                        int_divide_with_ceil(dst_in_range, recout_width_alignment);
                                    VPE_ASSERT(get_section_width(section) >= pixels_to_give);
                                }

                                if (pixels_to_give == get_section_width(section)) {
                                    next_section->start_x = section->start_x;
                                    vpe_vector_erase(section_vector, section_idx, 1);
                                    section_idx--;
                                } else {
                                    section->end_x -= pixels_to_give;
                                    next_section->start_x -= pixels_to_give;
                                }
                            } else { // this section has more streams than next one
                                int32_t pixels_required = max(VPE_MIN_VIEWPORT_SIZE - dst_in_range,
                                    (int32_t)(scaling_ratios[i] *
                                              (double)(VPE_MIN_VIEWPORT_SIZE - src_in_range)));
                                bool    rerun_optimization = false;
                                if (recout_width_alignment != VPE_NO_ALIGNMENT)
                                    pixels_required = recout_width_alignment *
                                                      int_divide_with_ceil(
                                                          pixels_required, recout_width_alignment);

                                get_pixels_from_right_sections(section_vector, section_idx,
                                    get_section_width(section) + pixels_required,
                                    &rerun_optimization, recout_width_alignment);
                            }
                        }
                    }

                    if (status != VPE_STATUS_ERROR && !right_edge_inside) {
                        // Now check how much remains for the next segment
                        dst_in_range = dst_rects[i].width + dst_rects[i].x - next_section->start_x;
                        src_in_range = (int32_t)((double)dst_in_range / scaling_ratios[i]);

                        if ((dst_in_range < VPE_MIN_VIEWPORT_SIZE) ||
                            (src_in_range < VPE_MIN_VIEWPORT_SIZE)) {
                            if (next_section->num_streams > section->num_streams) {
                                int32_t pixels_to_give = max(VPE_MIN_VIEWPORT_SIZE - dst_in_range,
                                    (int32_t)(scaling_ratios[i] *
                                              (double)(VPE_MIN_VIEWPORT_SIZE - src_in_range)));
                                if (recout_width_alignment != VPE_NO_ALIGNMENT) {
                                    pixels_to_give =
                                        recout_width_alignment *
                                        int_divide_with_ceil(dst_in_range, recout_width_alignment);
                                    VPE_ASSERT(get_section_width(section) >= pixels_to_give);
                                }

                                if (pixels_to_give >= get_section_width(section)) {
                                    next_section->start_x = section->start_x;
                                    vpe_vector_erase(section_vector, section_idx, 1);
                                    section_idx--;
                                } else {
                                    section->end_x -= pixels_to_give;
                                    next_section->start_x -= pixels_to_give;
                                }
                            } else { // this section has more streams than next one
                                uint32_t pixels_required    = dst_in_range;
                                bool     rerun_optimization = false;
                                if (recout_width_alignment != VPE_NO_ALIGNMENT)
                                    pixels_required = recout_width_alignment *
                                                      int_divide_with_ceil(
                                                          pixels_required, recout_width_alignment);

                                get_pixels_from_right_sections(section_vector, section_idx,
                                    get_section_width(section) + pixels_required,
                                    &rerun_optimization, recout_width_alignment);
                            }
                        }
                    }
                }
            }
        }
    }

    return status;
}

static enum vpe_status enforce_minimum_viewport_size_for_rect_in_seg(
    const struct vpe_rect *dst_rects, double *scaling_ratios, uint16_t i, int32_t seg_left_x,
    int32_t *seg_right_x, uint32_t recout_width_alignment, bool is_last_seg)
{
    uint32_t        dst_in_range = get_rect_width_in_range(&dst_rects[i], seg_left_x, *seg_right_x);
    enum vpe_status status       = VPE_STATUS_OK;
    uint32_t        src_in_range;

    if (dst_in_range == 0)
        return status;

    src_in_range = (uint32_t)((double)dst_in_range / scaling_ratios[i]);

    if (is_last_seg) {
        // Remove all of this stream from this segment if either src or dst rect too small
        if (dst_in_range < VPE_MIN_VIEWPORT_SIZE || src_in_range < VPE_MIN_VIEWPORT_SIZE) {
            VPE_ASSERT(false);
            status = VPE_STATUS_ERROR; // Error: we haven't left enough source rect
        }
    } else {
        bool left_edge_inside  = is_value_in_range(dst_rects[i].x, seg_left_x, *seg_right_x);
        bool right_edge_inside = is_value_in_range(
            dst_rects[i].x + (int32_t)dst_rects[i].width, seg_left_x, *seg_right_x);

        // Remove all of this stream from this segment if either src or dst rect too small
        if (dst_in_range < VPE_MIN_VIEWPORT_SIZE || src_in_range < VPE_MIN_VIEWPORT_SIZE) {
            if (!left_edge_inside) {
                VPE_ASSERT(false);
                status = VPE_STATUS_ERROR; // If left edge not inside, we won't be able to remove
            }
            if (!right_edge_inside) {
                if (recout_width_alignment == VPE_NO_ALIGNMENT) {
                    *seg_right_x -= dst_in_range;
                } else {
                    *seg_right_x -= recout_width_alignment *
                                    int_divide_with_ceil(dst_in_range, recout_width_alignment);
                }
            } else {
                if (recout_width_alignment == VPE_NO_ALIGNMENT) {
                    *seg_right_x = dst_rects[i].x;
                } else {
                    *seg_right_x =
                        dst_rects[i].x * (uint32_t)(dst_rects[i].x / recout_width_alignment);
                }
            }
            if (status != VPE_STATUS_ERROR)
                status = VPE_STATUS_REPEAT_ITEM; // If we need to adjust seg, re-check that all
                                                 // contraints are met again
        }

        if (status != VPE_STATUS_ERROR && !right_edge_inside) {
            // Now check how much remains for the next segment
            dst_in_range = dst_rects[i].width + dst_rects[i].x - *seg_right_x;
            src_in_range = (uint32_t)((double)dst_in_range / scaling_ratios[i]);

            if (dst_in_range < VPE_MIN_VIEWPORT_SIZE) {
                *seg_right_x -= max(recout_width_alignment, (VPE_MIN_VIEWPORT_SIZE - dst_in_range));
                status = VPE_STATUS_REPEAT_ITEM; // If we need to adjust seg, re-check that all
                                                 // contraints are met again
            } else if (src_in_range < VPE_MIN_VIEWPORT_SIZE) {
                *seg_right_x -= max(recout_width_alignment,
                    (uint32_t)((double)(VPE_MIN_VIEWPORT_SIZE - src_in_range) * scaling_ratios[i]));
                status = VPE_STATUS_REPEAT_ITEM; // If we need to adjust seg, re-check that all
                                                 // contraints are met again
            }
        }
    }

    return status;
}

static enum vpe_status limit_stream_count_in_seg_to_section_num_streams(
    struct vpe_mps_section *section, const struct vpe_rect *dst_rects, int32_t seg_left_x,
    int32_t *seg_right_x, uint16_t num_input_streams)
{
    int16_t         stream_to_remove      = -1;
    int32_t         remove_stream_start_x = 0;
    enum vpe_status status                = VPE_STATUS_OK;

    while (get_num_streams_in_segment(dst_rects, num_input_streams, seg_left_x, *seg_right_x) >
           section->num_streams) {
        status = VPE_STATUS_REPEAT_ITEM;
        for (int16_t i = 0; i < num_input_streams; i++) {
            if (get_rect_width_in_range(&dst_rects[i], seg_left_x, *seg_right_x) != 0) {
                if (stream_to_remove == -1) {
                    stream_to_remove      = i;
                    remove_stream_start_x = dst_rects[i].x;
                } else if (dst_rects[i].x > remove_stream_start_x) {
                    stream_to_remove      = i;
                    remove_stream_start_x = dst_rects[i].x;
                }
            }
        }

        if (stream_to_remove == -1 || dst_rects[stream_to_remove].x <= seg_left_x ||
            dst_rects[stream_to_remove].x >= *seg_right_x) {
            VPE_ASSERT(false);
            status = VPE_STATUS_ERROR;
            break;
        }
        *seg_right_x = dst_rects[stream_to_remove].x;
    }

    return status;
}

// See if current segment can be added to the last command, or if a new command is required
static bool mps_is_new_command_required(struct vpe_priv *vpe_priv, struct vpe_mps_ctx *mps_ctx,
    struct vpe_mps_section *section, const struct vpe_rect *dst_rects, int32_t seg_left_x,
    int32_t seg_right_x, uint16_t num_input_streams)
{
    // FROD limits the number of backends and blending requires all frontends to be in sync
    bool create_new_command =
        ((section->num_streams > 1) || (section->command_vector->num_elements == 0) ||
            vpe_priv->output_ctx.frod_param.enable_frod);
    if (!create_new_command) {
        // Check if this segment can be added to the last command
        struct vpe_mps_command *last_command =
            vpe_vector_get(section->command_vector, section->command_vector->num_elements - 1);
        uint16_t lut3d_count = 0;

        if (last_command->num_inputs >= vpe_priv->pub.caps->resource_caps.num_dpp)
            create_new_command = true;

        for (int i = 0; i < num_input_streams; i++)
            if (get_rect_width_in_range(&dst_rects[i], seg_left_x, seg_right_x) != 0) {
                if ((vpe_priv->stream_ctx[mps_ctx->stream_idx[i]].stream.tm_params.enable_3dlut) ||
                    (vpe_priv->stream_ctx[mps_ctx->stream_idx[i]].stream.tm_params.UID != 0)) {
                    lut3d_count++;
                }
            }

        for (int i = 0; i < last_command->num_inputs; i++) {
            if (last_command->is_bg_gen[i] == false) {
                if ((vpe_priv->stream_ctx[last_command->stream_idx[i]]
                            .stream.tm_params.enable_3dlut) ||
                    (vpe_priv->stream_ctx[last_command->stream_idx[i]].stream.tm_params.UID != 0)) {
                    lut3d_count++;
                }
            }
        }

        if (lut3d_count > vpe_priv->pub.caps->resource_caps.num_mpc_3dlut) {
            create_new_command = true;
        }
    }

    return create_new_command;
}

static enum vpe_status mps_segmentation_algo(struct vpe_priv *vpe_priv, struct vpe_mps_ctx *mps_ctx,
    struct vpe_mps_input *input_params, uint32_t *max_dst_width_per_stream, double *scaling_ratios,
    struct vpe_mps_section *section)
{
    struct vpe_rect dst_rects[MAX_INPUT_PIPE];

    for (int i = 0; i < mps_ctx->num_streams; i++)
        dst_rects[i] = input_params->mps_stream_ctx[i].dst_rect;

    // command_vector should be null because we need to clear the section_vector before we start
    if (section->command_vector == NULL)
        section->command_vector = vpe_vector_create(vpe_priv, sizeof(struct vpe_mps_command), 2);

    int32_t seg_left_x = section->start_x;
    int32_t seg_right_x;

    while (seg_left_x < section->end_x) {
        // Start by setting seg to maximum width
        seg_right_x = min(seg_left_x + (int32_t)input_params->max_seg_width, section->end_x);

        // Limit total seg width to keep src rects within max seg width - can happen w/ downscaling
        for (int i = 0; i < input_params->num_inputs; i++) {
            seg_right_x = change_right_seg_edge_to_limit_dst_rect_width(
                &dst_rects[i], seg_left_x, seg_right_x, max_dst_width_per_stream[i]);
        }

        // Determine if alignment is required for segment width and adjust
        bool adjustment_required;
        do {
            adjustment_required = false;

            for (uint16_t i = 0; i < input_params->num_inputs; i++) {
                bool is_last_seg = (seg_right_x == section->end_x);
                if (!is_last_seg)
                    if (align_seg_right_x(seg_left_x, &seg_right_x,
                            input_params->recout_width_alignment) == VPE_STATUS_REPEAT_ITEM)
                        adjustment_required = true;

                // We need to ensure that there's enough dst/src rect in the current segment and
                // enough left for the next one If not, cut off some more from this segment to
                // remove some from this one/add some to the next one
                if (enforce_minimum_viewport_size_for_rect_in_seg(dst_rects, scaling_ratios, i,
                        seg_left_x, &seg_right_x, input_params->recout_width_alignment,
                        is_last_seg) == VPE_STATUS_REPEAT_ITEM)
                    adjustment_required = true;
            }

            // Need to make sure each segment has as many stream as this section allows to correctly
            // build command ex. if we have a section with 2 stream that don't overlap, then its a 1
            // stream section (i.e. performance mode section)
            //     in this case we need to limit this section to only contain 1 stream
            if (limit_stream_count_in_seg_to_section_num_streams(section, dst_rects, seg_left_x,
                    &seg_right_x, input_params->num_inputs) == VPE_STATUS_REPEAT_ITEM)
                adjustment_required = true;
        } while (adjustment_required);

        // When blending we need every segment to be a new command
        // When not blending, we can combine segments into one command (perf mode style) if no HW
        // limitations
        bool create_new_command = mps_is_new_command_required(vpe_priv, mps_ctx, section, dst_rects,
            seg_left_x, seg_right_x, input_params->num_inputs);

        if (create_new_command) {
            struct vpe_mps_command new_command = {0};
            fill_mps_cmd_packet(vpe_priv, mps_ctx, dst_rects, &new_command, seg_left_x, seg_right_x,
                input_params->num_inputs);
            vpe_vector_push(section->command_vector, &new_command);
        } else {
            struct vpe_mps_command *last_command =
                vpe_vector_get(section->command_vector, section->command_vector->num_elements - 1);
            fill_mps_cmd_packet(vpe_priv, mps_ctx, dst_rects, last_command, seg_left_x, seg_right_x,
                input_params->num_inputs);
        }

        // Update how many segments each stream has.
        // Segment width is calculated later after some further optimizations
        for (int i = 0; i < input_params->num_inputs; i++) {
            uint32_t width_in_seg = get_rect_width_in_range(&dst_rects[i], seg_left_x, seg_right_x);
            if (width_in_seg != 0) {
                mps_ctx->segment_count[i]++;
            }
        }
        seg_left_x = seg_right_x;
    }
    return VPE_STATUS_OK;
}

static enum vpe_status mps_create_initial_sections(const struct vpe_rect *dst_rects,
    const struct vpe_rect *combined_dst, uint16_t num_input_streams,
    uint32_t recout_width_alignment, struct vpe_vector *section_vector)
{
    // Move left to right across combined dst rect, split into areas of zero/single segs or multi
    // segs, then call correct seg algo on them
    int32_t section_start_x = combined_dst->x;
    int32_t section_end_x;
    int16_t num_stream_in_current_section;

    while (section_start_x < combined_dst->x + (int32_t)combined_dst->width) {
        num_stream_in_current_section = get_num_rects_at_x_recout_align(
            section_start_x, dst_rects, num_input_streams, recout_width_alignment);
        section_end_x = calculate_next_section_start(section_start_x, num_stream_in_current_section,
            dst_rects, combined_dst, num_input_streams, recout_width_alignment);

        if (section_end_x - section_start_x <= 0) { // Sanity check
            VPE_ASSERT(false);
            return VPE_STATUS_ERROR;
        }

        struct vpe_mps_section new_section = {0};
        new_section.start_x                = section_start_x;
        new_section.end_x                  = section_end_x;
        new_section.num_streams            = num_stream_in_current_section;

        if (section_vector->capacity > section_vector->num_elements) {
            struct vpe_mps_section *preexisting_section =
                vpe_vector_get(section_vector, section_vector->num_elements);
            if (preexisting_section != NULL)
                if (preexisting_section->command_vector != NULL)
                    new_section.command_vector = preexisting_section->command_vector;
        }

        vpe_vector_push(section_vector, &new_section);

        section_start_x = section_end_x;
    }

    return VPE_STATUS_OK;
}

static enum vpe_status mps_check_empty(struct vpe_vector *section_vector)
{
    for (int i = 0; i < (int)(section_vector->num_elements - 1); i++) {
        struct vpe_mps_section *section = vpe_vector_get(section_vector, i);

        if (section->start_x >= section->end_x) {
            // vpe_vector_erase(section_vector, i, 1);
            // i--;
            VPE_ASSERT(false); // This shouldn't happen, means something went wrong earlier
            return VPE_STATUS_ERROR;
        }
    }

    return VPE_STATUS_OK;
}

// The point of this function is essentially to combine sections that are handled the same
// i.e. combine background only sections and 1 stream sections, as both are segmented the same way
// (performance mode style)
static enum vpe_status mps_combine_single_zero_stream_sections(struct vpe_vector *section_vector)
{
    bool rerun_optimization = false;

    for (int i = 0; i < (int)(section_vector->num_elements - 1); i++) {
        struct vpe_mps_section *section      = vpe_vector_get(section_vector, i);
        struct vpe_mps_section *next_section = vpe_vector_get(section_vector, i + 1);

        // < 2 to combine 1 stream and 0 stream sections
        if (section->num_streams < 2 && next_section->num_streams < 2) {
            section->end_x       = next_section->end_x;
            section->num_streams = max(section->num_streams, next_section->num_streams);
            vpe_vector_erase(section_vector, i + 1, 1);

            i--; // re-run check on this section, as this one and next one will have changed.
        }
    }

    return VPE_STATUS_OK;
}

static enum vpe_status mps_merge_sections_under_optimal_size(struct vpe_vector *section_vector,
    uint32_t max_seg_width, const uint32_t recout_width_alignment)
{
    bool rerun_optimization = false;

    for (uint16_t i = 0; i < (uint16_t)section_vector->num_elements; i++) {
        struct vpe_mps_section *section = vpe_vector_get(section_vector, i);

        uint32_t width            = get_section_width(section);
        uint32_t minimum_seg_size =
            get_minimum_seg_size(i == (section_vector->num_elements - 1), recout_width_alignment);
        uint32_t optimal_seg_size =
            get_optimal_seg_size(section, minimum_seg_size, recout_width_alignment);

        if (width < optimal_seg_size) {
            struct vpe_mps_section *next_section = vpe_vector_get(section_vector, i + 1);
            struct vpe_mps_section *prev_section = vpe_vector_get(section_vector, i - 1);

            if (width < minimum_seg_size)
                rerun_optimization = true;

            if (next_section == NULL && prev_section == NULL) {
                if (width < minimum_seg_size) {
                    VPE_ASSERT(false);
                    return VPE_STATUS_ERROR;
                } else {
                    break;
                }
            } else if (prev_section == NULL) { // first section
                if (next_section->num_streams >= section->num_streams) {
                    section->end_x       = next_section->end_x;
                    section->num_streams = next_section->num_streams;

                    vpe_vector_erase(section_vector, i + 1, 1);
                } else {
                    uint32_t pixels_required           = optimal_seg_size - width;
                    uint32_t minimum_next_section_size = get_minimum_seg_size(
                        (uint16_t)(i + 1) == (uint16_t)(section_vector->num_elements - 1),
                        recout_width_alignment);
                    uint32_t optimal_next_section_size = get_optimal_seg_size(
                        next_section, minimum_next_section_size, recout_width_alignment);

                    if ((uint32_t)get_section_width(next_section) >
                        optimal_next_section_size + pixels_required)
                        get_pixels_from_right_sections(section_vector, i, optimal_seg_size,
                            &rerun_optimization, recout_width_alignment);
                    else if (width < minimum_seg_size)
                        get_pixels_from_right_sections(section_vector, i, minimum_seg_size,
                            &rerun_optimization, recout_width_alignment);
                }
            } else if (next_section == NULL) {
                if (prev_section->num_streams >= section->num_streams) {
                    prev_section->end_x = section->end_x;
                    prev_section->num_streams =
                        max(section->num_streams, prev_section->num_streams);

                    vpe_vector_erase(section_vector, i, 1);
                    i--; // If we erase a previous vector, need to update i to match the new
                         // indexing.
                } else {
                    // if section has more streams than prev_section - prev_section efficiency will
                    // decrease. take as few pixels as we can
                    uint32_t pixels_required           = optimal_seg_size - width;
                    uint32_t optimal_prev_section_size = get_optimal_seg_size(prev_section,
                        get_minimum_seg_size(false, recout_width_alignment),
                        recout_width_alignment);

                    if ((uint32_t)get_section_width(prev_section) >
                        optimal_prev_section_size + pixels_required) {
                        rerun_optimization = true;
                        i -= get_pixels_from_left_sections(
                            section_vector, i, optimal_seg_size, recout_width_alignment);
                    } else if (width < minimum_seg_size) {
                        rerun_optimization = true;
                        i -= get_pixels_from_left_sections(
                            section_vector, i, minimum_seg_size, recout_width_alignment);
                    } else {
                        continue; // if section is under opt seg size but last section can't handle
                                  // it, just skip
                    }
                }

            } else {
                // Merge this section with whichever nearby section has more (or most, if
                // impossible) streams
                if (prev_section->num_streams == next_section->num_streams ||
                    (prev_section->num_streams < 2 && next_section->num_streams < 2)) {
                    if (prev_section->num_streams > section->num_streams) {
                        prev_section->end_x = next_section->end_x;
                        prev_section->num_streams =
                            max(prev_section->num_streams, next_section->num_streams);

                        vpe_vector_erase(section_vector, i, 2);
                        i--;
                    } else {
                        // Attempt to grab pixels from the next sections. If there's not enough,
                        // consume all, and let this optimization section re-run, using the
                        // prev_section algo next time.

                        uint32_t pixels_required           = optimal_seg_size - width;
                        uint32_t optimal_next_section_size = get_optimal_seg_size(next_section,
                            get_minimum_seg_size((i + 1) == (int)(section_vector->num_elements - 1),
                                recout_width_alignment),
                            recout_width_alignment);

                        if ((uint32_t)get_section_width(next_section) >
                            optimal_next_section_size + pixels_required)
                            get_pixels_from_right_sections(section_vector, i, optimal_seg_size,
                                &rerun_optimization, recout_width_alignment);
                        else if (width < minimum_seg_size)
                            get_pixels_from_right_sections(section_vector, i, minimum_seg_size,
                                &rerun_optimization, recout_width_alignment);
                    }
                } else {
                    // This shouldn't happen, as we should've merged sections with the same number
                    // of streams (or 0 and 1) earlier This will change with 3 pipe support
                    VPE_ASSERT(false);
                    return VPE_STATUS_ERROR;
                }
            }
        }
    }

    return rerun_optimization ? VPE_STATUS_REPEAT_ITEM : VPE_STATUS_OK;
}

static enum vpe_status mps_align_sections(
    struct vpe_vector *section_vector, const uint32_t recout_width_alignment)
{
    bool rerun_optimization = false;

    if (recout_width_alignment != VPE_NO_ALIGNMENT) {
        // num_elements-1 because last section doesn't need to be aligned
        for (int i = 0; i < (int)(section_vector->num_elements - 1); i++) {
            struct vpe_mps_section *section = vpe_vector_get(section_vector, i);
            uint32_t                width   = get_section_width(section);

            if ((width % recout_width_alignment) != 0) {
                struct vpe_mps_section *next_section = vpe_vector_get(section_vector, i + 1);
                VPE_ASSERT(next_section->num_streams != section->num_streams); // Sanity check

                // Whichever section has more streams in it will 'take' pixels from the other
                // segment Need to do this as a section with 1 stream can be incorporated into the 2
                // stream algo, but reverse is not true (cannot run perf mode segmentation on
                // multi-stream section)
                if (next_section->num_streams > section->num_streams) {
                    next_section->start_x -= (width % recout_width_alignment);
                    section->end_x -= (width % recout_width_alignment);

                    if (get_section_width(section) <= 0) {
                        VPE_ASSERT(get_section_width(section) == 0); // Sanity check
                        vpe_vector_erase(section_vector, i, 1);
                        i--;
                        rerun_optimization = true;
                    }
                } else {
                    int32_t pixels_required =
                        recout_width_alignment - (width % recout_width_alignment);

                    // edge case for 2nd last section - if final section doesnt have enough, just
                    // merge the two
                    if (i == (int)(section_vector->num_elements - 2)) {
                        // for 2nd last section, next section == final_section
                        if (get_section_width(next_section) < pixels_required)
                            pixels_required = get_section_width(next_section);
                    }

                    next_section->start_x += pixels_required;
                    section->end_x += pixels_required;

                    if (get_section_width(next_section) <= 0) {
                        VPE_ASSERT(get_section_width(next_section) == 0); // Sanity check
                        vpe_vector_erase(section_vector, i + 1, 1);
                        rerun_optimization = true;
                    }
                }
            }
        }
    }

    return rerun_optimization ? VPE_STATUS_REPEAT_ITEM : VPE_STATUS_OK;
}

static struct vpe_rect get_combined_dst_rect(
    const struct vpe_rect *dst_rect, uint16_t num_input_streams)
{
    if (num_input_streams < 2) {
        VPE_ASSERT(false);
        return (struct vpe_rect){0};
    }

    struct vpe_rect new_rect = dst_rect[0];

    for (int i = 1; i < num_input_streams; i++) {
        new_rect.x = min(new_rect.x, dst_rect[i].x);
        new_rect.y = min(new_rect.y, dst_rect[i].y);
        new_rect.width =
            max(new_rect.x + new_rect.width, dst_rect[i].x + dst_rect[i].width) - new_rect.x;
        new_rect.height =
            max(new_rect.y + new_rect.height, dst_rect[i].y + dst_rect[i].height) - new_rect.y;
    }

    return new_rect;
}

// find the furthest left x coordinate we can include
static int32_t get_leftmost_legal_x(struct vpe_mps_ctx *mps_ctx, const struct vpe_rect *dst_rects,
    struct vpe_mps_command *cmd, uint16_t input_idx, uint32_t recout_alignment)
{
    // if we're in the first input, we can't take any more pixels from the left
    // it should never happen
    if (input_idx == 0) {
        ASSERT(false);
        return cmd->start_x[0];
    }

    int32_t leftmost_legal_x      = cmd->start_x[max(1, input_idx) - 1];
    int32_t next_leftmost_legal_x = leftmost_legal_x; // used if input has no stream ex. bg gen

    // step 1: check if we are limited by any other streams (only 1 stream per input)
    for (int i = 0; i < mps_ctx->num_streams; i++) {
        if ((mps_ctx->stream_idx[i] == cmd->stream_idx[input_idx]) &&
            (cmd->mps_idx[input_idx] != -1)) {
            continue; // if stream is already part of this input, its not an issue
        }

        if (dst_rects[i].x < cmd->start_x[input_idx]) {
            if (dst_rects[i].x + (int32_t)dst_rects[i].width > leftmost_legal_x) {
                next_leftmost_legal_x = leftmost_legal_x;
                leftmost_legal_x      = dst_rects[i].x + dst_rects[i].width;
            }
        }
    }

    if (cmd->is_bg_gen[input_idx])
        leftmost_legal_x = next_leftmost_legal_x;

    leftmost_legal_x = max(leftmost_legal_x, cmd->start_x[0]);

    if (leftmost_legal_x < cmd->start_x[input_idx - 1])
        leftmost_legal_x = cmd->start_x[input_idx - 1];

    if (recout_alignment != VPE_NO_ALIGNMENT)
        leftmost_legal_x =
            recout_alignment * ((leftmost_legal_x + recout_alignment - 1) / recout_alignment);

    // step 2: check if taking this many pixels would cause issues in the last input (rect too
    // small)
    if (cmd->is_bg_gen[input_idx - 1] == false) {
        for (int i = 0; i < mps_ctx->num_streams; i++) {
            if (mps_ctx->stream_idx[i] != cmd->stream_idx[input_idx - 1])
                continue;

            uint32_t rect_width_in_range = get_rect_width_in_range(
                &dst_rects[i], cmd->start_x[input_idx - 1], leftmost_legal_x);
            if (rect_width_in_range < VPE_MIN_VIEWPORT_SIZE) {
                if (recout_alignment == VPE_NO_ALIGNMENT)
                    leftmost_legal_x += VPE_MIN_VIEWPORT_SIZE - rect_width_in_range;
                else
                    leftmost_legal_x += recout_alignment;

                rect_width_in_range = get_rect_width_in_range(
                    &dst_rects[i], cmd->start_x[input_idx - 1], leftmost_legal_x);
                if (rect_width_in_range < VPE_MIN_VIEWPORT_SIZE) {
                    VPE_ASSERT(false); // this should never happen
                    return cmd->start_x[input_idx];
                }
            }
            break;
        }
    }

    return leftmost_legal_x;
}

static uint16_t max_num_pipes_for_perf_mode(struct vpe_priv *vpe_priv, struct vpe_mps_command *cmd)
{
    uint16_t available_pipes = (uint16_t)vpe_priv->pub.caps->resource_caps.num_dpp;

    if (vpe_priv->output_ctx.frod_param.enable_frod)
        available_pipes = 1;

    if (vpe_priv->stream_ctx[cmd->stream_idx[0]].stream.tm_params.enable_3dlut) {
        uint16_t num_3dlut = (uint16_t)min(0xFFFF, vpe_priv->pub.caps->resource_caps.num_mpc_3dlut);

        available_pipes =
            min((uint16_t)vpe_priv->pub.caps->resource_caps.num_mpc_3dlut, available_pipes);
    }

    return available_pipes;
}

// try to even out the width of the segments in this command
// only valid for 0/1 stream sections
static void get_pixels_from_previous_input(struct vpe_mps_ctx *mps_ctx,
    const struct vpe_rect *dst_rects, struct vpe_mps_command *cmd, uint16_t input_idx,
    int32_t pixels_requested, bool *rerun_optimization, uint32_t recout_alignment)
{
    if (input_idx < 1 || input_idx >= cmd->num_inputs) {
        VPE_ASSERT(false);
        return;
    }

    // check that the region we're planning on grabbing pixels from is continuous with this one
    if (cmd->start_x[input_idx] != cmd->end_x[input_idx - 1])
        return;

    int32_t ideal_new_x = cmd->start_x[input_idx] - pixels_requested;
    int32_t leftmost_legal_x =
        get_leftmost_legal_x(mps_ctx, dst_rects, cmd, input_idx, recout_alignment);
    int32_t new_x = cmd->start_x[input_idx];

    if (recout_alignment != VPE_NO_ALIGNMENT)
        ideal_new_x = recout_alignment * ((ideal_new_x + recout_alignment - 1) / recout_alignment);

    if (leftmost_legal_x >= cmd->start_x[input_idx] || ideal_new_x >= cmd->start_x[input_idx]) {
        if (leftmost_legal_x > cmd->start_x[input_idx] || ideal_new_x > cmd->start_x[input_idx])
            VPE_ASSERT(false); // this should never happen

        return;
    }

    new_x = max(ideal_new_x, leftmost_legal_x);

    cmd->start_x[input_idx]   = new_x;
    cmd->end_x[input_idx - 1] = new_x;
}

static void divide_single_stream_cmd_perf_mode_style(struct vpe_priv *vpe_priv,
    struct vpe_mps_ctx *mps_ctx, struct vpe_mps_command *cmd, uint32_t recout_alignment,
    const struct vpe_rect *dst_rects)
{
    if (cmd->num_inputs == 0) {
        ASSERT(false);
        return;
    }

    uint16_t max_pipes = max_num_pipes_for_perf_mode(vpe_priv, cmd);
    int32_t  end_x     = cmd->end_x[cmd->num_inputs - 1];

    if (max_pipes < 2)
        return;

    if (end_x - cmd->start_x[0] > OPTIMAL_MIN_PERFORMACE_MODE_SIZE) {
        // Make this section run 'performance mode' style.
        // Will need to adjust with multi-section single-command optimization
        // (ex. can this extra pipe be better used to render a stream from another section?)

        int32_t  ideal_size     = (end_x - cmd->start_x[0]) / max_pipes;
        uint16_t new_stream_idx = cmd->stream_idx[0];
        int16_t  mps_stream_idx = cmd->mps_idx[0];

        int16_t  input_idx;

        int32_t extra_pixels = 0;
        // after evenly distributing the largest amount of pixels to each input as
        // possible (while remaining in multiples of recout_alignment, if using alignment)

        // ex if recout_align = 2, MAX_PIPE = 3, and cmd_width = 28, distribute evenly among pipes
        // ideal_size = 8, pipe width = (8, 8, 8) + 4 remaining. extra_pixels_post_alignment = 4

        // we then take the amount remaining in extra_pixels and distribute round robin style in
        // multiples of recout_align so pipe width = (8, 8, 8) + 4 extra -> (10, 8, 8) + 2 extra ->
        // (10, 10, 8)

        // for non-aligned cases add 1 ex. MAX_PIPE = 3, and cmd_width = 14 => ideal size = 14/3 = 4
        // distribute evenly among pipes pipe width = (4, 4, 4) + 2 extra => (5, 5, 4)

        VPE_ASSERT(ideal_size != 0); // sanity check incase above if statement changes

        if (recout_alignment != VPE_NO_ALIGNMENT)
            ideal_size = recout_alignment * (ideal_size / recout_alignment);

        extra_pixels = (end_x - cmd->start_x[0]) - (ideal_size * max_pipes);

        if (mps_stream_idx >= 0)
            mps_ctx->segment_count[mps_stream_idx] += (max_pipes - cmd->num_inputs);

        for (input_idx = 0; input_idx < max_pipes; input_idx++) {
            if (input_idx != 0)
                cmd->start_x[input_idx] = cmd->end_x[input_idx - 1];

            cmd->end_x[input_idx] = cmd->start_x[input_idx] + ideal_size;

            if (extra_pixels > 0) {
                if (recout_alignment == VPE_NO_ALIGNMENT) {
                    cmd->end_x[input_idx]++;
                    extra_pixels--;
                } else {
                    cmd->end_x[input_idx] += recout_alignment;
                    extra_pixels -= recout_alignment;
                }
            }

            if (input_idx == max_pipes - 1)
                VPE_ASSERT(end_x == cmd->end_x[input_idx]); // sanity check, should be same

            if (mps_stream_idx >= 0) {
                if (get_rect_width_in_range(&dst_rects[mps_stream_idx], cmd->start_x[input_idx],
                        cmd->end_x[input_idx]) > 0) {
                    cmd->stream_idx[input_idx] = new_stream_idx;
                    cmd->mps_idx[input_idx]    = mps_stream_idx;
                } else {
                    cmd->stream_idx[input_idx] = 0;
                    cmd->is_bg_gen[input_idx]  = true;
                    cmd->mps_idx[input_idx]    = -1;
                    mps_ctx->segment_count[mps_stream_idx]--;
                }
            } else {
                // Need this else case as most inputs will not be initialized yet
                cmd->stream_idx[input_idx] = 0;
                cmd->is_bg_gen[input_idx]  = true;
                cmd->mps_idx[input_idx]    = -1;
            }
        }

        cmd->num_inputs = max_pipes;
    }
}

// Attempt to even out width of segments in a command
static void even_out_segment_widths_in_cmd(struct vpe_mps_ctx *mps_ctx, struct vpe_mps_command *cmd,
    const struct vpe_rect *dst_rects, uint32_t recout_alignment)
{
    int32_t  cmd_start_x = 0;
    int32_t  cmd_end_x   = 0;
    uint16_t input_idx;

    for (input_idx = 0; input_idx < cmd->num_inputs; input_idx++) {
        if (input_idx == 0 || cmd->start_x[input_idx] < cmd_start_x)
            cmd_start_x = cmd->start_x[input_idx];

        if (input_idx == 0 || cmd->end_x[input_idx] > cmd_end_x)
            cmd_end_x = cmd->end_x[input_idx];

        // all segs need to be next to each other to be able to transfer pixels between them
        if (input_idx != 0)
            if ((cmd->end_x[input_idx - 1] != cmd->start_x[input_idx]) &&
                (cmd->end_x[input_idx] != cmd->start_x[input_idx - 1]))
                return;
    }

    int32_t ideal_seg_width = (cmd_end_x - cmd_start_x) / cmd->num_inputs;
    int32_t pixels_requested;
    bool    run_optimization;

    // Sanity check: make sure cmd is valid
    if ((cmd->num_inputs > MAX_INPUT_PIPE) || (cmd->num_inputs < 2)) {
        VPE_ASSERT(false);
        return;
    }

    do {
        run_optimization = false;

        for (input_idx = cmd->num_inputs - 1; input_idx > 0; input_idx--) {
            // leftmost segs are already max size, so can't modify starting from there
            // rightmost seg will be unoptimized (likely too small), so start there and work
            // back
            pixels_requested = ideal_seg_width - (cmd->end_x[input_idx] - cmd->start_x[input_idx]);

            if (pixels_requested > 8)
                // we need to gain or lose some pixels from this input. to avoid overadjustment,
                // don't adjust if an insignificant of adjustment is required
                get_pixels_from_previous_input(mps_ctx, dst_rects, cmd, input_idx, pixels_requested,
                    &run_optimization, recout_alignment);
        }
    } while (run_optimization);
}

// Check if there are empty inputs available for this command that can be used
static bool is_perf_mode_possible(struct vpe_priv *vpe_priv, struct vpe_mps_command *cmd)
{
    // First check if there are any free inputs
    if (cmd->num_inputs >= vpe_priv->pub.caps->resource_caps.num_dpp) {
        return false;
    }

    // Next check that all inputs are the same stream
    for (uint16_t i = 1; (i < cmd->num_inputs) && (i < MAX_INPUT_PIPE); i++) {
        if ((cmd->stream_idx[i] != cmd->stream_idx[0]) && (cmd->is_bg_gen[i] == false)) {
            return false;
        }
    }

    return true;
}

// Attempt to even out width of segments in each command a section
// This is to improve efficiency of cmd by balancing/maximizing pipe usage
static void optimize_segments_in_section(struct vpe_priv *vpe_priv, struct vpe_mps_ctx *mps_ctx,
    struct vpe_mps_section *section, const struct vpe_rect *dst_rects, uint32_t recout_alignment)
{
    if (section->num_streams < 2) {
        int16_t cmd_idx;
        for (cmd_idx = 0; cmd_idx < (uint16_t)(section->command_vector->num_elements); cmd_idx++) {
            struct vpe_mps_command *cmd = vpe_vector_get(section->command_vector, cmd_idx);

            // NOTE: this will need to be adjusted with multi-section single-command optimization
            if (is_perf_mode_possible(vpe_priv, cmd)) {
                divide_single_stream_cmd_perf_mode_style(
                    vpe_priv, mps_ctx, cmd, recout_alignment, dst_rects);
            } else {
                // if command has multiple inputs (all from one stream + bg), attempt to even out
                // the width of the segments
                even_out_segment_widths_in_cmd(mps_ctx, cmd, dst_rects, recout_alignment);
            }
        }
    }
}

static enum vpe_status calculate_segment_widths(
    struct vpe_mps_ctx *mps_ctx, const struct vpe_rect *dst_rects)
{
    // track seg_idx for each command. We already have total number of segs for each stream,
    // but need a separate variable to count up from 0 to TOTAL_NUM_SEG
    uint16_t stream_segment_idx[MAX_INPUT_PIPE] = {0};

    uint16_t section_idx, cmd_idx, input_idx;
    for (section_idx = 0; section_idx < (uint16_t)mps_ctx->section_vector->num_elements;
         section_idx++) {
        struct vpe_mps_section *section = vpe_vector_get(mps_ctx->section_vector, section_idx);

        for (cmd_idx = 0; cmd_idx < (uint16_t)section->command_vector->num_elements; cmd_idx++) {
            struct vpe_mps_command *cmd = vpe_vector_get(section->command_vector, cmd_idx);

            for (input_idx = 0; input_idx < cmd->num_inputs; input_idx++) {
                uint16_t mps_idx = cmd->mps_idx[input_idx];
                if (cmd->is_bg_gen[input_idx] == false) {
                    uint32_t width = get_rect_width_in_range(
                        &dst_rects[mps_idx], cmd->start_x[input_idx], cmd->end_x[input_idx]);

                    if (width == 0) {
                        VPE_ASSERT(false);
                        return VPE_STATUS_ERROR;
                    }

                    cmd->seg_idx[input_idx] = stream_segment_idx[mps_idx]++;
                    vpe_vector_push(mps_ctx->segment_widths[mps_idx], &width);
                }
            }
        }
    }

    for (int i = 0; i < mps_ctx->num_streams; i++) {
        // make sure we've been correctly tracking number of segments so far
        if (mps_ctx->segment_count[i] != mps_ctx->segment_widths[i]->num_elements) {
            VPE_ASSERT(false);
            return VPE_STATUS_ERROR;
        }
    }

    return VPE_STATUS_OK;
}

// mps dst_viewports need to cover total area covered, i.e. stream + background
// adjust recout and dst_viewport to do so (even required for non-bg gen streams, as blending MPCCs
// need to match)
static void mps_calculate_dst_viewport_and_active(struct stream_ctx *stream_ctx,
    struct scaler_data *scaler_data, struct vpe_rect *target_rect, int32_t seg_start_x,
    int32_t seg_end_x, struct spl_opp_adjust *opp_recout_adjust)
{
    struct vpe_priv *vpe_priv = stream_ctx->vpe_priv;

    int32_t stream_start_x = stream_ctx->stream.scaling_info.dst_rect.x;
    int32_t stream_end_x =
        stream_ctx->stream.scaling_info.dst_rect.x + stream_ctx->stream.scaling_info.dst_rect.width;

    scaler_data->recout.x = (seg_start_x >= stream_start_x) ? 0 : (stream_start_x - seg_start_x);
    scaler_data->recout.y = (target_rect->y >= stream_ctx->stream.scaling_info.dst_rect.y)
                                ? 0
                                : (stream_ctx->stream.scaling_info.dst_rect.y - target_rect->y);
    scaler_data->dst_viewport.x      = seg_start_x;
    scaler_data->dst_viewport.width  = seg_end_x - seg_start_x;
    scaler_data->dst_viewport.y      = target_rect->y;
    scaler_data->dst_viewport.height = target_rect->height;

    vpe20_update_recout_dst_viewport(scaler_data, vpe_priv->output_ctx.surface.format,
        opp_recout_adjust, (vpe_priv->init.debug.opp_background_gen == 1));
}

static void fill_background_cmd_info_input(struct vpe_priv *vpe_priv, struct vpe_rect *viewport,
    struct vpe_cmd_info *cmd_info, uint16_t input_idx)
{
    uint16_t            bg_index    = vpe_priv->resource.get_bg_stream_idx(vpe_priv);
    struct scaler_data *scaler_data = &(cmd_info->inputs[input_idx].scaler_data);
    struct stream_ctx  *stream_ctx  = &(vpe_priv->stream_ctx[bg_index]);

    vpe20_fill_bg_cmd_scaler_data(stream_ctx, viewport, scaler_data);

    // background takes stream_idx 0 as its input
    cmd_info->inputs[input_idx].stream_idx = 0;

    cmd_info->outputs[input_idx].dst_viewport   = scaler_data->dst_viewport;
    cmd_info->outputs[input_idx].dst_viewport_c = scaler_data->dst_viewport_c;
}

// generate cmd_info for blending case aka. section.num_streams > 1
static enum vpe_status fill_mps_blending_cmd_info(struct vpe_priv *vpe_priv,
    struct vpe_mps_ctx *mps_ctx, struct vpe_mps_command *command, uint16_t *num_cmds)
{
    struct vpe_cmd_info cmd_info = {0};
    uint16_t            mps_idx, input_idx, cmd_info_input_idx, seg_idx;
    enum vpe_status     status = VPE_STATUS_OK;

    // number used by countdown field, shared by whole MPS op
    *num_cmds -= 1;

    cmd_info.num_inputs         = command->num_inputs;
    cmd_info.cd                 = (uint8_t)(*num_cmds);
    cmd_info.lut3d_type         = LUT3D_TYPE_NONE;
    cmd_info.ops                = VPE_CMD_OPS_BLENDING;
    cmd_info.insert_start_csync = false;
    cmd_info.insert_end_csync   = false;

    mps_idx = 0;

    struct fmt_boundary_mode output_boundary_mode = {0}; // Initialize all to BOUNDARY_REPEAT
    struct spl_opp_adjust output_opp_adjust = {
        0}; // for entire output, start by assuming no seam then add as needed

    for (input_idx = 0; input_idx < command->num_inputs; input_idx++) {
        seg_idx                         = command->seg_idx[input_idx];
        struct stream_ctx  *stream_ctx  = &vpe_priv->stream_ctx[command->stream_idx[input_idx]];
        struct segment_ctx *segment_ctx = &stream_ctx->segment_ctx[seg_idx];

        if (segment_ctx->boundary_mode.left == FMT_SUBSAMPLING_BOUNDARY_EXTRA)
            output_boundary_mode.left = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
        if (segment_ctx->boundary_mode.right == FMT_SUBSAMPLING_BOUNDARY_EXTRA)
            output_boundary_mode.right = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
        if (segment_ctx->boundary_mode.top == FMT_SUBSAMPLING_BOUNDARY_EXTRA)
            output_boundary_mode.top = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
        if (segment_ctx->boundary_mode.bottom == FMT_SUBSAMPLING_BOUNDARY_EXTRA)
            output_boundary_mode.bottom = FMT_SUBSAMPLING_BOUNDARY_EXTRA;

        // how many pixels (absolute value) are required on each side of this stream
        // aka seg_left/right_seam will always be positive values
        int seg_left_seam =
            -segment_ctx->opp_recout_adjust
                 .x; // opp_recout_adjust.x will always be -ve or 0, we want absolute
        int seg_right_seam =
            segment_ctx->opp_recout_adjust.width + segment_ctx->opp_recout_adjust.x;

        // how many pixels (absolute value) are required on each side for whole segment (all
        // streams) aka out_left/right_seam will always be positive values
        int out_left_seam =
            -output_opp_adjust.x; // opp_recout_adjust.x will always be -ve or 0, we want absolute
        int out_right_seam = output_opp_adjust.width + output_opp_adjust.x;

        // keep increasing size of output_opp_adjust until it covers all required seams
        if (seg_left_seam > out_left_seam && seg_right_seam > out_left_seam) {
            output_opp_adjust = segment_ctx->opp_recout_adjust;
        } else if (seg_left_seam > out_left_seam) {
            output_opp_adjust.width += seg_left_seam - out_left_seam;
            output_opp_adjust.x = -seg_left_seam;
        } else if (seg_right_seam > out_right_seam) {
            output_opp_adjust.width += seg_right_seam - out_right_seam;
        }
    }
    cmd_info.outputs[0].boundary_mode = output_boundary_mode;
    cmd_info.outputs[0].opp_recout_adjust = output_opp_adjust;

    if (vpe_priv->output_ctx.frod_param.enable_frod) {
        cmd_info.frod_param.enable_frod = vpe_priv->output_ctx.frod_param.enable_frod;
        cmd_info.num_outputs            = FROD_NUM_OUTPUTS;
    } else {
        cmd_info.num_outputs = 1;
    }
    for (input_idx = 0; input_idx < command->num_inputs; input_idx++) {

        cmd_info_input_idx = command->num_inputs - input_idx - 1;
        // mps command packet packs smallest->larget stream idx (0 means bottom stream)
        // but blending command expects other way (pipe 0 is topmost pipe)

        if (command->is_bg_gen[input_idx])
            VPE_ASSERT(false); // sanity check: shouldn't be background gen in a multi_stream cmd
                               // bg gen should be handled by the bottom-most stream in blending

        mps_idx = command->mps_idx[input_idx];
        seg_idx = command->seg_idx[input_idx];

        struct stream_ctx  *stream_ctx  = &vpe_priv->stream_ctx[command->stream_idx[input_idx]];
        struct segment_ctx *segment_ctx = &stream_ctx->segment_ctx[seg_idx];

        cmd_info.lut3d_type = vpe_get_stream_lut3d_type(stream_ctx);

        cmd_info.inputs[cmd_info_input_idx].stream_idx = command->stream_idx[input_idx];

        struct scaler_data *scaler_data = &segment_ctx->scaler_data;
        struct vpe_rect    *target_rect = &vpe_priv->output_ctx.target_rect;

        // Adjust dst_viewport and recout to match whole segment
        mps_calculate_dst_viewport_and_active(stream_ctx, scaler_data, target_rect,
            command->start_x[input_idx], command->end_x[input_idx],
            &segment_ctx->opp_recout_adjust);

        int additional_left_seam_required = segment_ctx->opp_recout_adjust.x - output_opp_adjust.x;
        int additional_right_seam_required =
            (output_opp_adjust.x + output_opp_adjust.width) -
            (segment_ctx->opp_recout_adjust.x + segment_ctx->opp_recout_adjust.width);

        if (additional_left_seam_required > 0) {
            scaler_data->recout.x += additional_left_seam_required;
            scaler_data->dscl_prog_data.recout.x += additional_left_seam_required;
            scaler_data->dscl_prog_data.mpc_size.width += additional_left_seam_required;
        }

        if (additional_right_seam_required > 0)
            scaler_data->dscl_prog_data.mpc_size.width += additional_right_seam_required;

        memcpy(&(cmd_info.inputs[cmd_info_input_idx].scaler_data), scaler_data,
            sizeof(struct scaler_data));

        if (input_idx != 0) {
            // sanity check to ensure all MPC sizes are still the same after the OPP adjust
            if (scaler_data->dscl_prog_data.mpc_size.width !=
                    cmd_info.inputs[0].scaler_data.dscl_prog_data.mpc_size.width ||
                scaler_data->dscl_prog_data.mpc_size.height !=
                    cmd_info.inputs[0].scaler_data.dscl_prog_data.mpc_size.height) {
                VPE_ASSERT(false);
                status = VPE_STATUS_ERROR;
                break;
            }
        }
    }

    cmd_info.outputs[0].dst_viewport   = cmd_info.inputs[0].scaler_data.dst_viewport;
    cmd_info.outputs[0].dst_viewport_c = cmd_info.inputs[0].scaler_data.dst_viewport_c;

    if (status == VPE_STATUS_OK)
        vpe_vector_push(vpe_priv->vpe_cmd_vector, &cmd_info);

    return status;
}

// generate command for single stream case (standard compositing or performance mode)
static enum vpe_status fill_mps_performance_cmd_info(struct vpe_priv *vpe_priv,
    struct vpe_mps_ctx *mps_ctx, struct vpe_mps_command *command, uint16_t *num_cmds)
{
    struct vpe_cmd_info cmd_info = {0};
    uint16_t            mps_idx, input_idx, seg_idx;

    // number used by countdown field, shared by whole MPS op
    *num_cmds -= 1;

    cmd_info.num_inputs         = command->num_inputs;
    cmd_info.cd                 = (uint8_t)(*num_cmds);
    cmd_info.lut3d_type         = LUT3D_TYPE_NONE;
    cmd_info.ops                = VPE_CMD_OPS_COMPOSITING;
    cmd_info.insert_start_csync = false;
    cmd_info.insert_end_csync   = false;

    if (vpe_priv->output_ctx.frod_param.enable_frod) {
        VPE_ASSERT(command->num_inputs == 1); // only 1 input allowed for FROD
        cmd_info.frod_param.enable_frod = vpe_priv->output_ctx.frod_param.enable_frod;
        cmd_info.num_outputs            = FROD_NUM_OUTPUTS;
    } else {
        cmd_info.num_outputs = command->num_inputs;
    }

    mps_idx = 0;
    for (input_idx = 0; input_idx < command->num_inputs; input_idx++) {
        if (command->is_bg_gen[input_idx]) {
            struct stream_ctx *stream_ctx = &vpe_priv->stream_ctx[0];
            struct vpe_rect    viewport   = {
                     .x      = command->start_x[input_idx],
                     .y      = vpe_priv->output_ctx.target_rect.y,
                     .width  = command->end_x[input_idx] - command->start_x[input_idx],
                     .height = vpe_priv->output_ctx.target_rect.height,
            };
            fill_background_cmd_info_input(vpe_priv, &viewport, &cmd_info, input_idx);
        } else {

            mps_idx                         = command->mps_idx[input_idx];
            seg_idx                         = command->seg_idx[input_idx];
            struct stream_ctx  *stream_ctx  = &vpe_priv->stream_ctx[command->stream_idx[input_idx]];
            struct segment_ctx *segment_ctx = &stream_ctx->segment_ctx[seg_idx];

            cmd_info.lut3d_type = vpe_get_stream_lut3d_type(stream_ctx);

            cmd_info.inputs[input_idx].stream_idx = command->stream_idx[input_idx];

            // For 1 seg sections, we always need to generate background, so update dst_viewport
            struct scaler_data *scaler_data = &stream_ctx->segment_ctx[seg_idx].scaler_data;
            struct vpe_rect    *target_rect = &vpe_priv->output_ctx.target_rect;

            // Adjust dst_viewport and recout to match whole segment
            mps_calculate_dst_viewport_and_active(stream_ctx, scaler_data, target_rect,
                command->start_x[input_idx], command->end_x[input_idx],
                &segment_ctx->opp_recout_adjust);

            memcpy(
                &(cmd_info.inputs[input_idx].scaler_data), scaler_data, sizeof(struct scaler_data));

            cmd_info.outputs[input_idx].dst_viewport   = segment_ctx->scaler_data.dst_viewport;
            cmd_info.outputs[input_idx].dst_viewport_c = segment_ctx->scaler_data.dst_viewport_c;
            cmd_info.outputs[input_idx].boundary_mode  = segment_ctx->boundary_mode;
            cmd_info.outputs[input_idx].opp_recout_adjust = segment_ctx->opp_recout_adjust;
        }
    }
    vpe_vector_push(vpe_priv->vpe_cmd_vector, &cmd_info);

    return VPE_STATUS_OK;
}
/**
 * @brief Get the total number of segments for a mps blend operation
 * @param[in]     combined_dst            smallest rect that contains all input streams dst_rects
 * @param[in]     src_rects               array of src rects for each input stream involved in mps
 * blend
 * @param[in]     dst_rects               array of dst rects for each input stream involved in mps
 * blend
 * @param[in]     max_seg_width           maximum width of a segment
 * @param[in]     recout_width_alignment  alignment width for recout
 * @param[in]     num_input_streams       number of input streams for mps blend
 * @param[in/out] mps_ctx                struct to store the return values (vector of segment widths
 * and vector of sections)
 * @return 0 if segments cannot be allocated for some reason
 */
static enum vpe_status vpe_mps_build_mps_ctx(
    struct vpe_priv *vpe_priv, struct vpe_mps_input *input_params, struct vpe_mps_ctx *mps_ctx)
{
    uint32_t        max_dst_width_per_stream[MAX_INPUT_PIPE];
    double          scaling_ratios[MAX_INPUT_PIPE]; // ratio of dst over src rect width
    struct vpe_rect combined_dst;
    struct vpe_rect dst_rects[MAX_INPUT_PIPE];
    struct vpe_rect src_rects[MAX_INPUT_PIPE];
    enum vpe_status status = VPE_STATUS_OK;

    for (int i = 0; i < mps_ctx->num_streams; i++) {
        dst_rects[i] = input_params->mps_stream_ctx[i].dst_rect;
        src_rects[i] = input_params->mps_stream_ctx[i].src_rect;
    }

    if (mps_ctx->stream_idx[0] == 0)
        combined_dst = vpe_priv->output_ctx.target_rect;
    else
        combined_dst = get_combined_dst_rect(dst_rects, mps_ctx->num_streams);

    VPE_ASSERT(mps_ctx->num_streams <= vpe_priv->pub.caps->resource_caps.num_dpp);

    for (int i = 0; i < mps_ctx->num_streams; i++)
        if (mps_ctx->segment_widths[i] == NULL)
            mps_ctx->segment_widths[i] = vpe_vector_create(vpe_priv, sizeof(uint32_t), 1);

    if (mps_ctx->section_vector == NULL)
        mps_ctx->section_vector =
            vpe_vector_create(vpe_priv, sizeof(struct vpe_mps_section), MPS_INITIAL_SECTION_SIZE);

    // Downscaled inputs have a smaller max dst output width, as their src rect width will be
    // larger and must stay within 1 segment width. Compute the max dst and src rect for them.

    for (int i = 0; i < mps_ctx->num_streams; i++) {
        if (src_rects[i].width == 0) {
            VPE_ASSERT(false);
            status = VPE_STATUS_ERROR;
        }
        max_dst_width_per_stream[i] = min(input_params->max_seg_width,
            dst_rects[i].width * input_params->max_seg_width / src_rects[i].width);
        scaling_ratios[i]           = (double)dst_rects[i].width / (double)src_rects[i].width;
    }

    // MPS algo part 1 - divide combined dst rect into 'sections' ============================
    // horizontal spans that are differentiated by how many dst rects exist in that area
    if (status != VPE_STATUS_ERROR)
        status = mps_create_initial_sections(dst_rects, &combined_dst, mps_ctx->num_streams,
            input_params->recout_width_alignment, mps_ctx->section_vector);

    // MPS algo part 2 - optimize the sections we've just generated ==========================
    // 2.0 - merge conglomerate adjacent 0 and 1 stream sections ==========================
    if (status != VPE_STATUS_ERROR)
        status = mps_combine_single_zero_stream_sections(mps_ctx->section_vector);

    while (((status == VPE_STATUS_OK) || (status == VPE_STATUS_REPEAT_ITEM)) &&
           mps_ctx->section_vector->num_elements > 1) {
        // note: all 2.x functions make high num_stream sections bigger and smaller num_stream
        // sections smaller, so no infinite loop issues. worst case scenario largest num_streams
        // section takes whole target rect

        // 2.1 - check for empty streams and delete ===========================================
        // mostly a sanity check + status reset
        status = mps_check_empty(mps_ctx->section_vector);

        if (status != VPE_STATUS_OK)
            break;

        while ((status == VPE_STATUS_OK) || (status == VPE_STATUS_REPEAT_ITEM)) {
            // keep optimizing/merging until we can't anymore

            // 2.2 - add pixels to small sections from neighboring ones until optimal minimum size
            // met ================
            status = mps_merge_sections_under_optimal_size(mps_ctx->section_vector,
                input_params->max_seg_width, input_params->recout_width_alignment);

            // 2.2 - ensure the rects in every section are large enough to meet min_viewport_reqs
            if (status == VPE_STATUS_OK)
                status = enforce_minimum_viewport_size_for_rect_in_section(dst_rects,
                    scaling_ratios, input_params->recout_width_alignment, mps_ctx->section_vector,
                    mps_ctx->num_streams);

            if (status == VPE_STATUS_OK)
                break;
            else if (status != VPE_STATUS_REPEAT_ITEM)
                break;
        }

        // only align ro recout once other requirements have been met
        // Algo part 3 - Align our sections ===================================================
        status = mps_align_sections(mps_ctx->section_vector, input_params->recout_width_alignment);

        if (status == VPE_STATUS_OK)
            break;
    }

    if (mps_ctx->section_vector->num_elements == 0) {
        VPE_ASSERT(false);
        status = VPE_STATUS_ERROR;
    }

    // Algo part 4 - break sections into segment ==================================================
    if (status == VPE_STATUS_OK) {
        for (int i = 0; i < (int)(mps_ctx->section_vector->num_elements); i++) {
            struct vpe_mps_section *section = vpe_vector_get(mps_ctx->section_vector, i);
            status                          = mps_segmentation_algo(
                vpe_priv, mps_ctx, input_params, max_dst_width_per_stream, scaling_ratios, section);

            if (status != VPE_STATUS_OK)
                break;

            // adjust segment widths within a single command to be more even.
            // This can only be done for single stream / BG sections, as blending (num_streams >= 2)
            // cannot run in perf mode
            //
            // either width adjust (ex. seg width per pipe (300, 100, 500) -> (300, 300, 300))
            // and/or pipe count adjust (ex. seg width per pipe (1000, 0, 0) -> (333, 333, 334))
            optimize_segments_in_section(
                vpe_priv, mps_ctx, section, dst_rects, input_params->recout_width_alignment);
        }

        // Fill in MPS struct field segment_widths + seg_idx in commands
        calculate_segment_widths(mps_ctx, dst_rects);
    }
    return status;
}

/* ===== END HELPER FUNCTIONS ================================================================= */

enum vpe_status vpe_init_mps_ctx(
    struct vpe_priv *vpe_priv, struct stream_ctx **stream_ctx, uint16_t num_streams)
{
    struct vpe_mps_ctx **mps_ctx = &stream_ctx[0]->mps_ctx;
    *mps_ctx                     = vpe_zalloc(sizeof(struct vpe_mps_ctx));

    if (!(*mps_ctx))
        return VPE_STATUS_NO_MEMORY;

    (*mps_ctx)->num_streams = num_streams;
    for (int i = 0; i < num_streams; i++) {
        stream_ctx[i]->mps_parent_stream = stream_ctx[0];
        (*mps_ctx)->stream_idx[i]        = (uint16_t)stream_ctx[i]->stream_idx;
    }

    return VPE_STATUS_OK;
}

void vpe_free_mps_ctx(struct vpe_priv *vpe_priv, struct vpe_mps_ctx **mps_ctx)
{
    if (mps_ctx == NULL)
        return;

    if (*mps_ctx == NULL)
        return;

    if ((*mps_ctx)->section_vector != NULL) {
        if ((*mps_ctx)->section_vector->element != NULL) {
            // Because of deinit function, sometimes elements after num_elements can have cmd_vector
            // initialized
            (*mps_ctx)->section_vector->num_elements = (*mps_ctx)->section_vector->capacity;
            for (int i = 0; i < (int)((*mps_ctx)->section_vector->capacity); i++) {
                struct vpe_mps_section *section = vpe_vector_get((*mps_ctx)->section_vector, i);
                if (section != NULL)
                    if (section->command_vector != NULL)
                        if (section->command_vector->element != NULL)
                            vpe_vector_free(section->command_vector);
            }
            vpe_vector_free((*mps_ctx)->section_vector);
            (*mps_ctx)->section_vector = NULL;
        }
    }

    for (int i = 0; i < MAX_INPUT_PIPE; i++) {
        if ((*mps_ctx)->segment_widths[i] != NULL) {
            if ((*mps_ctx)->segment_widths[i]->element != NULL) {
                vpe_vector_free((*mps_ctx)->segment_widths[i]);
                (*mps_ctx)->segment_widths[i] = NULL;
            }
        }
    }

    for (int i = 0; i < (*mps_ctx)->num_streams; i++)
        vpe_priv->stream_ctx[(*mps_ctx)->stream_idx[i]].mps_parent_stream = NULL;

    vpe_free(*mps_ctx);
    *mps_ctx = NULL;
}

void vpe_clear_mps_ctx(struct vpe_priv *vpe_priv, struct vpe_mps_ctx *mps_ctx)
{
    for (int i = 0; i < mps_ctx->num_streams; i++) {
        struct stream_ctx *stream_ctx = &vpe_priv->stream_ctx[mps_ctx->stream_idx[i]];
        if (stream_ctx != stream_ctx->mps_parent_stream)
            stream_ctx->mps_parent_stream = NULL;
    }

    if (mps_ctx->section_vector != NULL) {
        if (mps_ctx->section_vector->element != NULL) {
            for (int i = 0; i < (int)(mps_ctx->section_vector->num_elements); i++) {
                struct vpe_mps_section *section = vpe_vector_get(mps_ctx->section_vector, i);
                if (section->command_vector != NULL)
                    if (section->command_vector->element != NULL)
                        vpe_vector_clear(section->command_vector);

                section->end_x       = 0;
                section->start_x     = 0;
                section->num_streams = 0;
            }
            mps_ctx->section_vector->num_elements = 0;
        }
    }

    mps_ctx->num_streams = 0;
    for (int i = 0; i < MAX_INPUT_PIPE; i++) {
        vpe_vector_clear(mps_ctx->segment_widths[i]);
        mps_ctx->stream_idx[i]    = 0;
        mps_ctx->segment_count[i] = 0;
    }
}

enum vpe_status vpe_fill_mps_blend_cmd_info(struct vpe_priv *vpe_priv, struct vpe_mps_ctx *mps_ctx)
{
    uint16_t section_idx, cmd_idx, cmd_count, num_cmds;

    if (mps_ctx == NULL)
        return VPE_STATUS_ERROR;
    else if (mps_ctx->section_vector == NULL)
        return VPE_STATUS_ERROR;

    num_cmds = 0;
    for (section_idx = 0; section_idx < (uint16_t)mps_ctx->section_vector->num_elements;
         section_idx++) {
        struct vpe_mps_section *section = vpe_vector_get(mps_ctx->section_vector, section_idx);

        if (section->command_vector == NULL)
            return VPE_STATUS_ERROR;
        else if (section->command_vector->element == NULL)
            return VPE_STATUS_ERROR;

        num_cmds += (uint16_t)section->command_vector->num_elements;
    }

    // need to generate non-blending cmds first, then blending cmds
    // i.e. first run all bg commands, then 1 stream commands, then blending
    for (cmd_count = 0; cmd_count <= (uint16_t)vpe_priv->pub.caps->resource_caps.num_dpp;
         cmd_count++) {
        for (section_idx = 0; section_idx < (uint16_t)mps_ctx->section_vector->num_elements;
             section_idx++) {
            struct vpe_mps_section *section = vpe_vector_get(mps_ctx->section_vector, section_idx);

            if (section->num_streams == cmd_count) {
                for (cmd_idx = 0; cmd_idx < (uint16_t)section->command_vector->num_elements;
                     cmd_idx++) {
                    struct vpe_mps_command *command =
                        vpe_vector_get(section->command_vector, cmd_idx);
                    if (section->num_streams < 2 || command->num_inputs < 2)
                        fill_mps_performance_cmd_info(vpe_priv, mps_ctx, command, &num_cmds);
                    else
                        fill_mps_blending_cmd_info(vpe_priv, mps_ctx, command, &num_cmds);
                }
            }
        }
    }

    return VPE_STATUS_OK;
}

// return the number of 3dluts required for MPS, which is the same as however many streams width
// 3dluts overlap in their x coordinates
static uint16_t get_num_3dlut_required(
    const struct stream_ctx **mps_stream_ctx, uint16_t num_streams, uint32_t recout_width_align)
{
    uint16_t        num_3dlut_required = 0;
    uint16_t        num_3dlut_streams  = 0;
    uint16_t        stream_idx;
    uint16_t        lut_stream_idx[MAX_INPUT_PIPE] = {0};
    struct vpe_rect lut_rects[MAX_INPUT_PIPE]      = {0};

    // create array of streams containing 3dluts as setup for next loop
    for (stream_idx = 0; stream_idx < num_streams; stream_idx++) {
        const struct stream_ctx *stream_ctx = mps_stream_ctx[stream_idx];
        if (stream_ctx->stream.tm_params.UID != 0 || stream_ctx->stream.tm_params.enable_3dlut) {
            lut_stream_idx[num_3dlut_streams] = stream_idx;
            lut_rects[num_3dlut_streams]      = stream_ctx->stream.scaling_info.dst_rect;
            num_3dlut_streams++;
        }
    }

    for (stream_idx = 0; stream_idx < num_3dlut_streams; stream_idx++) {
        uint16_t                 luts_required_at_this_x = 0;
        const struct stream_ctx *stream_ctx              = mps_stream_ctx[stream_idx];
        int32_t                  start_x = stream_ctx->stream.scaling_info.dst_rect.x;
        int32_t                  end_x   = stream_ctx->stream.scaling_info.dst_rect.x +
                        stream_ctx->stream.scaling_info.dst_rect.width;

        luts_required_at_this_x = get_num_rects_at_x_recout_align(
            start_x, lut_rects, num_3dlut_streams, recout_width_align);

        if (luts_required_at_this_x > num_3dlut_required)
            num_3dlut_required = luts_required_at_this_x;

        luts_required_at_this_x = get_num_rects_at_x_recout_align(
            end_x, lut_rects, num_3dlut_streams, recout_width_align);

        if (luts_required_at_this_x > num_3dlut_required)
            num_3dlut_required = luts_required_at_this_x;
    }

    return num_3dlut_required;
}

// check if we can run multi-pipe segmentation with these streams
bool vpe_is_mps_possible(struct vpe_priv *vpe_priv, struct stream_ctx **mps_stream_ctx,
    uint16_t num_streams, uint32_t recout_width_alignment)
{
    uint16_t num_3dlut_required     = 0;
    uint16_t num_front_end_required = 0;
    uint16_t stream_idx;
    bool     blending_required = false;

    // need to make sure our rect start value is aligned so that the alignment calulations
    // used in MPS work as expected
    if (mps_stream_ctx[0]->stream_idx == 0)
        if (recout_width_alignment != VPE_NO_ALIGNMENT)
            if (vpe_priv->output_ctx.target_rect.x % recout_width_alignment != 0)
                return false;

    if (vpe_priv->num_input_streams == 0)
        return false; // if no input streams fall back to bg_gen case - no MPS

    for (stream_idx = 0; stream_idx < num_streams; stream_idx++) {
        struct stream_ctx *current_stream_ctx = mps_stream_ctx[stream_idx];
        if (stream_idx > 0) {
            if (current_stream_ctx->stream.blend_info.blending)
                blending_required = true;

            if (current_stream_ctx->stream_idx <= mps_stream_ctx[stream_idx - 1]->stream_idx)
                return false; // we should also pass in streams in ascending order (0, 1, 2)
        }

        if (current_stream_ctx->stream.tm_params.enable_3dlut)
            num_3dlut_required++;

        num_front_end_required++;

        // for alpha combine family of ops, ensure we have enough input pipes for ALL streams
        // required
        if (current_stream_ctx->stream.flags.is_alpha_plane) {
            bool is_bkgr_op = false; // assume only alpha combine first, then check if full bkgr
            if (vpe_priv->num_streams >=
                (uint32_t)current_stream_ctx->stream_idx + VPE_BKGR_STREAM_BACKGROUND_OFFSET + 1)
                if (vpe_priv
                        ->stream_ctx[current_stream_ctx->stream_idx +
                                     VPE_BKGR_STREAM_BACKGROUND_OFFSET]
                        .stream.flags.is_background_plane)
                    is_bkgr_op = true;

            if (stream_idx >= num_streams - 1 || (stream_idx >= num_streams - 2 && is_bkgr_op)) {
                return false;
            } else {
                struct stream_ctx *alpha_combine_video_stream =
                    mps_stream_ctx[stream_idx + VPE_BKGR_STREAM_VIDEO_OFFSET];
                if (alpha_combine_video_stream->stream_idx !=
                    current_stream_ctx->stream_idx + VPE_BKGR_STREAM_VIDEO_OFFSET)
                    return false;

                if (is_bkgr_op) {
                    struct stream_ctx *bkgr_bg_stream =
                        mps_stream_ctx[stream_idx + VPE_BKGR_STREAM_BACKGROUND_OFFSET];
                    if (bkgr_bg_stream->stream_idx !=
                        current_stream_ctx->stream_idx + VPE_BKGR_STREAM_BACKGROUND_OFFSET)
                        return false;
                }
            }
        }

        // these virtual stream types shouldn't be included in MPS
        if (current_stream_ctx->stream_type == VPE_STREAM_TYPE_BG_GEN ||
            current_stream_ctx->stream_type == VPE_STREAM_TYPE_DESTINATION)
            return false;
    }

    num_3dlut_required =
        get_num_3dlut_required((const struct stream_ctx **)mps_stream_ctx, num_streams, recout_width_alignment);

    if (vpe_priv->init.debug.multi_pipe_segmentation_policy == VPE_MPS_DISABLED)
        return false;
    else if (vpe_priv->init.debug.multi_pipe_segmentation_policy == VPE_MPS_BLENDING_ONLY) {
        if (num_streams == 1 || blending_required == false)
            return false;
    }

    // need to blend in MPC with generic location, so need bg gen in MPC
    if (vpe_priv->init.debug.opp_background_gen == 1)
        return false;

    if (num_3dlut_required > vpe_priv->pub.caps->resource_caps.num_mpc_3dlut)
        return false;

    if (num_front_end_required > vpe_priv->pub.caps->resource_caps.num_dpp)
        return false;

    return true;
}

uint16_t vpe_mps_get_num_segs(struct vpe_priv *vpe_priv, struct stream_ctx *stream_ctx,
    uint32_t *max_seg_width, uint32_t recout_width_alignment)
{
    uint16_t num_segs;
    if (stream_ctx == stream_ctx->mps_parent_stream) {
        // only parent stream runs MPS algorithm
        struct vpe_mps_ctx *mps_ctx = stream_ctx->mps_ctx;
        enum vpe_status     status;

        struct vpe_mps_input input = {0};

        for (int i = 0; i < mps_ctx->num_streams; i++) {
            input.mps_stream_ctx[i].stream_ctx = &vpe_priv->stream_ctx[mps_ctx->stream_idx[i]];

            struct vpe_rect stream_local_src_rect =
                input.mps_stream_ctx[i].stream_ctx->stream.scaling_info.src_rect;
            if (input.mps_stream_ctx[i].stream_ctx->stream.rotation == VPE_ROTATION_ANGLE_90 ||
                input.mps_stream_ctx[i].stream_ctx->stream.rotation == VPE_ROTATION_ANGLE_270) {
                swap(stream_local_src_rect.width, stream_local_src_rect.height);
            }

            input.mps_stream_ctx[i].src_rect = stream_local_src_rect;
            input.mps_stream_ctx[i].dst_rect =
                input.mps_stream_ctx[i].stream_ctx->stream.scaling_info.dst_rect;
        }

        input.num_inputs             = mps_ctx->num_streams;
        input.recout_width_alignment = recout_width_alignment;
        input.max_seg_width          = *max_seg_width;

        status = vpe_mps_build_mps_ctx(vpe_priv, &input, mps_ctx);

        if (mps_ctx->num_streams == 0 || status != VPE_STATUS_OK) {
            VPE_ASSERT(false);
            return 0;
        }

        num_segs = mps_ctx->segment_count[0];

    } else { // If this is a non-parent stream for mps blend op, grab results from mps_ctx
        struct vpe_mps_ctx *mps_ctx = stream_ctx->mps_parent_stream->mps_ctx;
        uint16_t            mps_idx = 0;
        for (uint16_t i = 0; i < mps_ctx->num_streams; i++) {
            if (mps_ctx->stream_idx[i] == stream_ctx->stream_idx) {
                mps_idx = i;
                break;
            }
        }

        num_segs = mps_ctx->segment_count[mps_idx];

        if (mps_idx == 0 || num_segs != mps_ctx->segment_widths[mps_idx]->num_elements) {
            VPE_ASSERT(false);
            return VPE_STATUS_ERROR;
        }
    }
    return num_segs;
}

