/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include <stdint.h>
#include "util/u_math.h"
#include "util/u_memory.h"

#include "ac_vcn.h"
#include "ac_vcn_dec.h"
#include "ac_vcn_av1_default.h"
#include "ac_vcn_vp9_default.h"
#include "ac_uvd_dec.h"
#include "ac_cmdbuf.h"
#include "amd/addrlib/inc/addrtypes.h"

#define AES_BLOCK_SIZE 16
#define KEY_SIZE_128 16
#define CMAC_SIZE AES_BLOCK_SIZE
#define MAX_SUBSAMPLES 288 /* Maximum subsamples in a sample */

typedef struct PACKED _secure_buffer_header
{
   uint8_t cookie[8];     /* 8-byte cookie with value 'wvcencsb' */
   uint8_t version;       /* Set to 1 */
   uint8_t reserved[55];  /* Reserved for future use */
} secure_buffer_header;

typedef struct PACKED _subsample_description
{
   uint32_t num_bytes_clear;
   uint32_t num_bytes_encrypted;
   uint8_t subsample_flags;       /* Is this the first/last subsample in a sample? */
   uint8_t block_offset;          /* Used only for CTR "cenc" mode */
} subsample_description;

typedef struct PACKED _cenc_encrypt_pattern_desc
{
   uint32_t encrypt;  /* Number of 16 byte blocks to decrypt */
   uint32_t skip;     /* Number of 16 byte blocks to leave in clear */
} cenc_encrypt_pattern_desc;

typedef struct PACKED _sample_description
{
   subsample_description subsamples[MAX_SUBSAMPLES];
   uint8_t iv[AES_BLOCK_SIZE];  /* The IV for the initial subsample */
   cenc_encrypt_pattern_desc pattern;
   uint32_t subsamples_length;  /* The number of subsamples in the sample */
} sample_description;

typedef struct PACKED _native_enforce_policy_info
{
   uint8_t enabled_policy_index[4];
   uint32_t policy_array[32];
} native_enforce_policy_info;

typedef struct PACKED _signed_native_enforce_policy
{
   uint8_t wrapped_key[KEY_SIZE_128];
   native_enforce_policy_info native_policy;
   uint8_t signature[CMAC_SIZE];
} signed_native_enforce_policy;

typedef struct PACKED hw_drm_key_blob_info
{
   uint8_t wrapped_key[KEY_SIZE_128];       /* Content key encrypted with session key */
   uint8_t wrapped_key_iv[AES_BLOCK_SIZE];  /* IV used to encrypt content key */
   union {
      struct {
         uint32_t drm_session_id : 4;       /* DRM Session ID */
         uint32_t use_hw_drm_aes_ctr : 1;   /* Invoke HW-DRM with AES-CTR for content decryption */
         uint32_t use_hw_drm_aes_cbc : 1;   /* Invoke HW-DRM with AES-CBC for content decryption */
         uint32_t reserved_bits : 26;       /* Reserved fields */
      } s;
      uint32_t value;
   } u;
   signed_native_enforce_policy local_policy;
   uint8_t reserved[128];
} hw_drm_key_blob_info;

typedef struct PACKED amd_secure_buffer_format
{
   secure_buffer_header sb_header;
   sample_description desc;
   hw_drm_key_blob_info key_blob;
} amd_secure_buffer_format;

typedef struct _DECRYPT_PARAMETERS_
{
   uint32_t frame_size;        /* Size of encrypted frame */
   uint8_t encrypted_iv[16];   /* IV of the encrypted frame (clear) */
   uint8_t encrypted_key[16];  /* key to decrypt encrypted frame (encrypted with session key) */
   uint8_t session_iv[16];     /* IV to be used to decrypt encrypted_key */

   union {
      struct {
         uint32_t drm_id : 4;  /* DRM session ID */
         uint32_t ctr : 1;
         uint32_t cbc : 1;
         uint32_t reserved : 26;
      } s;
      uint32_t value;
   } u;
} DECRYPT_PARAMETERS;

struct ac_vcn_decoder {
   struct ac_video_dec base;

   enum vcn_version vcn_version;
   uint32_t av1_version;
   uint32_t stream_handle;
   uint32_t dpb_alignment;
   uint32_t hw_ctx_size;
   uint32_t it_probs_offset;
   uint32_t feedback_offset;
   uint32_t subsample_offset;

   struct {
      uint32_t data0;
      uint32_t data1;
      uint32_t cmd;
      uint32_t cntl;
   } reg;
   uint32_t addr_mode;
};

struct cmd_buffer {
   struct ac_vcn_decoder *dec;
   struct ac_cmdbuf cs;
   struct rvcn_sq_var sq;
   uint32_t decode_buffer_flags;
   rvcn_decode_buffer_t *decode_buffer;
   void *it_probs_ptr;
};

static void
ac_vcn_vp9_fill_probs_table(void *ptr)
{
   rvcn_dec_vp9_probs_t *probs = (rvcn_dec_vp9_probs_t *)ptr;

   memcpy(&probs->coef_probs[0], default_coef_probs_4x4, sizeof(default_coef_probs_4x4));
   memcpy(&probs->coef_probs[1], default_coef_probs_8x8, sizeof(default_coef_probs_8x8));
   memcpy(&probs->coef_probs[2], default_coef_probs_16x16, sizeof(default_coef_probs_16x16));
   memcpy(&probs->coef_probs[3], default_coef_probs_32x32, sizeof(default_coef_probs_32x32));
   memcpy(probs->y_mode_prob, default_if_y_probs, sizeof(default_if_y_probs));
   memcpy(probs->uv_mode_prob, default_if_uv_probs, sizeof(default_if_uv_probs));
   memcpy(probs->single_ref_prob, default_single_ref_p, sizeof(default_single_ref_p));
   memcpy(probs->switchable_interp_prob, default_switchable_interp_prob,
          sizeof(default_switchable_interp_prob));
   memcpy(probs->partition_prob, default_partition_probs, sizeof(default_partition_probs));
   memcpy(probs->inter_mode_probs, default_inter_mode_probs, sizeof(default_inter_mode_probs));
   memcpy(probs->mbskip_probs, default_skip_probs, sizeof(default_skip_probs));
   memcpy(probs->intra_inter_prob, default_intra_inter_p, sizeof(default_intra_inter_p));
   memcpy(probs->comp_inter_prob, default_comp_inter_p, sizeof(default_comp_inter_p));
   memcpy(probs->comp_ref_prob, default_comp_ref_p, sizeof(default_comp_ref_p));
   memcpy(probs->tx_probs_32x32, default_tx_probs_32x32, sizeof(default_tx_probs_32x32));
   memcpy(probs->tx_probs_16x16, default_tx_probs_16x16, sizeof(default_tx_probs_16x16));
   memcpy(probs->tx_probs_8x8, default_tx_probs_8x8, sizeof(default_tx_probs_8x8));
   memcpy(probs->mv_joints, default_nmv_joints, sizeof(default_nmv_joints));
   memcpy(&probs->mv_comps[0], default_nmv_components, sizeof(default_nmv_components));
   memset(&probs->nmvc_mask, 0, sizeof(rvcn_dec_vp9_nmv_ctx_mask_t));
}

static unsigned
ac_vcn_dec_frame_ctx_size_av1(unsigned av1_version)
{
   return av1_version == RDECODE_AV1_VER_0
      ? align(sizeof(rvcn_av1_frame_context_t), 2048)
      : align(sizeof(rvcn_av1_vcn4_frame_context_t), 2048);
}

static unsigned
ac_vcn_dec_calc_ctx_size_av1(unsigned av1_version)
{
   unsigned frame_ctxt_size = ac_vcn_dec_frame_ctx_size_av1(av1_version);
   unsigned ctx_size = (9 + 4) * frame_ctxt_size + 9 * 64 * 34 * 512 + 9 * 64 * 34 * 256 * 5;

   int num_64x64_CTB_8k = 68;
   int num_128x128_CTB_8k = 34;
   int sdb_pitch_64x64 = align(32 * num_64x64_CTB_8k, 256) * 2;
   int sdb_pitch_128x128 = align(32 * num_128x128_CTB_8k, 256) * 2;
   int sdb_lf_size_ctb_64x64 = sdb_pitch_64x64 * (align(1728, 64) / 64);
   int sdb_lf_size_ctb_128x128 = sdb_pitch_128x128 * (align(3008, 64) / 64);

   if (av1_version == RDECODE_AV1_VER_2) {
      int aligned_height_in_64x64_blk = align(4352, 64) / 64;
      int aligned_superres_total_pixels = align((78 + 2) * 3 * 32, 256);
      int sdb_superres_size_ctb = aligned_height_in_64x64_blk * aligned_superres_total_pixels;
      ctx_size += (MAX2(sdb_lf_size_ctb_64x64, sdb_lf_size_ctb_128x128) + sdb_superres_size_ctb) *
                  2 +
                  68 * 512;
   } else {
      int sdb_superres_size_ctb_64x64 = sdb_pitch_64x64 * (align(3232, 64) / 64);
      int sdb_superres_size_ctb_128x128 = sdb_pitch_128x128 * (align(6208, 64) / 64);
      int sdb_output_size_ctb_64x64 = sdb_pitch_64x64 * (align(1312, 64) / 64);
      int sdb_output_size_ctb_128x128 = sdb_pitch_128x128 * (align(2336, 64) / 64);
      int sdb_fg_avg_luma_size_ctb_64x64 = sdb_pitch_64x64 * (align(384, 64) / 64);
      int sdb_fg_avg_luma_size_ctb_128x128 = sdb_pitch_128x128 * (align(640, 64) / 64);

      ctx_size += (MAX2(sdb_lf_size_ctb_64x64, sdb_lf_size_ctb_128x128) +
         MAX2(sdb_superres_size_ctb_64x64, sdb_superres_size_ctb_128x128) +
         MAX2(sdb_output_size_ctb_64x64, sdb_output_size_ctb_128x128) +
         MAX2(sdb_fg_avg_luma_size_ctb_64x64, sdb_fg_avg_luma_size_ctb_128x128)) *
         2 +
         68 * 512;
   }

   return ctx_size;
}

static void
ac_vcn_av1_init_mode_probs(void *prob)
{
   rvcn_av1_frame_context_t *fc = (rvcn_av1_frame_context_t *)prob;
   int i;

   memcpy(fc->palette_y_size_cdf, default_palette_y_size_cdf, sizeof(default_palette_y_size_cdf));
   memcpy(fc->palette_uv_size_cdf, default_palette_uv_size_cdf, sizeof(default_palette_uv_size_cdf));
   memcpy(fc->palette_y_color_index_cdf, default_palette_y_color_index_cdf, sizeof(default_palette_y_color_index_cdf));
   memcpy(fc->palette_uv_color_index_cdf, default_palette_uv_color_index_cdf,
          sizeof(default_palette_uv_color_index_cdf));
   memcpy(fc->kf_y_cdf, default_kf_y_mode_cdf, sizeof(default_kf_y_mode_cdf));
   memcpy(fc->angle_delta_cdf, default_angle_delta_cdf, sizeof(default_angle_delta_cdf));
   memcpy(fc->comp_inter_cdf, default_comp_inter_cdf, sizeof(default_comp_inter_cdf));
   memcpy(fc->comp_ref_type_cdf, default_comp_ref_type_cdf, sizeof(default_comp_ref_type_cdf));
   memcpy(fc->uni_comp_ref_cdf, default_uni_comp_ref_cdf, sizeof(default_uni_comp_ref_cdf));
   memcpy(fc->palette_y_mode_cdf, default_palette_y_mode_cdf, sizeof(default_palette_y_mode_cdf));
   memcpy(fc->palette_uv_mode_cdf, default_palette_uv_mode_cdf, sizeof(default_palette_uv_mode_cdf));
   memcpy(fc->comp_ref_cdf, default_comp_ref_cdf, sizeof(default_comp_ref_cdf));
   memcpy(fc->comp_bwdref_cdf, default_comp_bwdref_cdf, sizeof(default_comp_bwdref_cdf));
   memcpy(fc->single_ref_cdf, default_single_ref_cdf, sizeof(default_single_ref_cdf));
   memcpy(fc->txfm_partition_cdf, default_txfm_partition_cdf, sizeof(default_txfm_partition_cdf));
   memcpy(fc->compound_index_cdf, default_compound_idx_cdfs, sizeof(default_compound_idx_cdfs));
   memcpy(fc->comp_group_idx_cdf, default_comp_group_idx_cdfs, sizeof(default_comp_group_idx_cdfs));
   memcpy(fc->newmv_cdf, default_newmv_cdf, sizeof(default_newmv_cdf));
   memcpy(fc->zeromv_cdf, default_zeromv_cdf, sizeof(default_zeromv_cdf));
   memcpy(fc->refmv_cdf, default_refmv_cdf, sizeof(default_refmv_cdf));
   memcpy(fc->drl_cdf, default_drl_cdf, sizeof(default_drl_cdf));
   memcpy(fc->motion_mode_cdf, default_motion_mode_cdf, sizeof(default_motion_mode_cdf));
   memcpy(fc->obmc_cdf, default_obmc_cdf, sizeof(default_obmc_cdf));
   memcpy(fc->inter_compound_mode_cdf, default_inter_compound_mode_cdf, sizeof(default_inter_compound_mode_cdf));
   memcpy(fc->compound_type_cdf, default_compound_type_cdf, sizeof(default_compound_type_cdf));
   memcpy(fc->wedge_idx_cdf, default_wedge_idx_cdf, sizeof(default_wedge_idx_cdf));
   memcpy(fc->interintra_cdf, default_interintra_cdf, sizeof(default_interintra_cdf));
   memcpy(fc->wedge_interintra_cdf, default_wedge_interintra_cdf, sizeof(default_wedge_interintra_cdf));
   memcpy(fc->interintra_mode_cdf, default_interintra_mode_cdf, sizeof(default_interintra_mode_cdf));
   memcpy(fc->pred_cdf, default_segment_pred_cdf, sizeof(default_segment_pred_cdf));
   memcpy(fc->switchable_restore_cdf, default_switchable_restore_cdf, sizeof(default_switchable_restore_cdf));
   memcpy(fc->wiener_restore_cdf, default_wiener_restore_cdf, sizeof(default_wiener_restore_cdf));
   memcpy(fc->sgrproj_restore_cdf, default_sgrproj_restore_cdf, sizeof(default_sgrproj_restore_cdf));
   memcpy(fc->y_mode_cdf, default_if_y_mode_cdf, sizeof(default_if_y_mode_cdf));
   memcpy(fc->uv_mode_cdf, default_uv_mode_cdf, sizeof(default_uv_mode_cdf));
   memcpy(fc->switchable_interp_cdf, default_switchable_interp_cdf, sizeof(default_switchable_interp_cdf));
   memcpy(fc->partition_cdf, default_partition_cdf, sizeof(default_partition_cdf));
   memcpy(fc->intra_ext_tx_cdf, default_intra_ext_tx_cdf, sizeof(default_intra_ext_tx_cdf));
   memcpy(fc->inter_ext_tx_cdf, default_inter_ext_tx_cdf, sizeof(default_inter_ext_tx_cdf));
   memcpy(fc->skip_cdfs, default_skip_cdfs, sizeof(default_skip_cdfs));
   memcpy(fc->intra_inter_cdf, default_intra_inter_cdf, sizeof(default_intra_inter_cdf));
   memcpy(fc->tree_cdf, default_seg_tree_cdf, sizeof(default_seg_tree_cdf));
   for (i = 0; i < SPATIAL_PREDICTION_PROBS; ++i)
      memcpy(fc->spatial_pred_seg_cdf[i], default_spatial_pred_seg_tree_cdf[i],
             sizeof(default_spatial_pred_seg_tree_cdf[i]));
   memcpy(fc->tx_size_cdf, default_tx_size_cdf, sizeof(default_tx_size_cdf));
   memcpy(fc->delta_q_cdf, default_delta_q_cdf, sizeof(default_delta_q_cdf));
   memcpy(fc->skip_mode_cdfs, default_skip_mode_cdfs, sizeof(default_skip_mode_cdfs));
   memcpy(fc->delta_lf_cdf, default_delta_lf_cdf, sizeof(default_delta_lf_cdf));
   memcpy(fc->delta_lf_multi_cdf, default_delta_lf_multi_cdf, sizeof(default_delta_lf_multi_cdf));
   memcpy(fc->cfl_sign_cdf, default_cfl_sign_cdf, sizeof(default_cfl_sign_cdf));
   memcpy(fc->cfl_alpha_cdf, default_cfl_alpha_cdf, sizeof(default_cfl_alpha_cdf));
   memcpy(fc->filter_intra_cdfs, default_filter_intra_cdfs, sizeof(default_filter_intra_cdfs));
   memcpy(fc->filter_intra_mode_cdf, default_filter_intra_mode_cdf, sizeof(default_filter_intra_mode_cdf));
   memcpy(fc->intrabc_cdf, default_intrabc_cdf, sizeof(default_intrabc_cdf));
}

static void
ac_vcn_av1_init_mv_probs(void *prob)
{
   rvcn_av1_frame_context_t *fc = (rvcn_av1_frame_context_t *)prob;

   memcpy(fc->nmvc_joints_cdf, default_nmv_context.joints_cdf, sizeof(default_nmv_context.joints_cdf));
   memcpy(fc->nmvc_0_bits_cdf, default_nmv_context.comps[0].bits_cdf, sizeof(default_nmv_context.comps[0].bits_cdf));
   memcpy(fc->nmvc_0_class0_cdf, default_nmv_context.comps[0].class0_cdf,
          sizeof(default_nmv_context.comps[0].class0_cdf));
   memcpy(fc->nmvc_0_class0_fp_cdf, default_nmv_context.comps[0].class0_fp_cdf,
          sizeof(default_nmv_context.comps[0].class0_fp_cdf));
   memcpy(fc->nmvc_0_class0_hp_cdf, default_nmv_context.comps[0].class0_hp_cdf,
          sizeof(default_nmv_context.comps[0].class0_hp_cdf));
   memcpy(fc->nmvc_0_classes_cdf, default_nmv_context.comps[0].classes_cdf,
          sizeof(default_nmv_context.comps[0].classes_cdf));
   memcpy(fc->nmvc_0_fp_cdf, default_nmv_context.comps[0].fp_cdf, sizeof(default_nmv_context.comps[0].fp_cdf));
   memcpy(fc->nmvc_0_hp_cdf, default_nmv_context.comps[0].hp_cdf, sizeof(default_nmv_context.comps[0].hp_cdf));
   memcpy(fc->nmvc_0_sign_cdf, default_nmv_context.comps[0].sign_cdf, sizeof(default_nmv_context.comps[0].sign_cdf));
   memcpy(fc->nmvc_1_bits_cdf, default_nmv_context.comps[1].bits_cdf, sizeof(default_nmv_context.comps[1].bits_cdf));
   memcpy(fc->nmvc_1_class0_cdf, default_nmv_context.comps[1].class0_cdf,
          sizeof(default_nmv_context.comps[1].class0_cdf));
   memcpy(fc->nmvc_1_class0_fp_cdf, default_nmv_context.comps[1].class0_fp_cdf,
          sizeof(default_nmv_context.comps[1].class0_fp_cdf));
   memcpy(fc->nmvc_1_class0_hp_cdf, default_nmv_context.comps[1].class0_hp_cdf,
          sizeof(default_nmv_context.comps[1].class0_hp_cdf));
   memcpy(fc->nmvc_1_classes_cdf, default_nmv_context.comps[1].classes_cdf,
          sizeof(default_nmv_context.comps[1].classes_cdf));
   memcpy(fc->nmvc_1_fp_cdf, default_nmv_context.comps[1].fp_cdf, sizeof(default_nmv_context.comps[1].fp_cdf));
   memcpy(fc->nmvc_1_hp_cdf, default_nmv_context.comps[1].hp_cdf, sizeof(default_nmv_context.comps[1].hp_cdf));
   memcpy(fc->nmvc_1_sign_cdf, default_nmv_context.comps[1].sign_cdf, sizeof(default_nmv_context.comps[1].sign_cdf));
   memcpy(fc->ndvc_joints_cdf, default_nmv_context.joints_cdf, sizeof(default_nmv_context.joints_cdf));
   memcpy(fc->ndvc_0_bits_cdf, default_nmv_context.comps[0].bits_cdf, sizeof(default_nmv_context.comps[0].bits_cdf));
   memcpy(fc->ndvc_0_class0_cdf, default_nmv_context.comps[0].class0_cdf,
          sizeof(default_nmv_context.comps[0].class0_cdf));
   memcpy(fc->ndvc_0_class0_fp_cdf, default_nmv_context.comps[0].class0_fp_cdf,
          sizeof(default_nmv_context.comps[0].class0_fp_cdf));
   memcpy(fc->ndvc_0_class0_hp_cdf, default_nmv_context.comps[0].class0_hp_cdf,
          sizeof(default_nmv_context.comps[0].class0_hp_cdf));
   memcpy(fc->ndvc_0_classes_cdf, default_nmv_context.comps[0].classes_cdf,
          sizeof(default_nmv_context.comps[0].classes_cdf));
   memcpy(fc->ndvc_0_fp_cdf, default_nmv_context.comps[0].fp_cdf, sizeof(default_nmv_context.comps[0].fp_cdf));
   memcpy(fc->ndvc_0_hp_cdf, default_nmv_context.comps[0].hp_cdf, sizeof(default_nmv_context.comps[0].hp_cdf));
   memcpy(fc->ndvc_0_sign_cdf, default_nmv_context.comps[0].sign_cdf, sizeof(default_nmv_context.comps[0].sign_cdf));
   memcpy(fc->ndvc_1_bits_cdf, default_nmv_context.comps[1].bits_cdf, sizeof(default_nmv_context.comps[1].bits_cdf));
   memcpy(fc->ndvc_1_class0_cdf, default_nmv_context.comps[1].class0_cdf,
          sizeof(default_nmv_context.comps[1].class0_cdf));
   memcpy(fc->ndvc_1_class0_fp_cdf, default_nmv_context.comps[1].class0_fp_cdf,
          sizeof(default_nmv_context.comps[1].class0_fp_cdf));
   memcpy(fc->ndvc_1_class0_hp_cdf, default_nmv_context.comps[1].class0_hp_cdf,
          sizeof(default_nmv_context.comps[1].class0_hp_cdf));
   memcpy(fc->ndvc_1_classes_cdf, default_nmv_context.comps[1].classes_cdf,
          sizeof(default_nmv_context.comps[1].classes_cdf));
   memcpy(fc->ndvc_1_fp_cdf, default_nmv_context.comps[1].fp_cdf, sizeof(default_nmv_context.comps[1].fp_cdf));
   memcpy(fc->ndvc_1_hp_cdf, default_nmv_context.comps[1].hp_cdf, sizeof(default_nmv_context.comps[1].hp_cdf));
   memcpy(fc->ndvc_1_sign_cdf, default_nmv_context.comps[1].sign_cdf, sizeof(default_nmv_context.comps[1].sign_cdf));
}

static void
ac_vcn_av1_default_coef_probs(void *prob, int index)
{
   rvcn_av1_frame_context_t *fc = (rvcn_av1_frame_context_t *)prob;

   memcpy(fc->txb_skip_cdf, av1_default_txb_skip_cdfs[index], sizeof(av1_default_txb_skip_cdfs[index]));
   memcpy(fc->eob_extra_cdf, av1_default_eob_extra_cdfs[index], sizeof(av1_default_eob_extra_cdfs[index]));
   memcpy(fc->dc_sign_cdf, av1_default_dc_sign_cdfs[index], sizeof(av1_default_dc_sign_cdfs[index]));
   memcpy(fc->coeff_br_cdf, av1_default_coeff_lps_multi_cdfs[index], sizeof(av1_default_coeff_lps_multi_cdfs[index]));
   memcpy(fc->coeff_base_cdf, av1_default_coeff_base_multi_cdfs[index],
          sizeof(av1_default_coeff_base_multi_cdfs[index]));
   memcpy(fc->coeff_base_eob_cdf, av1_default_coeff_base_eob_multi_cdfs[index],
          sizeof(av1_default_coeff_base_eob_multi_cdfs[index]));
   memcpy(fc->eob_flag_cdf16, av1_default_eob_multi16_cdfs[index], sizeof(av1_default_eob_multi16_cdfs[index]));
   memcpy(fc->eob_flag_cdf32, av1_default_eob_multi32_cdfs[index], sizeof(av1_default_eob_multi32_cdfs[index]));
   memcpy(fc->eob_flag_cdf64, av1_default_eob_multi64_cdfs[index], sizeof(av1_default_eob_multi64_cdfs[index]));
   memcpy(fc->eob_flag_cdf128, av1_default_eob_multi128_cdfs[index], sizeof(av1_default_eob_multi128_cdfs[index]));
   memcpy(fc->eob_flag_cdf256, av1_default_eob_multi256_cdfs[index], sizeof(av1_default_eob_multi256_cdfs[index]));
   memcpy(fc->eob_flag_cdf512, av1_default_eob_multi512_cdfs[index], sizeof(av1_default_eob_multi512_cdfs[index]));
   memcpy(fc->eob_flag_cdf1024, av1_default_eob_multi1024_cdfs[index], sizeof(av1_default_eob_multi1024_cdfs[index]));
}

static void
ac_vcn_vcn4_av1_init_mode_probs(void *prob)
{
   rvcn_av1_vcn4_frame_context_t *fc = (rvcn_av1_vcn4_frame_context_t *)prob;
   int i;

   memcpy(fc->palette_y_size_cdf, default_palette_y_size_cdf, sizeof(default_palette_y_size_cdf));
   memcpy(fc->palette_uv_size_cdf, default_palette_uv_size_cdf, sizeof(default_palette_uv_size_cdf));
   memcpy(fc->palette_y_color_index_cdf, default_palette_y_color_index_cdf, sizeof(default_palette_y_color_index_cdf));
   memcpy(fc->palette_uv_color_index_cdf, default_palette_uv_color_index_cdf,
          sizeof(default_palette_uv_color_index_cdf));
   memcpy(fc->kf_y_cdf, default_kf_y_mode_cdf, sizeof(default_kf_y_mode_cdf));
   memcpy(fc->angle_delta_cdf, default_angle_delta_cdf, sizeof(default_angle_delta_cdf));
   memcpy(fc->comp_inter_cdf, default_comp_inter_cdf, sizeof(default_comp_inter_cdf));
   memcpy(fc->comp_ref_type_cdf, default_comp_ref_type_cdf, sizeof(default_comp_ref_type_cdf));
   memcpy(fc->uni_comp_ref_cdf, default_uni_comp_ref_cdf, sizeof(default_uni_comp_ref_cdf));
   memcpy(fc->palette_y_mode_cdf, default_palette_y_mode_cdf, sizeof(default_palette_y_mode_cdf));
   memcpy(fc->palette_uv_mode_cdf, default_palette_uv_mode_cdf, sizeof(default_palette_uv_mode_cdf));
   memcpy(fc->comp_ref_cdf, default_comp_ref_cdf, sizeof(default_comp_ref_cdf));
   memcpy(fc->comp_bwdref_cdf, default_comp_bwdref_cdf, sizeof(default_comp_bwdref_cdf));
   memcpy(fc->single_ref_cdf, default_single_ref_cdf, sizeof(default_single_ref_cdf));
   memcpy(fc->txfm_partition_cdf, default_txfm_partition_cdf, sizeof(default_txfm_partition_cdf));
   memcpy(fc->compound_index_cdf, default_compound_idx_cdfs, sizeof(default_compound_idx_cdfs));
   memcpy(fc->comp_group_idx_cdf, default_comp_group_idx_cdfs, sizeof(default_comp_group_idx_cdfs));
   memcpy(fc->newmv_cdf, default_newmv_cdf, sizeof(default_newmv_cdf));
   memcpy(fc->zeromv_cdf, default_zeromv_cdf, sizeof(default_zeromv_cdf));
   memcpy(fc->refmv_cdf, default_refmv_cdf, sizeof(default_refmv_cdf));
   memcpy(fc->drl_cdf, default_drl_cdf, sizeof(default_drl_cdf));
   memcpy(fc->motion_mode_cdf, default_motion_mode_cdf, sizeof(default_motion_mode_cdf));
   memcpy(fc->obmc_cdf, default_obmc_cdf, sizeof(default_obmc_cdf));
   memcpy(fc->inter_compound_mode_cdf, default_inter_compound_mode_cdf, sizeof(default_inter_compound_mode_cdf));
   memcpy(fc->compound_type_cdf, default_compound_type_cdf, sizeof(default_compound_type_cdf));
   memcpy(fc->wedge_idx_cdf, default_wedge_idx_cdf, sizeof(default_wedge_idx_cdf));
   memcpy(fc->interintra_cdf, default_interintra_cdf, sizeof(default_interintra_cdf));
   memcpy(fc->wedge_interintra_cdf, default_wedge_interintra_cdf, sizeof(default_wedge_interintra_cdf));
   memcpy(fc->interintra_mode_cdf, default_interintra_mode_cdf, sizeof(default_interintra_mode_cdf));
   memcpy(fc->pred_cdf, default_segment_pred_cdf, sizeof(default_segment_pred_cdf));
   memcpy(fc->switchable_restore_cdf, default_switchable_restore_cdf, sizeof(default_switchable_restore_cdf));
   memcpy(fc->wiener_restore_cdf, default_wiener_restore_cdf, sizeof(default_wiener_restore_cdf));
   memcpy(fc->sgrproj_restore_cdf, default_sgrproj_restore_cdf, sizeof(default_sgrproj_restore_cdf));
   memcpy(fc->y_mode_cdf, default_if_y_mode_cdf, sizeof(default_if_y_mode_cdf));
   memcpy(fc->uv_mode_cdf, default_uv_mode_cdf, sizeof(default_uv_mode_cdf));
   memcpy(fc->switchable_interp_cdf, default_switchable_interp_cdf, sizeof(default_switchable_interp_cdf));
   memcpy(fc->partition_cdf, default_partition_cdf, sizeof(default_partition_cdf));
   memcpy(fc->intra_ext_tx_cdf, &default_intra_ext_tx_cdf[1], sizeof(default_intra_ext_tx_cdf[1]) * 2);
   memcpy(fc->inter_ext_tx_cdf, &default_inter_ext_tx_cdf[1], sizeof(default_inter_ext_tx_cdf[1]) * 3);
   memcpy(fc->skip_cdfs, default_skip_cdfs, sizeof(default_skip_cdfs));
   memcpy(fc->intra_inter_cdf, default_intra_inter_cdf, sizeof(default_intra_inter_cdf));
   memcpy(fc->tree_cdf, default_seg_tree_cdf, sizeof(default_seg_tree_cdf));
   for (i = 0; i < SPATIAL_PREDICTION_PROBS; ++i)
      memcpy(fc->spatial_pred_seg_cdf[i], default_spatial_pred_seg_tree_cdf[i],
             sizeof(default_spatial_pred_seg_tree_cdf[i]));
   memcpy(fc->tx_size_cdf, default_tx_size_cdf, sizeof(default_tx_size_cdf));
   memcpy(fc->delta_q_cdf, default_delta_q_cdf, sizeof(default_delta_q_cdf));
   memcpy(fc->skip_mode_cdfs, default_skip_mode_cdfs, sizeof(default_skip_mode_cdfs));
   memcpy(fc->delta_lf_cdf, default_delta_lf_cdf, sizeof(default_delta_lf_cdf));
   memcpy(fc->delta_lf_multi_cdf, default_delta_lf_multi_cdf, sizeof(default_delta_lf_multi_cdf));
   memcpy(fc->cfl_sign_cdf, default_cfl_sign_cdf, sizeof(default_cfl_sign_cdf));
   memcpy(fc->cfl_alpha_cdf, default_cfl_alpha_cdf, sizeof(default_cfl_alpha_cdf));
   memcpy(fc->filter_intra_cdfs, default_filter_intra_cdfs, sizeof(default_filter_intra_cdfs));
   memcpy(fc->filter_intra_mode_cdf, default_filter_intra_mode_cdf, sizeof(default_filter_intra_mode_cdf));
   memcpy(fc->intrabc_cdf, default_intrabc_cdf, sizeof(default_intrabc_cdf));
}

static void
ac_vcn_vcn4_av1_init_mv_probs(void *prob)
{
   rvcn_av1_vcn4_frame_context_t *fc = (rvcn_av1_vcn4_frame_context_t *)prob;

   memcpy(fc->nmvc_joints_cdf, default_nmv_context.joints_cdf, sizeof(default_nmv_context.joints_cdf));
   memcpy(fc->nmvc_0_bits_cdf, default_nmv_context.comps[0].bits_cdf, sizeof(default_nmv_context.comps[0].bits_cdf));
   memcpy(fc->nmvc_0_class0_cdf, default_nmv_context.comps[0].class0_cdf,
          sizeof(default_nmv_context.comps[0].class0_cdf));
   memcpy(fc->nmvc_0_class0_fp_cdf, default_nmv_context.comps[0].class0_fp_cdf,
          sizeof(default_nmv_context.comps[0].class0_fp_cdf));
   memcpy(fc->nmvc_0_class0_hp_cdf, default_nmv_context.comps[0].class0_hp_cdf,
          sizeof(default_nmv_context.comps[0].class0_hp_cdf));
   memcpy(fc->nmvc_0_classes_cdf, default_nmv_context.comps[0].classes_cdf,
          sizeof(default_nmv_context.comps[0].classes_cdf));
   memcpy(fc->nmvc_0_fp_cdf, default_nmv_context.comps[0].fp_cdf, sizeof(default_nmv_context.comps[0].fp_cdf));
   memcpy(fc->nmvc_0_hp_cdf, default_nmv_context.comps[0].hp_cdf, sizeof(default_nmv_context.comps[0].hp_cdf));
   memcpy(fc->nmvc_0_sign_cdf, default_nmv_context.comps[0].sign_cdf, sizeof(default_nmv_context.comps[0].sign_cdf));
   memcpy(fc->nmvc_1_bits_cdf, default_nmv_context.comps[1].bits_cdf, sizeof(default_nmv_context.comps[1].bits_cdf));
   memcpy(fc->nmvc_1_class0_cdf, default_nmv_context.comps[1].class0_cdf,
          sizeof(default_nmv_context.comps[1].class0_cdf));
   memcpy(fc->nmvc_1_class0_fp_cdf, default_nmv_context.comps[1].class0_fp_cdf,
          sizeof(default_nmv_context.comps[1].class0_fp_cdf));
   memcpy(fc->nmvc_1_class0_hp_cdf, default_nmv_context.comps[1].class0_hp_cdf,
          sizeof(default_nmv_context.comps[1].class0_hp_cdf));
   memcpy(fc->nmvc_1_classes_cdf, default_nmv_context.comps[1].classes_cdf,
          sizeof(default_nmv_context.comps[1].classes_cdf));
   memcpy(fc->nmvc_1_fp_cdf, default_nmv_context.comps[1].fp_cdf, sizeof(default_nmv_context.comps[1].fp_cdf));
   memcpy(fc->nmvc_1_hp_cdf, default_nmv_context.comps[1].hp_cdf, sizeof(default_nmv_context.comps[1].hp_cdf));
   memcpy(fc->nmvc_1_sign_cdf, default_nmv_context.comps[1].sign_cdf, sizeof(default_nmv_context.comps[1].sign_cdf));
   memcpy(fc->ndvc_joints_cdf, default_nmv_context.joints_cdf, sizeof(default_nmv_context.joints_cdf));
   memcpy(fc->ndvc_0_bits_cdf, default_nmv_context.comps[0].bits_cdf, sizeof(default_nmv_context.comps[0].bits_cdf));
   memcpy(fc->ndvc_0_class0_cdf, default_nmv_context.comps[0].class0_cdf,
          sizeof(default_nmv_context.comps[0].class0_cdf));
   memcpy(fc->ndvc_0_class0_fp_cdf, default_nmv_context.comps[0].class0_fp_cdf,
          sizeof(default_nmv_context.comps[0].class0_fp_cdf));
   memcpy(fc->ndvc_0_class0_hp_cdf, default_nmv_context.comps[0].class0_hp_cdf,
          sizeof(default_nmv_context.comps[0].class0_hp_cdf));
   memcpy(fc->ndvc_0_classes_cdf, default_nmv_context.comps[0].classes_cdf,
          sizeof(default_nmv_context.comps[0].classes_cdf));
   memcpy(fc->ndvc_0_fp_cdf, default_nmv_context.comps[0].fp_cdf, sizeof(default_nmv_context.comps[0].fp_cdf));
   memcpy(fc->ndvc_0_hp_cdf, default_nmv_context.comps[0].hp_cdf, sizeof(default_nmv_context.comps[0].hp_cdf));
   memcpy(fc->ndvc_0_sign_cdf, default_nmv_context.comps[0].sign_cdf, sizeof(default_nmv_context.comps[0].sign_cdf));
   memcpy(fc->ndvc_1_bits_cdf, default_nmv_context.comps[1].bits_cdf, sizeof(default_nmv_context.comps[1].bits_cdf));
   memcpy(fc->ndvc_1_class0_cdf, default_nmv_context.comps[1].class0_cdf,
          sizeof(default_nmv_context.comps[1].class0_cdf));
   memcpy(fc->ndvc_1_class0_fp_cdf, default_nmv_context.comps[1].class0_fp_cdf,
          sizeof(default_nmv_context.comps[1].class0_fp_cdf));
   memcpy(fc->ndvc_1_class0_hp_cdf, default_nmv_context.comps[1].class0_hp_cdf,
          sizeof(default_nmv_context.comps[1].class0_hp_cdf));
   memcpy(fc->ndvc_1_classes_cdf, default_nmv_context.comps[1].classes_cdf,
          sizeof(default_nmv_context.comps[1].classes_cdf));
   memcpy(fc->ndvc_1_fp_cdf, default_nmv_context.comps[1].fp_cdf, sizeof(default_nmv_context.comps[1].fp_cdf));
   memcpy(fc->ndvc_1_hp_cdf, default_nmv_context.comps[1].hp_cdf, sizeof(default_nmv_context.comps[1].hp_cdf));
   memcpy(fc->ndvc_1_sign_cdf, default_nmv_context.comps[1].sign_cdf, sizeof(default_nmv_context.comps[1].sign_cdf));
}

static void
ac_vcn_vcn4_av1_default_coef_probs(void *prob, int index)
{
   rvcn_av1_vcn4_frame_context_t *fc = (rvcn_av1_vcn4_frame_context_t *)prob;
   char *p;
   int i, j;
   unsigned size;

   memcpy(fc->txb_skip_cdf, av1_default_txb_skip_cdfs[index], sizeof(av1_default_txb_skip_cdfs[index]));

   p = (char *)fc->eob_extra_cdf;
   size = sizeof(av1_default_eob_extra_cdfs[0][0][0][0]) * EOB_COEF_CONTEXTS_VCN4;
   for (i = 0; i < AV1_TX_SIZES; i++) {
      for (j = 0; j < AV1_PLANE_TYPES; j++) {
         memcpy(p, &av1_default_eob_extra_cdfs[index][i][j][3], size);
         p += size;
      }
   }

   memcpy(fc->dc_sign_cdf, av1_default_dc_sign_cdfs[index], sizeof(av1_default_dc_sign_cdfs[index]));
   memcpy(fc->coeff_br_cdf, av1_default_coeff_lps_multi_cdfs[index], sizeof(av1_default_coeff_lps_multi_cdfs[index]));
   memcpy(fc->coeff_base_cdf, av1_default_coeff_base_multi_cdfs[index],
          sizeof(av1_default_coeff_base_multi_cdfs[index]));
   memcpy(fc->coeff_base_eob_cdf, av1_default_coeff_base_eob_multi_cdfs[index],
          sizeof(av1_default_coeff_base_eob_multi_cdfs[index]));
   memcpy(fc->eob_flag_cdf16, av1_default_eob_multi16_cdfs[index], sizeof(av1_default_eob_multi16_cdfs[index]));
   memcpy(fc->eob_flag_cdf32, av1_default_eob_multi32_cdfs[index], sizeof(av1_default_eob_multi32_cdfs[index]));
   memcpy(fc->eob_flag_cdf64, av1_default_eob_multi64_cdfs[index], sizeof(av1_default_eob_multi64_cdfs[index]));
   memcpy(fc->eob_flag_cdf128, av1_default_eob_multi128_cdfs[index], sizeof(av1_default_eob_multi128_cdfs[index]));
   memcpy(fc->eob_flag_cdf256, av1_default_eob_multi256_cdfs[index], sizeof(av1_default_eob_multi256_cdfs[index]));
   memcpy(fc->eob_flag_cdf512, av1_default_eob_multi512_cdfs[index], sizeof(av1_default_eob_multi512_cdfs[index]));
   memcpy(fc->eob_flag_cdf1024, av1_default_eob_multi1024_cdfs[index], sizeof(av1_default_eob_multi1024_cdfs[index]));
}

static void
ac_vcn_av1_init_probs(unsigned av1_version, uint8_t *prob)
{
   unsigned frame_ctxt_size = ac_vcn_dec_frame_ctx_size_av1(av1_version);
   if (av1_version == RDECODE_AV1_VER_0) {
      for (unsigned i = 0; i < 4; ++i) {
         ac_vcn_av1_init_mode_probs((void *)(prob + i * frame_ctxt_size));
         ac_vcn_av1_init_mv_probs((void *)(prob + i * frame_ctxt_size));
         ac_vcn_av1_default_coef_probs((void *)(prob + i * frame_ctxt_size), i);
      }
   } else {
      for (unsigned i = 0; i < 4; ++i) {
         ac_vcn_vcn4_av1_init_mode_probs((void *)(prob + i * frame_ctxt_size));
         ac_vcn_vcn4_av1_init_mv_probs((void *)(prob + i * frame_ctxt_size));
         ac_vcn_vcn4_av1_default_coef_probs((void *)(prob + i * frame_ctxt_size), i);
      }
   }

}

#define LUMA_BLOCK_SIZE_Y   73
#define LUMA_BLOCK_SIZE_X   82
#define CHROMA_BLOCK_SIZE_Y 38
#define CHROMA_BLOCK_SIZE_X 44

static int32_t
radv_vcn_av1_film_grain_random_number(unsigned short *seed, int32_t bits)
{
   unsigned short bit;
   unsigned short value = *seed;

   bit = ((value >> 0) ^ (value >> 1) ^ (value >> 3) ^ (value >> 12)) & 1;
   value = (value >> 1) | (bit << 15);
   *seed = value;

   return (value >> (16 - bits)) & ((1 << bits) - 1);
}

static void
radv_vcn_av1_film_grain_init_scaling(uint8_t scaling_points[][2], uint8_t num, short scaling_lut[])
{
   int32_t i, x, delta_x, delta_y;
   int64_t delta;

   if (num == 0)
      return;

   for (i = 0; i < scaling_points[0][0]; i++)
      scaling_lut[i] = scaling_points[0][1];

   for (i = 0; i < num - 1; i++) {
      delta_y = scaling_points[i + 1][1] - scaling_points[i][1];
      delta_x = scaling_points[i + 1][0] - scaling_points[i][0];

      delta = delta_y * (int64_t)((65536 + (delta_x >> 1)) / delta_x);

      for (x = 0; x < delta_x; x++)
         scaling_lut[scaling_points[i][0] + x] = (short)(scaling_points[i][1] + (int32_t)((x * delta + 32768) >> 16));
   }

   for (i = scaling_points[num - 1][0]; i < 256; i++)
      scaling_lut[i] = scaling_points[num - 1][1];
}

static void
ac_vcn_av1_init_film_grain_buffer(unsigned av1_version, rvcn_dec_film_grain_params_t *fg_params, rvcn_dec_av1_fg_init_buf_t *fg_buf)
{
   const int32_t luma_block_size_y = LUMA_BLOCK_SIZE_Y;
   const int32_t luma_block_size_x = LUMA_BLOCK_SIZE_X;
   const int32_t chroma_block_size_y = CHROMA_BLOCK_SIZE_Y;
   const int32_t chroma_block_size_x = CHROMA_BLOCK_SIZE_X;
   const int32_t gauss_bits = 11;
   int32_t filt_luma_grain_block[LUMA_BLOCK_SIZE_Y][LUMA_BLOCK_SIZE_X];
   int32_t filt_cb_grain_block[CHROMA_BLOCK_SIZE_Y][CHROMA_BLOCK_SIZE_X];
   int32_t filt_cr_grain_block[CHROMA_BLOCK_SIZE_Y][CHROMA_BLOCK_SIZE_X];
   int32_t chroma_subsamp_y = 1;
   int32_t chroma_subsamp_x = 1;
   unsigned short seed = fg_params->random_seed;
   int32_t ar_coeff_lag = fg_params->ar_coeff_lag;
   int32_t bit_depth = fg_params->bit_depth_minus_8 + 8;
   short grain_center = 128 << (bit_depth - 8);
   short grain_min = 0 - grain_center;
   short grain_max = (256 << (bit_depth - 8)) - 1 - grain_center;
   int32_t shift = 12 - bit_depth + fg_params->grain_scale_shift;
   short luma_grain_block_tmp[64][80];
   short cb_grain_block_tmp[32][40];
   short cr_grain_block_tmp[32][40];
   short *align_ptr, *align_ptr0, *align_ptr1;
   int32_t x, y, g, i, j, c, c0, c1, delta_row, delta_col;
   int32_t s, s0, s1, pos, r;

   /* generate luma grain block */
   memset(filt_luma_grain_block, 0, sizeof(filt_luma_grain_block));
   for (y = 0; y < luma_block_size_y; y++) {
      for (x = 0; x < luma_block_size_x; x++) {
         g = 0;
         if (fg_params->num_y_points > 0) {
            r = radv_vcn_av1_film_grain_random_number(&seed, gauss_bits);
            g = gaussian_sequence[CLAMP(r, 0, 2048 - 1)];
         }
         filt_luma_grain_block[y][x] = ROUND_POWER_OF_TWO(g, shift);
      }
   }

   for (y = 3; y < luma_block_size_y; y++) {
      for (x = 3; x < luma_block_size_x - 3; x++) {
         s = 0;
         pos = 0;
         for (delta_row = -ar_coeff_lag; delta_row <= 0; delta_row++) {
            for (delta_col = -ar_coeff_lag; delta_col <= ar_coeff_lag; delta_col++) {
               if (delta_row == 0 && delta_col == 0)
                  break;
               c = fg_params->ar_coeffs_y[pos];
               s += filt_luma_grain_block[y + delta_row][x + delta_col] * c;
               pos++;
            }
         }
         filt_luma_grain_block[y][x] = AV1_CLAMP(
            filt_luma_grain_block[y][x] + ROUND_POWER_OF_TWO(s, fg_params->ar_coeff_shift), grain_min, grain_max);
      }
   }

   /* generate chroma grain block */
   memset(filt_cb_grain_block, 0, sizeof(filt_cb_grain_block));
   shift = 12 - bit_depth + fg_params->grain_scale_shift;
   seed = fg_params->random_seed ^ 0xb524;
   for (y = 0; y < chroma_block_size_y; y++) {
      for (x = 0; x < chroma_block_size_x; x++) {
         g = 0;
         if (fg_params->num_cb_points || fg_params->chroma_scaling_from_luma) {
            r = radv_vcn_av1_film_grain_random_number(&seed, gauss_bits);
            g = gaussian_sequence[CLAMP(r, 0, 2048 - 1)];
         }
         filt_cb_grain_block[y][x] = ROUND_POWER_OF_TWO(g, shift);
      }
   }

   memset(filt_cr_grain_block, 0, sizeof(filt_cr_grain_block));
   seed = fg_params->random_seed ^ 0x49d8;
   for (y = 0; y < chroma_block_size_y; y++) {
      for (x = 0; x < chroma_block_size_x; x++) {
         g = 0;
         if (fg_params->num_cr_points || fg_params->chroma_scaling_from_luma) {
            r = radv_vcn_av1_film_grain_random_number(&seed, gauss_bits);
            g = gaussian_sequence[CLAMP(r, 0, 2048 - 1)];
         }
         filt_cr_grain_block[y][x] = ROUND_POWER_OF_TWO(g, shift);
      }
   }

   for (y = 3; y < chroma_block_size_y; y++) {
      for (x = 3; x < chroma_block_size_x - 3; x++) {
         s0 = 0, s1 = 0, pos = 0;
         for (delta_row = -ar_coeff_lag; delta_row <= 0; delta_row++) {
            for (delta_col = -ar_coeff_lag; delta_col <= ar_coeff_lag; delta_col++) {
               c0 = fg_params->ar_coeffs_cb[pos];
               c1 = fg_params->ar_coeffs_cr[pos];
               if (delta_row == 0 && delta_col == 0) {
                  if (fg_params->num_y_points > 0) {
                     int luma = 0;
                     int luma_x = ((x - 3) << chroma_subsamp_x) + 3;
                     int luma_y = ((y - 3) << chroma_subsamp_y) + 3;
                     for (i = 0; i <= chroma_subsamp_y; i++)
                        for (j = 0; j <= chroma_subsamp_x; j++)
                           luma += filt_luma_grain_block[luma_y + i][luma_x + j];

                     luma = ROUND_POWER_OF_TWO(luma, chroma_subsamp_x + chroma_subsamp_y);
                     s0 += luma * c0;
                     s1 += luma * c1;
                  }
                  break;
               }
               s0 += filt_cb_grain_block[y + delta_row][x + delta_col] * c0;
               s1 += filt_cr_grain_block[y + delta_row][x + delta_col] * c1;
               pos++;
            }
         }
         filt_cb_grain_block[y][x] = AV1_CLAMP(
            filt_cb_grain_block[y][x] + ROUND_POWER_OF_TWO(s0, fg_params->ar_coeff_shift), grain_min, grain_max);
         filt_cr_grain_block[y][x] = AV1_CLAMP(
            filt_cr_grain_block[y][x] + ROUND_POWER_OF_TWO(s1, fg_params->ar_coeff_shift), grain_min, grain_max);
      }
   }

   for (i = 9; i < luma_block_size_y; i++)
      for (j = 9; j < luma_block_size_x; j++)
         luma_grain_block_tmp[i - 9][j - 9] = filt_luma_grain_block[i][j];

   for (i = 6; i < chroma_block_size_y; i++)
      for (j = 6; j < chroma_block_size_x; j++) {
         cb_grain_block_tmp[i - 6][j - 6] = filt_cb_grain_block[i][j];
         cr_grain_block_tmp[i - 6][j - 6] = filt_cr_grain_block[i][j];
      }

   align_ptr = &fg_buf->luma_grain_block[0][0];
   align_ptr0 = &fg_buf->cb_grain_block[0][0];
   align_ptr1 = &fg_buf->cr_grain_block[0][0];

   if (av1_version == RDECODE_AV1_VER_2) {
      for (i = 0; i < 64; i++)
         for (j = 0; j < 64; j++)
            *align_ptr++ = luma_grain_block_tmp[i][j];

      for (i = 0; i < 32; i++) {
         for (j = 0; j < 32; j++) {
            *align_ptr0++ = cb_grain_block_tmp[i][j];
            *align_ptr1++ = cr_grain_block_tmp[i][j];
         }
      }
   } else {
      for (i = 0; i < 64; i++) {
         for (j = 0; j < 80; j++)
            *align_ptr++ = luma_grain_block_tmp[i][j];

         if (((i + 1) % 4) == 0)
            align_ptr += 64;
      }

      for (i = 0; i < 32; i++) {
         for (j = 0; j < 40; j++) {
            *align_ptr0++ = cb_grain_block_tmp[i][j];
            *align_ptr1++ = cr_grain_block_tmp[i][j];
         }
         if (((i + 1) % 8) == 0) {
            align_ptr0 += 64;
            align_ptr1 += 64;
         }
      }
   }

   memset(fg_buf->scaling_lut_y, 0, sizeof(fg_buf->scaling_lut_y));
   radv_vcn_av1_film_grain_init_scaling(fg_params->scaling_points_y, fg_params->num_y_points, fg_buf->scaling_lut_y);
   if (fg_params->chroma_scaling_from_luma) {
      memcpy(fg_buf->scaling_lut_cb, fg_buf->scaling_lut_y, sizeof(fg_buf->scaling_lut_y));
      memcpy(fg_buf->scaling_lut_cr, fg_buf->scaling_lut_y, sizeof(fg_buf->scaling_lut_y));
   } else {
      memset(fg_buf->scaling_lut_cb, 0, sizeof(fg_buf->scaling_lut_cb));
      memset(fg_buf->scaling_lut_cr, 0, sizeof(fg_buf->scaling_lut_cr));
      radv_vcn_av1_film_grain_init_scaling(fg_params->scaling_points_cb, fg_params->num_cb_points,
                                           fg_buf->scaling_lut_cb);
      radv_vcn_av1_film_grain_init_scaling(fg_params->scaling_points_cr, fg_params->num_cr_points,
                                           fg_buf->scaling_lut_cr);
   }
}

static uint32_t
calc_ctx_size_avc(struct ac_vcn_decoder *dec)
{
   unsigned width = align(dec->base.param.max_width, 16);
   unsigned height = align(dec->base.param.max_height, 16);
   unsigned width_in_mb = width / 16;
   unsigned height_in_mb = align(height / 16, 2);

   return dec->base.param.max_num_ref * align(width_in_mb * height_in_mb * 192, 256);
}

static uint32_t
calc_ctx_size_hevc(struct ac_vcn_decoder *dec)
{
   unsigned width = align(dec->base.param.max_width, 16);
   unsigned height = align(dec->base.param.max_height, 16);

   if (dec->base.param.max_bit_depth == 10) {
      unsigned log2_ctb_size, width_in_ctb, height_in_ctb, num_16x16_block_per_ctb;
      unsigned context_buffer_size_per_ctb_row, cm_buffer_size, max_mb_address, db_left_tile_pxl_size;
      unsigned db_left_tile_ctx_size = 4096 / 16 * (32 + 16 * 4);
      const unsigned coeff_10bit = 2;
      /* 64x64 is the maximum ctb size. */
      log2_ctb_size = 6;

      width_in_ctb = (width + ((1 << log2_ctb_size) - 1)) >> log2_ctb_size;
      height_in_ctb = (height + ((1 << log2_ctb_size) - 1)) >> log2_ctb_size;

      num_16x16_block_per_ctb = ((1 << log2_ctb_size) >> 4) * ((1 << log2_ctb_size) >> 4);
      context_buffer_size_per_ctb_row = align(width_in_ctb * num_16x16_block_per_ctb * 16, 256);
      max_mb_address = (unsigned)ceil(height * 8 / 2048.0);

      cm_buffer_size = dec->base.param.max_num_ref * context_buffer_size_per_ctb_row * height_in_ctb;
      db_left_tile_pxl_size = coeff_10bit * (max_mb_address * 2 * 2048 + 1024);

      return cm_buffer_size + db_left_tile_ctx_size + db_left_tile_pxl_size;
   }

   return ((width + 255) / 16) * ((height + 255) / 16) * 16 * dec->base.param.max_num_ref + 52 * 1024;
}

static uint32_t
calc_ctx_size_vp9(struct ac_vcn_decoder *dec)
{
   /* default probability + probability data */
   uint32_t ctx_size = 2304 * 5;

   if (dec->vcn_version >= VCN_2_0_0) {
      /* SRE collocated context data */
      ctx_size += 32 * 2 * 128 * 68;
      /* SMP collocated context data */
      ctx_size += 9 * 64 * 2 * 128 * 68;
      /* SDB left tile pixel */
      ctx_size += 8 * 2 * 2 * 8192;
   } else {
      ctx_size += 32 * 2 * 64 * 64;
      ctx_size += 9 * 64 * 2 * 64 * 64;
      ctx_size += 8 * 2 * 4096;
   }

   if (dec->base.param.max_bit_depth == 10)
      ctx_size += 8 * 2 * 4096;

   return ctx_size;
}

static uint32_t
get_session_size(struct ac_vcn_decoder *dec)
{
   switch (dec->base.param.codec) {
   case AC_VIDEO_CODEC_AVC:
      dec->hw_ctx_size = calc_ctx_size_avc(dec);
      break;
   case AC_VIDEO_CODEC_HEVC:
      dec->hw_ctx_size = calc_ctx_size_hevc(dec);
      break;
   case AC_VIDEO_CODEC_VP9:
      dec->hw_ctx_size = calc_ctx_size_vp9(dec);
      break;
   case AC_VIDEO_CODEC_AV1:
      dec->hw_ctx_size = ac_vcn_dec_calc_ctx_size_av1(dec->av1_version);
      break;
   default:
      break;
   }

   return dec->hw_ctx_size + RDECODE_SESSION_CONTEXT_SIZE;
}

static uint32_t
get_embedded_size(struct ac_vcn_decoder *dec)
{
   uint32_t size = 256;
   uint32_t it_probs_size = 0;

   size += sizeof(rvcn_dec_message_decode_t);

   if (dec->vcn_version >= VCN_3_0_0)
      size += sizeof(rvcn_dec_message_dynamic_dpb_t2_t);
   else
      size += sizeof(rvcn_dec_message_dynamic_dpb_t);

   size += sizeof(rvcn_dec_message_drm_t);
   size += sizeof(rvcn_dec_message_drm_keyblob_t);

   switch (dec->base.param.codec) {
   case AC_VIDEO_CODEC_AVC:
      it_probs_size = sizeof(rvcn_dec_avc_its_t);
      size += sizeof(rvcn_dec_message_avc_t);
      break;
   case AC_VIDEO_CODEC_HEVC:
      it_probs_size = sizeof(rvcn_dec_hevc_its_t);
      size += sizeof(rvcn_dec_message_hevc_t);
      break;
   case AC_VIDEO_CODEC_VP9:
      it_probs_size = sizeof(rvcn_dec_vp9_probs_segment_t);
      size += sizeof(rvcn_dec_message_vp9_t);
      break;
   case AC_VIDEO_CODEC_AV1:
      it_probs_size = sizeof(rvcn_dec_av1_segment_fg_t);
      size += sizeof(rvcn_dec_message_av1_t);
      break;
   case AC_VIDEO_CODEC_MPEG2:
      size += sizeof(rvcn_dec_message_mpeg2_vld_t);
      break;
   case AC_VIDEO_CODEC_VC1:
      size += sizeof(rvcn_dec_message_vc1_t);
      break;
   default:
      break;
   }

   size = align(size, 256);
   dec->it_probs_offset = size;
   size += it_probs_size;

   if (dec->vcn_version < VCN_4_0_0) {
      size = align(size, 256);
      dec->feedback_offset = size;
      size += sizeof(rvcn_dec_feedback_header_t);
   }

   size = align(size, 256);
   dec->subsample_offset = size;
   size += RDECODE_MAX_SUBSAMPLE_SIZE;

   return size;
}

static uint32_t
get_stream_handle()
{
   static struct ac_uvd_stream_handle stream_handle;
   if (!stream_handle.base)
      ac_uvd_init_stream_handle(&stream_handle);
   return ac_uvd_alloc_stream_handle(&stream_handle);
}

static uint32_t
stream_type(enum ac_video_codec codec)
{
   switch (codec) {
   case AC_VIDEO_CODEC_AVC:
      return RDECODE_CODEC_H264_PERF;
   case AC_VIDEO_CODEC_HEVC:
      return RDECODE_CODEC_H265;
   case AC_VIDEO_CODEC_VP9:
      return RDECODE_CODEC_VP9;
   case AC_VIDEO_CODEC_AV1:
      return RDECODE_CODEC_AV1;
   case AC_VIDEO_CODEC_MPEG2:
      return RDECODE_CODEC_MPEG2_VLD;
   case AC_VIDEO_CODEC_VC1:
      return RDECODE_CODEC_VC1;
   default:
      UNREACHABLE("invalid codec");
   }
}

static void
sq_header(struct ac_cmdbuf *cs, struct rvcn_sq_var *sq)
{
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(RADEON_VCN_ENGINE_INFO_SIZE);
   ac_cmdbuf_emit(RADEON_VCN_ENGINE_INFO);
   ac_cmdbuf_emit(RADEON_VCN_ENGINE_TYPE_DECODE);
   ac_cmdbuf_emit(0);
   ac_cmdbuf_end();

   sq->engine_ib_size_of_packages = &cs->buf[cs->cdw - 1];
}

static void
sq_tail(struct ac_cmdbuf *cs, struct rvcn_sq_var *sq)
{
   uint32_t *end = &cs->buf[cs->cdw];
   uint32_t size_in_dw = end - sq->engine_ib_size_of_packages + 3;

   assert(cs->cdw <= cs->max_dw);
   *sq->engine_ib_size_of_packages = size_in_dw * sizeof(uint32_t);
}

static void
send_cmd(struct cmd_buffer *cmd_buf, uint32_t cmd, uint64_t va)
{
   if (cmd_buf->dec->base.ip_type == AMD_IP_VCN_DEC) {
      ac_cmdbuf_begin(&cmd_buf->cs);
      ac_cmdbuf_emit(RDECODE_PKT0(cmd_buf->dec->reg.data0 >> 2, 0));
      ac_cmdbuf_emit(va);
      ac_cmdbuf_emit(RDECODE_PKT0(cmd_buf->dec->reg.data1 >> 2, 0));
      ac_cmdbuf_emit(va >> 32);
      ac_cmdbuf_emit(RDECODE_PKT0(cmd_buf->dec->reg.cmd >> 2, 0));
      ac_cmdbuf_emit(cmd << 1);
      ac_cmdbuf_end();
      return;
   }

   switch (cmd) {
   case RDECODE_CMD_MSG_BUFFER:
      cmd_buf->decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_MSG_BUFFER;
      cmd_buf->decode_buffer->msg_buffer_address_hi = va >> 32;
      cmd_buf->decode_buffer->msg_buffer_address_lo = va;
      break;
   case RDECODE_CMD_DPB_BUFFER:
      cmd_buf->decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_DPB_BUFFER;
      cmd_buf->decode_buffer->dpb_buffer_address_hi = va >> 32;
      cmd_buf->decode_buffer->dpb_buffer_address_lo = va;
      break;
   case RDECODE_CMD_DECODING_TARGET_BUFFER:
      cmd_buf->decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_DECODING_TARGET_BUFFER;
      cmd_buf->decode_buffer->target_buffer_address_hi = va >> 32;
      cmd_buf->decode_buffer->target_buffer_address_lo = va;
      break;
   case RDECODE_CMD_FEEDBACK_BUFFER:
      cmd_buf->decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_FEEDBACK_BUFFER;
      cmd_buf->decode_buffer->feedback_buffer_address_hi = va >> 32;
      cmd_buf->decode_buffer->feedback_buffer_address_lo = va;
      break;
   case RDECODE_CMD_PROB_TBL_BUFFER:
      cmd_buf->decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_PROB_TBL_BUFFER;
      cmd_buf->decode_buffer->prob_tbl_buffer_address_hi = va >> 32;
      cmd_buf->decode_buffer->prob_tbl_buffer_address_lo = va;
      break;
   case RDECODE_CMD_SESSION_CONTEXT_BUFFER:
      cmd_buf->decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_SESSION_CONTEXT_BUFFER;
      cmd_buf->decode_buffer->session_contex_buffer_address_hi = va >> 32;
      cmd_buf->decode_buffer->session_contex_buffer_address_lo = va;
      break;
   case RDECODE_CMD_BITSTREAM_BUFFER:
      cmd_buf->decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_BITSTREAM_BUFFER;
      cmd_buf->decode_buffer->bitstream_buffer_address_hi = va >> 32;
      cmd_buf->decode_buffer->bitstream_buffer_address_lo = va;
      break;
   case RDECODE_CMD_IT_SCALING_TABLE_BUFFER:
      cmd_buf->decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_IT_SCALING_BUFFER;
      cmd_buf->decode_buffer->it_sclr_table_buffer_address_hi = va >> 32;
      cmd_buf->decode_buffer->it_sclr_table_buffer_address_lo = va;
      break;
   case RDECODE_CMD_CONTEXT_BUFFER:
      cmd_buf->decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_CONTEXT_BUFFER;
      cmd_buf->decode_buffer->context_buffer_address_hi = va >> 32;
      cmd_buf->decode_buffer->context_buffer_address_lo = va;
      break;
   case RDECODE_CMD_SUBSAMPLE:
      cmd_buf->decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_SUBSAMPLE_SIZE_INFO_BUFFER;
      cmd_buf->decode_buffer->subsample_hi = va >> 32;
      cmd_buf->decode_buffer->subsample_lo = va;
      break;
   default:
      assert(!"unhandled cmd");
   }
}

static rvcn_decode_buffer_t *
add_ib_decode_buffer(struct ac_cmdbuf *cs)
{
   rvcn_decode_ib_package_t *ib_header = (rvcn_decode_ib_package_t *)&(cs->buf[cs->cdw]);
   ib_header->package_size = sizeof(struct rvcn_decode_buffer_s) + sizeof(*ib_header);
   ib_header->package_type = RDECODE_IB_PARAM_DECODE_BUFFER;
   cs->cdw += sizeof(*ib_header) / 4;

   rvcn_decode_buffer_t *decode_buffer = (rvcn_decode_buffer_t *)&(cs->buf[cs->cdw]);
   cs->cdw += sizeof(*decode_buffer) / 4;
   memset(decode_buffer, 0, sizeof(*decode_buffer));
   return decode_buffer;
}

static int
vcn_init_session_buf(struct ac_video_dec *decoder, void *ptr)
{
   struct ac_vcn_decoder *dec = (struct ac_vcn_decoder *)decoder;
   void *hw_ctx_ptr = (uint8_t *)ptr + RDECODE_SESSION_CONTEXT_SIZE;

   if (decoder->param.codec == AC_VIDEO_CODEC_VP9)
      ac_vcn_vp9_fill_probs_table(hw_ctx_ptr);
   else if (decoder->param.codec == AC_VIDEO_CODEC_AV1)
      ac_vcn_av1_init_probs(dec->av1_version, hw_ctx_ptr);

   return 0;
}

static int
vcn_build_create_cmd(struct ac_video_dec *decoder, struct ac_video_dec_create_cmd *cmd)
{
   struct ac_vcn_decoder *dec = (struct ac_vcn_decoder *)decoder;
   uint8_t *emb = cmd->embedded_ptr;

   rvcn_dec_message_header_t *header = (rvcn_dec_message_header_t *)emb;
   header->header_size = sizeof(rvcn_dec_message_header_t);
   header->total_size = sizeof(rvcn_dec_message_header_t) + sizeof(rvcn_dec_message_create_t);
   header->num_buffers = 1;
   header->msg_type = RDECODE_MSG_CREATE;
   header->stream_handle = dec->stream_handle;
   header->status_report_feedback_number = 0;

   header->index[0].message_id = RDECODE_MESSAGE_CREATE;
   header->index[0].offset = sizeof(rvcn_dec_message_header_t);
   header->index[0].size = sizeof(rvcn_dec_message_create_t);
   header->index[0].filled = 0;

   rvcn_dec_message_create_t *create =
      (rvcn_dec_message_create_t *)(emb + sizeof(rvcn_dec_message_header_t));
   create->stream_type = stream_type(decoder->param.codec);
   create->session_flags = 0;
   create->width_in_samples = decoder->param.max_width;
   create->height_in_samples = decoder->param.max_height;

   struct cmd_buffer cmd_buf = {
      .dec = dec,
      .cs = {
         .buf = cmd->cmd_buffer,
         .max_dw = decoder->max_create_cmd_dw,
      }
   };

   if (decoder->ip_type == AMD_IP_VCN_UNIFIED) {
      sq_header(&cmd_buf.cs, &cmd_buf.sq);
      cmd_buf.decode_buffer = add_ib_decode_buffer(&cmd_buf.cs);
   }

   send_cmd(&cmd_buf, RDECODE_CMD_SESSION_CONTEXT_BUFFER, cmd->session_va);
   send_cmd(&cmd_buf, RDECODE_CMD_MSG_BUFFER, cmd->embedded_va);

   if (decoder->ip_type == AMD_IP_VCN_UNIFIED) {
      cmd_buf.decode_buffer->valid_buf_flag = cmd_buf.decode_buffer_flags;
      sq_tail(&cmd_buf.cs, &cmd_buf.sq);
   }

   cmd->out.cmd_dw = cmd_buf.cs.cdw;
   return 0;
}

static uint32_t
build_avc_msg(struct cmd_buffer *cmd_buf, struct ac_video_dec_decode_cmd *cmd, rvcn_dec_message_avc_t *codec)
{
   struct ac_video_dec_avc *avc = &cmd->codec_param.avc;
   rvcn_dec_avc_its_t *its = cmd_buf->it_probs_ptr;
   rvcn_dec_message_avc_t msg = {0};

   msg.sps_info_flags |= avc->sps_flags.direct_8x8_inference_flag
                         << RDECODE_SPS_INFO_H264_DIRECT_8X8_INFERENCE_FLAG_SHIFT;
   msg.sps_info_flags |= avc->pic_flags.mbaff_frame_flag
                         << RDECODE_SPS_INFO_H264_MB_ADAPTIVE_FRAME_FIELD_FLAG_SHIFT;
   msg.sps_info_flags |= avc->sps_flags.frame_mbs_only_flag
                         << RDECODE_SPS_INFO_H264_FRAME_MBS_ONLY_FLAG_SHIFT;
   msg.sps_info_flags |= avc->sps_flags.delta_pic_order_always_zero_flag
                         << RDECODE_SPS_INFO_H264_DELTA_PIC_ORDER_ALWAYS_ZERO_FLAG_SHIFT;
   msg.sps_info_flags |= avc->sps_flags.gaps_in_frame_num_value_allowed_flag
                         << RDECODE_SPS_INFO_H264_GAPS_IN_FRAME_NUM_VALUE_ALLOWED_FLAG_SHIFT;
   if (cmd->tier < AC_VIDEO_DEC_TIER2)
      msg.sps_info_flags |= 1 << RDECODE_SPS_INFO_H264_EXTENSION_SUPPORT_FLAG_SHIFT;

   msg.pps_info_flags |= avc->pps_flags.transform_8x8_mode_flag << 0;
   msg.pps_info_flags |= avc->pps_flags.redundant_pic_cnt_present_flag << 1;
   msg.pps_info_flags |= avc->pps_flags.constrained_intra_pred_flag << 2;
   msg.pps_info_flags |= avc->pps_flags.deblocking_filter_control_present_flag << 3;
   msg.pps_info_flags |= avc->pps_flags.weighted_bipred_idc << 4;
   msg.pps_info_flags |= avc->pps_flags.weighted_pred_flag << 6;
   msg.pps_info_flags |= avc->pps_flags.bottom_field_pic_order_in_frame_present_flag << 7;
   msg.pps_info_flags |= avc->pps_flags.entropy_coding_mode_flag << 8;

   switch (avc->profile_idc) {
   case 66:
      msg.profile = RDECODE_H264_PROFILE_BASELINE;
      break;
   case 77:
      msg.profile = RDECODE_H264_PROFILE_MAIN;
      break;
   case 100:
      msg.profile = RDECODE_H264_PROFILE_HIGH;
      break;
   default:
      break;
   }

   msg.level = avc->level_idc;
   msg.chroma_format = avc->pic_flags.chroma_format_idc;
   msg.bit_depth_luma_minus8 = avc->bit_depth_luma_minus8;
   msg.bit_depth_chroma_minus8 = avc->bit_depth_chroma_minus8;
   msg.log2_max_frame_num_minus4 = avc->log2_max_frame_num_minus4;
   msg.pic_order_cnt_type = avc->pic_order_cnt_type;
   msg.log2_max_pic_order_cnt_lsb_minus4 = avc->log2_max_pic_order_cnt_lsb_minus4;
   msg.num_ref_frames = avc->max_num_ref_frames;
   msg.pic_init_qp_minus26 = avc->pic_init_qp_minus26;
   msg.pic_init_qs_minus26 = avc->pic_init_qs_minus26;
   msg.chroma_qp_index_offset = avc->chroma_qp_index_offset;
   msg.second_chroma_qp_index_offset = avc->second_chroma_qp_index_offset;
   msg.num_slice_groups_minus1 = avc->num_slice_groups_minus1;
   msg.slice_group_map_type = avc->slice_group_map_type;
   msg.num_ref_idx_l0_active_minus1 = avc->num_ref_idx_l0_default_active_minus1;
   msg.num_ref_idx_l1_active_minus1 = avc->num_ref_idx_l1_default_active_minus1;
   msg.slice_group_change_rate_minus1 = avc->slice_group_change_rate_minus1;
   msg.frame_num = avc->frame_num;
   msg.curr_field_order_cnt_list[0] = avc->curr_field_order_cnt[0];
   msg.curr_field_order_cnt_list[1] = avc->curr_field_order_cnt[1];
   msg.curr_pic_ref_frame_num = avc->curr_pic_ref_frame_num;
   msg.non_existing_frame_flags = avc->non_existing_frame_flags;
   msg.used_for_reference_flags = avc->used_for_reference_flags;
   msg.decoded_pic_idx = avc->curr_pic_id;

   for (uint32_t i = 0; i < H264_MAX_NUM_REF_PICS; i++) {
      msg.frame_num_list[i] = avc->frame_num_list[i];
      msg.field_order_cnt_list[i][0] = avc->field_order_cnt_list[i][0];
      msg.field_order_cnt_list[i][1] = avc->field_order_cnt_list[i][1];
      msg.ref_frame_list[i] = avc->ref_frame_id_list[i];
      if (avc->used_for_long_term_ref_flags & (1 << i))
         msg.ref_frame_list[i] |= 0x80;
   }

   memcpy(its->scaling_list_4x4, avc->scaling_list_4x4, sizeof(avc->scaling_list_4x4));
   memcpy(its->scaling_list_8x8, avc->scaling_list_8x8, sizeof(avc->scaling_list_8x8));

   memcpy(codec, &msg, sizeof(msg));
   return sizeof(msg);
}

static uint32_t
build_hevc_msg(struct cmd_buffer *cmd_buf, struct ac_video_dec_decode_cmd *cmd, rvcn_dec_message_hevc_t *codec)
{
   struct ac_video_dec_hevc *hevc = &cmd->codec_param.hevc;
   rvcn_dec_hevc_its_t *its = cmd_buf->it_probs_ptr;
   rvcn_dec_message_hevc_t msg = {0};

   msg.sps_info_flags |= hevc->sps_flags.scaling_list_enabled_flag << 0;
   msg.sps_info_flags |= hevc->sps_flags.amp_enabled_flag << 1;
   msg.sps_info_flags |= hevc->sps_flags.sample_adaptive_offset_enabled_flag << 2;
   msg.sps_info_flags |= hevc->sps_flags.pcm_enabled_flag << 3;
   msg.sps_info_flags |= hevc->sps_flags.pcm_loop_filter_disabled_flag << 4;
   msg.sps_info_flags |= hevc->sps_flags.long_term_ref_pics_present_flag << 5;
   msg.sps_info_flags |= hevc->sps_flags.sps_temporal_mvp_enabled_flag << 6;
   msg.sps_info_flags |= hevc->sps_flags.strong_intra_smoothing_enabled_flag << 7;
   msg.sps_info_flags |= hevc->sps_flags.separate_colour_plane_flag << 8;
   msg.sps_info_flags |= !!hevc->num_bits_for_st_ref_pic_set_in_slice << 11;

   msg.pps_info_flags |= hevc->pps_flags.dependent_slice_segments_enabled_flag << 0;
   msg.pps_info_flags |= hevc->pps_flags.output_flag_present_flag << 1;
   msg.pps_info_flags |= hevc->pps_flags.sign_data_hiding_enabled_flag << 2;
   msg.pps_info_flags |= hevc->pps_flags.cabac_init_present_flag << 3;
   msg.pps_info_flags |= hevc->pps_flags.constrained_intra_pred_flag << 4;
   msg.pps_info_flags |= hevc->pps_flags.transform_skip_enabled_flag << 5;
   msg.pps_info_flags |= hevc->pps_flags.cu_qp_delta_enabled_flag << 6;
   msg.pps_info_flags |= hevc->pps_flags.pps_slice_chroma_qp_offsets_present_flag << 7;
   msg.pps_info_flags |= hevc->pps_flags.weighted_pred_flag << 8;
   msg.pps_info_flags |= hevc->pps_flags.weighted_bipred_flag << 9;
   msg.pps_info_flags |= hevc->pps_flags.transquant_bypass_enabled_flag << 10;
   msg.pps_info_flags |= hevc->pps_flags.tiles_enabled_flag << 11;
   msg.pps_info_flags |= hevc->pps_flags.entropy_coding_sync_enabled_flag << 12;
   msg.pps_info_flags |= hevc->pps_flags.uniform_spacing_flag << 13;
   msg.pps_info_flags |= hevc->pps_flags.loop_filter_across_tiles_enabled_flag << 14;
   msg.pps_info_flags |= hevc->pps_flags.pps_loop_filter_across_slices_enabled_flag << 15;
   msg.pps_info_flags |= hevc->pps_flags.deblocking_filter_override_enabled_flag << 16;
   msg.pps_info_flags |= hevc->pps_flags.pps_deblocking_filter_disabled_flag << 17;
   msg.pps_info_flags |= hevc->pps_flags.lists_modification_present_flag << 18;
   msg.pps_info_flags |= hevc->pps_flags.slice_segment_header_extension_present_flag << 19;

   msg.chroma_format = hevc->chroma_format_idc;
   msg.bit_depth_luma_minus8 = hevc->bit_depth_luma_minus8;
   msg.bit_depth_chroma_minus8 = hevc->bit_depth_chroma_minus8;
   msg.log2_max_pic_order_cnt_lsb_minus4 = hevc->log2_max_pic_order_cnt_lsb_minus4;
   msg.sps_max_dec_pic_buffering_minus1 = hevc->sps_max_dec_pic_buffering_minus1;
   msg.log2_min_luma_coding_block_size_minus3 = hevc->log2_min_luma_coding_block_size_minus3;
   msg.log2_diff_max_min_luma_coding_block_size = hevc->log2_diff_max_min_luma_coding_block_size;
   msg.log2_min_transform_block_size_minus2 = hevc->log2_min_transform_block_size_minus2;
   msg.log2_diff_max_min_transform_block_size = hevc->log2_diff_max_min_transform_block_size;
   msg.max_transform_hierarchy_depth_inter = hevc->max_transform_hierarchy_depth_inter;
   msg.max_transform_hierarchy_depth_intra = hevc->max_transform_hierarchy_depth_intra;
   msg.pcm_sample_bit_depth_luma_minus1 = hevc->pcm_sample_bit_depth_luma_minus1;
   msg.pcm_sample_bit_depth_chroma_minus1 = hevc->pcm_sample_bit_depth_chroma_minus1;
   msg.log2_min_pcm_luma_coding_block_size_minus3 = hevc->log2_min_pcm_luma_coding_block_size_minus3;
   msg.log2_diff_max_min_pcm_luma_coding_block_size = hevc->log2_diff_max_min_pcm_luma_coding_block_size;
   msg.num_extra_slice_header_bits = hevc->num_extra_slice_header_bits;
   msg.num_short_term_ref_pic_sets = hevc->num_short_term_ref_pic_sets;
   msg.num_long_term_ref_pic_sps = hevc->num_long_term_ref_pics_sps;
   msg.num_ref_idx_l0_default_active_minus1 = hevc->num_ref_idx_l0_default_active_minus1;
   msg.num_ref_idx_l1_default_active_minus1 = hevc->num_ref_idx_l1_default_active_minus1;
   msg.pps_cb_qp_offset = hevc->pps_cb_qp_offset;
   msg.pps_cr_qp_offset = hevc->pps_cr_qp_offset;
   msg.pps_beta_offset_div2 = hevc->pps_beta_offset_div2;
   msg.pps_tc_offset_div2 = hevc->pps_tc_offset_div2;
   msg.diff_cu_qp_delta_depth = hevc->diff_cu_qp_delta_depth;
   msg.num_tile_columns_minus1 = hevc->num_tile_columns_minus1;
   msg.num_tile_rows_minus1 = hevc->num_tile_rows_minus1;
   msg.log2_parallel_merge_level_minus2 = hevc->log2_parallel_merge_level_minus2;
   msg.st_rps_bits = hevc->num_bits_for_st_ref_pic_set_in_slice;
   msg.init_qp_minus26 = hevc->init_qp_minus26;
   msg.num_delta_pocs_ref_rps_idx = hevc->num_delta_pocs_of_ref_rps_idx;
   msg.curr_idx = hevc->curr_pic_id;
   msg.curr_poc = hevc->curr_poc;
   msg.highestTid = 0xff;
   msg.isNonRef = hevc->pic_flags.is_ref_pic_flag ? 0 : 1;

   if (hevc->bit_depth_luma_minus8 || hevc->bit_depth_chroma_minus8) {
      msg.p010_mode = 1;
      msg.msb_mode = 1;
   }

   for (uint32_t i = 0; i < H265_TILE_COLS_LIST_SIZE; i++)
      msg.column_width_minus1[i] = hevc->column_width_minus1[i];

   for (uint32_t i = 0; i < H265_TILE_ROWS_LIST_SIZE; i++)
      msg.row_height_minus1[i] = hevc->row_height_minus1[i];

   for (uint32_t i = 0; i < H265_MAX_NUM_REF_PICS; i++) {
      msg.ref_pic_list[i] = hevc->ref_pic_id_list[i];
      msg.poc_list[i] = hevc->ref_poc_list[i];
   }

   for (uint32_t i = 0; i < H265_MAX_RPS_SIZE; i++) {
      msg.ref_pic_set_st_curr_before[i] = hevc->ref_pic_set_st_curr_before[i];
      msg.ref_pic_set_st_curr_after[i] = hevc->ref_pic_set_st_curr_after[i];
      msg.ref_pic_set_lt_curr[i] = hevc->ref_pic_set_lt_curr[i];
   }

   for (uint32_t i = 0; i < H265_SCALING_LIST_16X16_NUM_LISTS; i++)
      msg.ucScalingListDCCoefSizeID2[i] = hevc->scaling_list_dc_coef_16x16[i];

   for (uint32_t i = 0; i < H265_SCALING_LIST_32X32_NUM_LISTS; i++)
      msg.ucScalingListDCCoefSizeID3[i] = hevc->scaling_list_dc_coef_32x32[i];

   memcpy(its->scaling_list_4x4, hevc->scaling_list_4x4, sizeof(hevc->scaling_list_4x4));
   memcpy(its->scaling_list_8x8, hevc->scaling_list_8x8, sizeof(hevc->scaling_list_8x8));
   memcpy(its->scaling_list_16x16, hevc->scaling_list_16x16, sizeof(hevc->scaling_list_16x16));
   memcpy(its->scaling_list_32x32, hevc->scaling_list_32x32, sizeof(hevc->scaling_list_32x32));

   memcpy(codec, &msg, sizeof(msg));
   return sizeof(msg);
}

static uint32_t
build_vp9_msg(struct cmd_buffer *cmd_buf, struct ac_video_dec_decode_cmd *cmd, rvcn_dec_message_vp9_t *codec)
{
   struct ac_video_dec_vp9 *vp9 = &cmd->codec_param.vp9;
   rvcn_dec_vp9_probs_segment_t *probs = cmd_buf->it_probs_ptr;
   rvcn_dec_message_vp9_t msg = {0};

   /* Only 10 bit is supported for Profile 2 */
   if (vp9->bit_depth_chroma_minus8 > 2 || vp9->bit_depth_luma_minus8 > 2)
      return 0;

   msg.frame_header_flags |= (vp9->frame_type
                              << RDECODE_FRAME_HDR_INFO_VP9_FRAME_TYPE_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_FRAME_TYPE_MASK;
   msg.frame_header_flags |= (vp9->pic_flags.error_resilient_mode
                              << RDECODE_FRAME_HDR_INFO_VP9_ERROR_RESILIENT_MODE_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_ERROR_RESILIENT_MODE_MASK;
   msg.frame_header_flags |= (vp9->pic_flags.intra_only
                              << RDECODE_FRAME_HDR_INFO_VP9_INTRA_ONLY_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_INTRA_ONLY_MASK;
   msg.frame_header_flags |= (vp9->pic_flags.allow_high_precision_mv
                              << RDECODE_FRAME_HDR_INFO_VP9_ALLOW_HIGH_PRECISION_MV_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_ALLOW_HIGH_PRECISION_MV_MASK;
   msg.frame_header_flags |= (vp9->pic_flags.refresh_frame_context
                              << RDECODE_FRAME_HDR_INFO_VP9_REFRESH_FRAME_CONTEXT_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_REFRESH_FRAME_CONTEXT_MASK;
   msg.frame_header_flags |= (vp9->pic_flags.frame_parallel_decoding_mode
                              << RDECODE_FRAME_HDR_INFO_VP9_FRAME_PARALLEL_DECODING_MODE_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_FRAME_PARALLEL_DECODING_MODE_MASK;
   msg.frame_header_flags |= (vp9->segmentation.flags.segmentation_enabled
                              << RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_ENABLED_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_ENABLED_MASK;
   msg.frame_header_flags |= (vp9->segmentation.flags.segmentation_update_map
                              << RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_UPDATE_MAP_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_UPDATE_MAP_MASK;
   msg.frame_header_flags |= (vp9->segmentation.flags.segmentation_temporal_update
                              << RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_TEMPORAL_UPDATE_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_TEMPORAL_UPDATE_MASK;
   msg.frame_header_flags |= (vp9->segmentation.flags.segmentation_update_data
                              << RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_UPDATE_DATA_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_SEGMENTATION_UPDATE_DATA_MASK;
   msg.frame_header_flags |= (vp9->loop_filter.loop_filter_flags.mode_ref_delta_enabled
                              << RDECODE_FRAME_HDR_INFO_VP9_MODE_REF_DELTA_ENABLED_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_MODE_REF_DELTA_ENABLED_MASK;
   msg.frame_header_flags |= (vp9->loop_filter.loop_filter_flags.mode_ref_delta_update
                              << RDECODE_FRAME_HDR_INFO_VP9_MODE_REF_DELTA_UPDATE_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_MODE_REF_DELTA_UPDATE_MASK;
   msg.frame_header_flags |= (vp9->pic_flags.use_prev_frame_mvs
                              << RDECODE_FRAME_HDR_INFO_VP9_USE_PREV_IN_FIND_MV_REFS_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_USE_PREV_IN_FIND_MV_REFS_MASK;
   msg.frame_header_flags |= (vp9->pic_flags.use_uncompressed_header
                              << RDECODE_FRAME_HDR_INFO_VP9_USE_UNCOMPRESSED_HEADER_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_USE_UNCOMPRESSED_HEADER_MASK;
   msg.frame_header_flags |= (!!vp9->uncompressed_header_offset
                              << RDECODE_FRAME_HDR_INFO_VP9_USE_FRAME_SIZE_AS_OFFSET_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_VP9_USE_FRAME_SIZE_AS_OFFSET_MASK;

   msg.frame_context_idx = vp9->frame_context_idx;
   msg.reset_frame_context = vp9->reset_frame_context;
   msg.curr_pic_idx = vp9->cur_id;
   msg.interp_filter = vp9->interp_filter;
   msg.filter_level = vp9->loop_filter.loop_filter_level;
   msg.sharpness_level = vp9->loop_filter.loop_filter_sharpness;
   msg.base_qindex = vp9->base_q_idx;
   msg.y_dc_delta_q = vp9->y_dc_delta_q;
   msg.uv_ac_delta_q = vp9->uv_ac_delta_q;
   msg.uv_dc_delta_q = vp9->uv_dc_delta_q;
   msg.log2_tile_cols = vp9->log2_tile_cols;
   msg.log2_tile_rows = vp9->log2_tile_rows;
   msg.chroma_format = 1;
   msg.bit_depth_luma_minus8 = vp9->bit_depth_luma_minus8;
   msg.bit_depth_chroma_minus8 = vp9->bit_depth_chroma_minus8;
   msg.vp9_frame_size = vp9->uncompressed_header_offset;
   msg.uncompressed_header_size = vp9->uncompressed_header_size;
   msg.compressed_header_size = vp9->compressed_header_size;

   if (vp9->bit_depth_luma_minus8 || vp9->bit_depth_chroma_minus8) {
      msg.p010_mode = 1;
      msg.msb_mode = 1;
   }

   const int32_t scale = 1 << (vp9->loop_filter.loop_filter_level >> 5);

   for (uint32_t seg_id = 0; seg_id < VP9_MAX_SEGMENTS; seg_id++) {
      int32_t base_level = vp9->loop_filter.loop_filter_level;

      if (vp9->segmentation.feature_mask[seg_id] & (1 << AC_VIDEO_DEC_VP9_SEG_LEVEL_ALT_LF)) {
         int32_t seg_data = vp9->segmentation.feature_data[seg_id][AC_VIDEO_DEC_VP9_SEG_LEVEL_ALT_LF];
         if (vp9->segmentation.flags.segmentation_abs_delta != VP9_SEG_ABS_DELTA)
            seg_data += base_level;
         base_level = CLAMP(seg_data, 0, VP9_MAX_LOOP_FILTER);
      }

      if (!vp9->loop_filter.loop_filter_flags.mode_ref_delta_enabled) {
         msg.lf_adj_level[seg_id][0][0] = base_level;
      } else {
         const int32_t intra_level = base_level + vp9->loop_filter.loop_filter_ref_deltas[0] * scale;
         msg.lf_adj_level[seg_id][0][0] = CLAMP(intra_level, 0, VP9_MAX_LOOP_FILTER);
      }

      for (uint32_t ref_type = 1; ref_type < VP9_MAX_REF_FRAMES; ref_type++) {
         for (uint32_t mode_type = 0; mode_type < VP9_LOOP_FILTER_ADJUSTMENTS; mode_type++) {
            if (!vp9->loop_filter.loop_filter_flags.mode_ref_delta_enabled) {
               msg.lf_adj_level[seg_id][ref_type][mode_type] = base_level;
            } else {
               const int32_t interLevel = base_level +
                  (vp9->loop_filter.loop_filter_ref_deltas[ref_type] +
                   vp9->loop_filter.loop_filter_mode_deltas[mode_type]) * scale;
               msg.lf_adj_level[seg_id][ref_type][mode_type] = CLAMP(interLevel, 0, VP9_MAX_LOOP_FILTER);
            }
         }
      }
   }

   for (uint32_t i = 0; i < VP9_NUM_REF_FRAMES; i++)
      msg.ref_frame_map[i] = vp9->ref_frame_id_list[i];

   for (uint32_t i = 0; i < VP9_TOTAL_REFS_PER_FRAME; i++) {
      msg.frame_refs[i] = vp9->ref_frames[i];
      msg.ref_frame_sign_bias[i] = vp9->ref_frame_sign_bias[i + 1];
   }

   if (vp9->segmentation.flags.segmentation_enabled) {
      for (uint32_t i = 0; i < VP9_MAX_SEGMENTS; i++) {
         probs->seg.feature_data[i] =
            (vp9->segmentation.feature_data[i][0] & 0xFFFF) |
            ((vp9->segmentation.feature_data[i][1] & 0xFF) << 16) |
            ((vp9->segmentation.feature_data[i][2] & 0xF) << 24) |
            ((vp9->segmentation.feature_data[i][3] & 0xF) << 28);
         probs->seg.feature_mask[i] = vp9->segmentation.feature_mask[i];
      }

      for (uint32_t i = 0; i < VP9_MAX_SEGMENTATION_TREE_PROBS; i++)
         probs->seg.tree_probs[i] = vp9->segmentation.tree_probs[i];

      for (uint32_t i = 0; i < VP9_MAX_SEGMENTATION_PRED_PROBS; i++)
         probs->seg.pred_probs[i] = vp9->segmentation.pred_probs[i];

      probs->seg.abs_delta = vp9->segmentation.flags.segmentation_abs_delta;
   }

   memcpy(codec, &msg, sizeof(msg));
   return sizeof(msg);
}

static uint32_t
build_av1_msg(struct cmd_buffer *cmd_buf, struct ac_video_dec_decode_cmd *cmd, rvcn_dec_message_av1_t *codec)
{
   struct ac_video_dec_av1 *av1 = &cmd->codec_param.av1;
   rvcn_dec_av1_segment_fg_t *seg_fg = cmd_buf->it_probs_ptr;
   rvcn_dec_message_av1_t msg = {0};

   /* Only 4:2:0 is supported for Profile 2 */
   if (!av1->color_config_flags.subsampling_x || !av1->color_config_flags.subsampling_y)
      return 0;

   msg.frame_header_flags |= (av1->pic_flags.show_frame <<
                              RDECODE_FRAME_HDR_INFO_AV1_SHOW_FRAME_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_SHOW_FRAME_MASK;
   msg.frame_header_flags |= (av1->pic_flags.disable_cdf_update <<
                              RDECODE_FRAME_HDR_INFO_AV1_DISABLE_CDF_UPDATE_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_DISABLE_CDF_UPDATE_MASK;
   msg.frame_header_flags |= (!av1->pic_flags.disable_frame_end_update_cdf
                              << RDECODE_FRAME_HDR_INFO_AV1_REFRESH_FRAME_CONTEXT_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_REFRESH_FRAME_CONTEXT_MASK;
   msg.frame_header_flags |= ((av1->frame_type == 2) <<
                              RDECODE_FRAME_HDR_INFO_AV1_INTRA_ONLY_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_INTRA_ONLY_MASK;
   msg.frame_header_flags |= (av1->pic_flags.allow_intrabc <<
                              RDECODE_FRAME_HDR_INFO_AV1_ALLOW_INTRABC_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ALLOW_INTRABC_MASK;
   msg.frame_header_flags |= (av1->pic_flags.allow_high_precision_mv <<
                              RDECODE_FRAME_HDR_INFO_AV1_ALLOW_HIGH_PRECISION_MV_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ALLOW_HIGH_PRECISION_MV_MASK;
   msg.frame_header_flags |= (av1->color_config_flags.mono_chrome <<
                              RDECODE_FRAME_HDR_INFO_AV1_MONOCHROME_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_MONOCHROME_MASK;
   msg.frame_header_flags |= (av1->pic_flags.skip_mode_present <<
                              RDECODE_FRAME_HDR_INFO_AV1_SKIP_MODE_FLAG_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_SKIP_MODE_FLAG_MASK;
   msg.frame_header_flags |= ((av1->quantization.qm_y != 0xff)
                              << RDECODE_FRAME_HDR_INFO_AV1_USING_QMATRIX_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_USING_QMATRIX_MASK;
   msg.frame_header_flags |= (av1->pic_flags.enable_filter_intra <<
                              RDECODE_FRAME_HDR_INFO_AV1_ENABLE_FILTER_INTRA_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ENABLE_FILTER_INTRA_MASK;
   msg.frame_header_flags |= (av1->pic_flags.enable_intra_edge_filter <<
                              RDECODE_FRAME_HDR_INFO_AV1_ENABLE_INTRA_EDGE_FILTER_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ENABLE_INTRA_EDGE_FILTER_MASK;
   msg.frame_header_flags |= (av1->pic_flags.enable_interintra_compound <<
                              RDECODE_FRAME_HDR_INFO_AV1_ENABLE_INTERINTRA_COMPOUND_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ENABLE_INTERINTRA_COMPOUND_MASK;
   msg.frame_header_flags |= (av1->pic_flags.enable_masked_compound <<
                              RDECODE_FRAME_HDR_INFO_AV1_ENABLE_MASKED_COMPOUND_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ENABLE_MASKED_COMPOUND_MASK;
   msg.frame_header_flags |= (av1->pic_flags.allow_warped_motion <<
                              RDECODE_FRAME_HDR_INFO_AV1_ALLOW_WARPED_MOTION_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ALLOW_WARPED_MOTION_MASK;
   msg.frame_header_flags |= (av1->pic_flags.enable_dual_filter <<
                              RDECODE_FRAME_HDR_INFO_AV1_ENABLE_DUAL_FILTER_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ENABLE_DUAL_FILTER_MASK;
   msg.frame_header_flags |= (!!av1->order_hint_bits
                              << RDECODE_FRAME_HDR_INFO_AV1_ENABLE_ORDER_HINT_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ENABLE_ORDER_HINT_MASK;
   msg.frame_header_flags |= (av1->pic_flags.enable_jnt_comp <<
                              RDECODE_FRAME_HDR_INFO_AV1_ENABLE_JNT_COMP_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ENABLE_JNT_COMP_MASK;
   msg.frame_header_flags |= (av1->pic_flags.use_ref_frame_mvs <<
                              RDECODE_FRAME_HDR_INFO_AV1_ALLOW_REF_FRAME_MVS_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ALLOW_REF_FRAME_MVS_MASK;
   msg.frame_header_flags |= (av1->pic_flags.allow_screen_content_tools <<
                              RDECODE_FRAME_HDR_INFO_AV1_ALLOW_SCREEN_CONTENT_TOOLS_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_ALLOW_SCREEN_CONTENT_TOOLS_MASK;
   msg.frame_header_flags |= (av1->pic_flags.force_integer_mv <<
                              RDECODE_FRAME_HDR_INFO_AV1_CUR_FRAME_FORCE_INTEGER_MV_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_CUR_FRAME_FORCE_INTEGER_MV_MASK;
   msg.frame_header_flags |= (av1->loop_filter.loop_filter_flags.mode_ref_delta_enabled
                              << RDECODE_FRAME_HDR_INFO_AV1_MODE_REF_DELTA_ENABLED_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_MODE_REF_DELTA_ENABLED_MASK;
   msg.frame_header_flags |= (av1->loop_filter.loop_filter_flags.mode_ref_delta_update
                              << RDECODE_FRAME_HDR_INFO_AV1_MODE_REF_DELTA_UPDATE_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_MODE_REF_DELTA_UPDATE_MASK;
   msg.frame_header_flags |= (av1->quantization.flags.delta_q_present
                              << RDECODE_FRAME_HDR_INFO_AV1_DELTA_Q_PRESENT_FLAG_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_DELTA_Q_PRESENT_FLAG_MASK;
   msg.frame_header_flags |= (av1->loop_filter.loop_filter_flags.delta_lf_present
                              << RDECODE_FRAME_HDR_INFO_AV1_DELTA_LF_PRESENT_FLAG_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_DELTA_LF_PRESENT_FLAG_MASK;
   msg.frame_header_flags |= (av1->pic_flags.reduced_tx_set
                              << RDECODE_FRAME_HDR_INFO_AV1_REDUCED_TX_SET_USED_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_REDUCED_TX_SET_USED_MASK;
   msg.frame_header_flags |= (av1->segmentation.flags.segmentation_enabled
                              << RDECODE_FRAME_HDR_INFO_AV1_SEGMENTATION_ENABLED_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_SEGMENTATION_ENABLED_MASK;
   msg.frame_header_flags |= (av1->segmentation.flags.segmentation_update_map
                              << RDECODE_FRAME_HDR_INFO_AV1_SEGMENTATION_UPDATE_MAP_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_SEGMENTATION_UPDATE_MAP_MASK;
   msg.frame_header_flags |= (av1->segmentation.flags.segmentation_temporal_update
                              << RDECODE_FRAME_HDR_INFO_AV1_SEGMENTATION_TEMPORAL_UPDATE_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_SEGMENTATION_TEMPORAL_UPDATE_MASK;
   msg.frame_header_flags |= (av1->loop_filter.loop_filter_flags.delta_lf_multi
                              << RDECODE_FRAME_HDR_INFO_AV1_DELTA_LF_MULTI_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_DELTA_LF_MULTI_MASK;
   msg.frame_header_flags |= (av1->pic_flags.is_motion_mode_switchable
                              << RDECODE_FRAME_HDR_INFO_AV1_SWITCHABLE_SKIP_MODE_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_SWITCHABLE_SKIP_MODE_MASK;
   msg.frame_header_flags |= (!av1->pic_flags.ref_frame_update
                              << RDECODE_FRAME_HDR_INFO_AV1_SKIP_REFERENCE_UPDATE_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_SKIP_REFERENCE_UPDATE_MASK;
   msg.frame_header_flags |= (!av1->pic_flags.enable_ref_frame_mvs
                              << RDECODE_FRAME_HDR_INFO_AV1_DISABLE_REF_FRAME_MVS_SHIFT) &
                             RDECODE_FRAME_HDR_INFO_AV1_DISABLE_REF_FRAME_MVS_MASK;

   msg.frame_offset = av1->order_hints;
   msg.profile = av1->seq_profile;
   msg.frame_type = av1->frame_type;
   msg.primary_ref_frame = av1->primary_ref_frame;
   msg.curr_pic_idx = av1->cur_id;
   msg.sb_size = av1->pic_flags.use_128x128_superblock;
   msg.interp_filter = av1->interp_filter;
   msg.filter_level[0] = av1->loop_filter.loop_filter_level[0];
   msg.filter_level[1] = av1->loop_filter.loop_filter_level[1];
   msg.filter_level_u = av1->loop_filter.loop_filter_level[2];
   msg.filter_level_v = av1->loop_filter.loop_filter_level[3];
   msg.sharpness_level = av1->loop_filter.loop_filter_sharpness;
   msg.mode_deltas[0] = av1->loop_filter.loop_filter_mode_deltas[0];
   msg.mode_deltas[1] = av1->loop_filter.loop_filter_mode_deltas[1];
   msg.base_qindex = av1->quantization.base_q_idx;
   msg.y_dc_delta_q = av1->quantization.delta_q_y_dc;
   msg.u_dc_delta_q = av1->quantization.delta_q_u_dc;
   msg.v_dc_delta_q = av1->quantization.delta_q_v_dc;
   msg.u_ac_delta_q = av1->quantization.delta_q_u_ac;
   msg.v_ac_delta_q = av1->quantization.delta_q_v_ac;
   msg.qm_y = av1->quantization.qm_y;
   msg.qm_u = av1->quantization.qm_u;
   msg.qm_v = av1->quantization.qm_v;
   msg.delta_q_res = 1 << av1->quantization.delta_q_res;
   msg.delta_lf_res = 1 << av1->loop_filter.delta_lf_res;
   msg.tile_cols = av1->tile_info.tile_cols;
   msg.tile_rows = av1->tile_info.tile_rows;
   msg.tx_mode = av1->tx_mode;
   msg.reference_mode = av1->pic_flags.reference_select == 1 ? 2 : 0;
   msg.chroma_format = av1->color_config_flags.mono_chrome ? 0 : 1;
   msg.tile_size_bytes = 0xff;
   msg.context_update_tile_id = av1->tile_info.context_update_tile_id;
   msg.max_width = av1->max_width;
   msg.max_height = av1->max_height;
   msg.width = av1->width;
   if (av1->pic_flags.use_superres)
      msg.width = (av1->width * 8 + av1->superres_denom / 2) / av1->superres_denom;
   msg.height = av1->height;
   msg.superres_upscaled_width = av1->width;
   msg.superres_scale_denominator = av1->superres_denom;
   msg.order_hint_bits = av1->order_hint_bits;
   msg.bit_depth_luma_minus8 = av1->bit_depth - 8;
   msg.bit_depth_chroma_minus8 = av1->bit_depth - 8;

   if (av1->bit_depth > 8) {
      msg.p010_mode = 1;
      msg.msb_mode = 1;
   }

   for (uint32_t i = 0; i < AV1_MAX_TILE_COLS + 1; i++)
      msg.tile_col_start_sb[i] = av1->tile_info.tile_col_start_sb[i];

   for (uint32_t i = 0; i < AV1_MAX_TILE_ROWS + 1; i++)
      msg.tile_row_start_sb[i] = av1->tile_info.tile_row_start_sb[i];

   for (uint32_t i = 0; i < AV1_NUM_REF_FRAMES; i++) {
      msg.ref_frame_map[i] = av1->ref_frame_id_list[i];
      msg.ref_deltas[i] = av1->loop_filter.loop_filter_ref_deltas[i];
      msg.global_motion[i].wmtype = av1->global_motion.gm_type[i];
      for (uint32_t j = 0; j < AV1_GLOBAL_MOTION_PARAMS; j++)
         msg.global_motion[i].wmmat[j] = av1->global_motion.gm_params[i][j];
   }

   for (uint32_t i = 0; i < AV1_TOTAL_REFS_PER_FRAME; i++) {
      msg.frame_refs[i] = av1->ref_frames[i].ref_id;
      msg.ref_frame_sign_bias[i] = av1->ref_frames[i].ref_frame_sign_bias;
   }

   for (uint32_t i = 0; i < AV1_MAX_SEGMENTS; i++) {
      int32_t get_qindex = av1->quantization.base_q_idx;
      if (av1->segmentation.feature_mask[i] & 1) {
         int32_t data = av1->segmentation.feature_data[i][0];
         int32_t seg_qindex = av1->quantization.base_q_idx + data;
         get_qindex = seg_qindex < 0 ? 0 : (seg_qindex > 255 ? 255 : seg_qindex);
      }

      int32_t qindex = av1->segmentation.flags.segmentation_enabled ? get_qindex : av1->quantization.base_q_idx;
      msg.seg_lossless_flag |= (((qindex == 0) && (av1->quantization.delta_q_y_dc == 0) &&
         (av1->quantization.delta_q_u_dc == 0) && (av1->quantization.delta_q_v_dc == 0) &&
         (av1->quantization.delta_q_u_ac == 0) && (av1->quantization.delta_q_v_ac == 0)) << i);

      msg.feature_mask[i] = av1->segmentation.feature_mask[i];
      for (uint32_t j = 0; j < AV1_SEG_LVL_MAX; j++) {
         msg.feature_data[i][j] = av1->segmentation.feature_data[i][j];

         if (av1->segmentation.feature_mask[i] & (1 << j)) {
            msg.last_active_segid = i;
            if (j >= 5)
               msg.preskip_segid = 1;
         }
      }
   }

   msg.cdef_damping = av1->cdef.cdef_damping_minus3 + 3;
   msg.cdef_bits = av1->cdef.cdef_bits;
   for (uint32_t i = 0; i < AV1_MAX_CDEF_FILTER_STRENGTHS; i++) {
      msg.cdef_strengths[i] = (av1->cdef.cdef_y_pri_strength[i] << 2) | (av1->cdef.cdef_y_sec_strength[i] & 0x3);
      msg.cdef_uv_strengths[i] = (av1->cdef.cdef_uv_pri_strength[i] << 2) | (av1->cdef.cdef_uv_sec_strength[i] & 0x3);
   }

   for (uint32_t i = 0; i < AV1_MAX_NUM_PLANES; i++) {
      msg.frame_restoration_type[i] = av1->loop_restoration.frame_restoration_type[i];
      msg.log2_restoration_unit_size_minus5[i] = av1->loop_restoration.log2_restoration_size_minus5[i];
   }

   for (uint32_t i = 0; i < AV1_MAX_NUM_TILES; i++) {
      msg.tile_info[i].offset = av1->tile_info.tile_offset[i];
      msg.tile_info[i].size = av1->tile_info.tile_size[i];
   }

   rvcn_dec_film_grain_params_t *fg = &msg.film_grain;
   fg->apply_grain = av1->film_grain.flags.apply_grain;
   if (fg->apply_grain) {
      fg->random_seed = av1->film_grain.grain_seed;
      fg->grain_scale_shift = av1->film_grain.grain_scale_shift;
      fg->scaling_shift = av1->film_grain.grain_scaling_minus8 + 8;
      fg->chroma_scaling_from_luma = av1->film_grain.flags.chroma_scaling_from_luma;
      fg->num_y_points = av1->film_grain.num_y_points;
      fg->num_cb_points = av1->film_grain.num_cb_points;
      fg->num_cr_points = av1->film_grain.num_cr_points;
      fg->cb_mult = av1->film_grain.cb_mult;
      fg->cb_luma_mult = av1->film_grain.cb_luma_mult;
      fg->cb_offset = av1->film_grain.cb_offset;
      fg->cr_mult = av1->film_grain.cr_mult;
      fg->cr_luma_mult = av1->film_grain.cr_luma_mult;
      fg->cr_offset = av1->film_grain.cr_offset;
      fg->bit_depth_minus_8 = av1->bit_depth - 8;

      for (uint32_t i = 0; i < fg->num_y_points; i++) {
         fg->scaling_points_y[i][0] = av1->film_grain.point_y_value[i];
         fg->scaling_points_y[i][1] = av1->film_grain.point_y_scaling[i];
      }

      for (uint32_t i = 0; i < fg->num_cb_points; i++) {
         fg->scaling_points_cb[i][0] = av1->film_grain.point_cb_value[i];
         fg->scaling_points_cb[i][1] = av1->film_grain.point_cb_scaling[i];
      }

      for (uint32_t i = 0; i < fg->num_cr_points; i++) {
         fg->scaling_points_cr[i][0] = av1->film_grain.point_cr_value[i];
         fg->scaling_points_cr[i][1] = av1->film_grain.point_cr_scaling[i];
      }

      fg->ar_coeff_lag = av1->film_grain.ar_coeff_lag;
      fg->ar_coeff_shift = av1->film_grain.ar_coeff_shift_minus6 + 6;

      for (uint32_t i = 0; i < AV1_MAX_NUM_POS_LUMA; i++)
         fg->ar_coeffs_y[i] = av1->film_grain.ar_coeffs_y_plus128[i] - 128;

      for (uint32_t i = 0; i < AV1_MAX_NUM_POS_CHROMA; i++) {
         fg->ar_coeffs_cb[i] = av1->film_grain.ar_coeffs_cb_plus128[i] - 128;
         fg->ar_coeffs_cr[i] = av1->film_grain.ar_coeffs_cr_plus128[i] - 128;
      }

      fg->clip_to_restricted_range = av1->film_grain.flags.clip_to_restricted_range;
      fg->overlap_flag = av1->film_grain.flags.overlap_flag;

      ac_vcn_av1_init_film_grain_buffer(cmd_buf->dec->av1_version, fg, &seg_fg->fg_buf);
   }

   memcpy(seg_fg->seg.feature_data, av1->segmentation.feature_data, sizeof(av1->segmentation.feature_data));
   memcpy(seg_fg->seg.feature_mask, av1->segmentation.feature_mask, sizeof(av1->segmentation.feature_mask));

   memcpy(codec, &msg, sizeof(msg));
   return sizeof(msg);
}

static uint32_t
build_mpeg2_msg(struct cmd_buffer *cmd_buf, struct ac_video_dec_decode_cmd *cmd, rvcn_dec_message_mpeg2_vld_t *codec)
{
   struct ac_video_dec_mpeg2 *mpeg2 = &cmd->codec_param.mpeg2;
   rvcn_dec_message_mpeg2_vld_t msg = {0};

   msg.load_intra_quantiser_matrix = mpeg2->load_intra_quantiser_matrix;
   msg.load_nonintra_quantiser_matrix = mpeg2->load_nonintra_quantiser_matrix;
   memcpy(msg.intra_quantiser_matrix, mpeg2->intra_quantiser_matrix, sizeof(msg.intra_quantiser_matrix));
   memcpy(msg.nonintra_quantiser_matrix, mpeg2->nonintra_quantiser_matrix, sizeof(msg.nonintra_quantiser_matrix));
   msg.chroma_format = 1;
   msg.picture_coding_type = mpeg2->picture_coding_type;
   memcpy(msg.f_code, mpeg2->f_code, sizeof(msg.f_code));
   msg.intra_dc_precision = mpeg2->intra_dc_precision;
   msg.pic_structure = mpeg2->pic_structure;
   msg.top_field_first = mpeg2->top_field_first;
   msg.frame_pred_frame_dct = mpeg2->frame_pred_frame_dct;
   msg.concealment_motion_vectors = mpeg2->concealment_motion_vectors;
   msg.q_scale_type = mpeg2->q_scale_type;
   msg.intra_vlc_format = mpeg2->intra_vlc_format;
   msg.alternate_scan = mpeg2->alternate_scan;

   memcpy(codec, &msg, sizeof(msg));
   return sizeof(msg);
}

static uint32_t
build_vc1_msg(struct cmd_buffer *cmd_buf, struct ac_video_dec_decode_cmd *cmd, rvcn_dec_message_vc1_t *codec)
{
   struct ac_video_dec_vc1 *vc1 = &cmd->codec_param.vc1;
   rvcn_dec_message_vc1_t msg = {0};

   msg.sps_info_flags |= vc1->postprocflag << 7;
   msg.sps_info_flags |= vc1->pulldown << 6;
   msg.sps_info_flags |= vc1->interlace << 5;
   msg.sps_info_flags |= vc1->tfcntrflag << 4;
   msg.sps_info_flags |= vc1->finterpflag << 3;
   msg.sps_info_flags |= vc1->psf << 1;

   msg.pps_info_flags |= vc1->range_mapy_flag << 31;
   msg.pps_info_flags |= vc1->range_mapy << 28;
   msg.pps_info_flags |= vc1->range_mapuv_flag << 27;
   msg.pps_info_flags |= vc1->range_mapuv << 24;
   msg.pps_info_flags |= vc1->multires << 21;
   msg.pps_info_flags |= vc1->maxbframes << 16;
   msg.pps_info_flags |= vc1->overlap << 11;
   msg.pps_info_flags |= vc1->quantizer << 9;
   msg.pps_info_flags |= vc1->panscan_flag << 7;
   msg.pps_info_flags |= vc1->refdist_flag << 6;
   msg.pps_info_flags |= vc1->vstransform << 0;
   msg.pps_info_flags |= vc1->syncmarker << 20;
   msg.pps_info_flags |= vc1->rangered << 19;
   msg.pps_info_flags |= vc1->loopfilter << 5;
   msg.pps_info_flags |= vc1->fastuvmc << 4;
   msg.pps_info_flags |= vc1->extended_mv << 3;
   msg.pps_info_flags |= vc1->extended_dmv << 8;
   msg.pps_info_flags |= vc1->dquant << 1;

   msg.chroma_format = 1;
   msg.profile = vc1->profile;
   msg.level = vc1->level;

   memcpy(codec, &msg, sizeof(msg));
   return sizeof(msg);
}

static int
vcn_build_decode_cmd(struct ac_video_dec *decoder, struct ac_video_dec_decode_cmd *cmd)
{
   struct ac_vcn_decoder *dec = (struct ac_vcn_decoder *)decoder;
   rvcn_dec_message_header_t *header;
   rvcn_dec_message_index_t *index_codec;
   rvcn_dec_message_decode_t *decode;
   rvcn_dec_feedback_header_t *feedback = NULL;
   rvcn_dec_message_index_t *index_drm = NULL;
   rvcn_dec_message_index_t *index_drm_keyblob = NULL;
   rvcn_dec_message_index_t *index_dynamic_dpb = NULL;
   rvcn_dec_message_drm_t *drm = NULL;
   rvcn_dec_message_drm_keyblob_t *drm_keyblob = NULL;
   rvcn_dec_message_dynamic_dpb_t *dynamic_dpb = NULL;
   rvcn_dec_message_dynamic_dpb_t2_t *dynamic_dpb_t2 = NULL;
   uint32_t size = 0;
   uint32_t num_buffers = 0;
   uint32_t decode_flags = 0;
   uint32_t offset_decode = 0;
   uint32_t offset_drm = 0;
   uint32_t offset_drm_keyblob = 0;
   uint32_t offset_dynamic_dpb = 0;
   uint32_t offset_codec = 0;
   uint32_t ref_id_size = 0;
   uint32_t *ref_id_list = NULL;
   uint8_t *emb = cmd->embedded_ptr;
   void *codec;

   header = (rvcn_dec_message_header_t *)emb;
   size += sizeof(rvcn_dec_message_header_t);

   index_codec = (rvcn_dec_message_index_t *)(emb + size);
   size += sizeof(rvcn_dec_message_index_t);

   if (cmd->protected_content.mode) {
      index_drm = (rvcn_dec_message_index_t *)(emb + size);
      size += sizeof(rvcn_dec_message_index_t);
   }

   if (cmd->protected_content.mode == AC_VIDEO_DEC_PROTECTED_CONTENT_CENC) {
      index_drm_keyblob = (rvcn_dec_message_index_t *)(emb + size);
      size += sizeof(rvcn_dec_message_index_t);
   }

   if (cmd->tier == AC_VIDEO_DEC_TIER1 || cmd->tier == AC_VIDEO_DEC_TIER2) {
      index_dynamic_dpb = (rvcn_dec_message_index_t *)(emb + size);
      size += sizeof(rvcn_dec_message_index_t);
   }

   offset_decode = size;
   decode = (rvcn_dec_message_decode_t *)(emb + size);
   size += sizeof(rvcn_dec_message_decode_t);

   if (cmd->protected_content.mode) {
      offset_drm = size;
      drm = (rvcn_dec_message_drm_t *)(emb + size);
      size += sizeof(rvcn_dec_message_drm_t);
   }

   if (cmd->protected_content.mode == AC_VIDEO_DEC_PROTECTED_CONTENT_CENC) {
      offset_drm_keyblob = size;
      drm_keyblob = (rvcn_dec_message_drm_keyblob_t *)(emb + size);
      size += sizeof(rvcn_dec_message_drm_keyblob_t);
   }

   if (cmd->tier == AC_VIDEO_DEC_TIER1 || cmd->tier == AC_VIDEO_DEC_TIER2) {
      offset_dynamic_dpb = size;
      if (cmd->tier == AC_VIDEO_DEC_TIER1) {
         dynamic_dpb = (rvcn_dec_message_dynamic_dpb_t *)(emb + size);
         size += sizeof(rvcn_dec_message_dynamic_dpb_t);
      } else {
         dynamic_dpb_t2 = (rvcn_dec_message_dynamic_dpb_t2_t *)(emb + size);
         size += sizeof(rvcn_dec_message_dynamic_dpb_t2_t);
      }
   }

   offset_codec = size;
   codec = emb + size;

   memset(emb, 0, size);
   header->header_size = sizeof(rvcn_dec_message_header_t);
   header->msg_type = RDECODE_MSG_DECODE;
   header->stream_handle = dec->stream_handle;

   header->index[0].message_id = RDECODE_MESSAGE_DECODE;
   header->index[0].offset = offset_decode;
   header->index[0].size = sizeof(rvcn_dec_message_decode_t);
   num_buffers++;

   index_codec->offset = offset_codec;
   num_buffers++;

   if (cmd->protected_content.mode) {
      index_drm->message_id = RDECODE_MESSAGE_DRM;
      index_drm->offset = offset_drm;
      index_drm->size = sizeof(rvcn_dec_message_drm_t);
      num_buffers++;
   }

   if (cmd->protected_content.mode == AC_VIDEO_DEC_PROTECTED_CONTENT_CENC) {
      index_drm_keyblob->message_id = RDECODE_MESSAGE_DRM_KEYBLOB;
      index_drm_keyblob->offset = offset_drm_keyblob;
      index_drm_keyblob->size = sizeof(rvcn_dec_message_drm_keyblob_t);
      num_buffers++;
   }

   if (cmd->tier == AC_VIDEO_DEC_TIER1 || cmd->tier == AC_VIDEO_DEC_TIER2) {
      index_dynamic_dpb->message_id = RDECODE_MESSAGE_DYNAMIC_DPB;
      index_dynamic_dpb->offset = offset_dynamic_dpb;
      if (cmd->tier == AC_VIDEO_DEC_TIER1)
         index_dynamic_dpb->size = sizeof(rvcn_dec_message_dynamic_dpb_t);
      else
         index_dynamic_dpb->size = sizeof(rvcn_dec_message_dynamic_dpb_t2_t);
      num_buffers++;
   }

   decode->stream_type = stream_type(decoder->param.codec);
   decode->width_in_samples = cmd->width;
   decode->height_in_samples = cmd->height;

   if (decoder->param.codec == AC_VIDEO_CODEC_VC1 && cmd->codec_param.vc1.profile < 2) {
      decode->width_in_samples = DIV_ROUND_UP(cmd->width, 16);
      decode->height_in_samples = DIV_ROUND_UP(cmd->height, 16);
   }

   decode->bsd_size = cmd->bitstream_size;
   decode->dt_size = cmd->decode_surface.planes[0].surf->total_size + cmd->decode_surface.planes[1].surf->total_size;
   decode->hw_ctxt_size = dec->hw_ctx_size;
   decode->sw_ctxt_size = RDECODE_SESSION_CONTEXT_SIZE;

   if (cmd->tier < AC_VIDEO_DEC_TIER2)
      decode->dpb_size = cmd->ref_surfaces[0].size;

   if (cmd->tier == AC_VIDEO_DEC_TIER0) {
      decode->db_pitch = align(cmd->width, dec->dpb_alignment);
      decode->db_pitch_uv = align(cmd->width / 2, dec->dpb_alignment);
      decode->db_aligned_height = align(cmd->height, 64);
   } else if (cmd->num_refs > 0) {
      decode->db_pitch = cmd->ref_surfaces[0].planes[0].surf->u.gfx9.surf_pitch;
      decode->db_pitch_uv = cmd->ref_surfaces[0].planes[1].surf->u.gfx9.surf_pitch;
      decode->db_aligned_height = cmd->ref_surfaces[0].planes[0].surf->u.gfx9.surf_height;
   } else {
      decode->db_pitch = cmd->decode_surface.planes[0].surf->u.gfx9.surf_pitch;
      decode->db_pitch_uv = cmd->decode_surface.planes[1].surf->u.gfx9.surf_pitch;
      decode->db_aligned_height = cmd->decode_surface.planes[0].surf->u.gfx9.surf_height;
   }
   decode->db_array_mode = dec->addr_mode;

   decode->dt_pitch = cmd->decode_surface.planes[0].surf->u.gfx9.surf_pitch;
   decode->dt_uv_pitch = cmd->decode_surface.planes[1].surf->u.gfx9.surf_pitch;
   decode->dt_swizzle_mode = cmd->decode_surface.planes[0].surf->u.gfx9.swizzle_mode;
   decode->dt_array_mode = dec->addr_mode;
   decode->dt_luma_top_offset = cmd->decode_surface.planes[0].surf->tile_swizzle << 8;
   decode->dt_chroma_top_offset = (cmd->decode_surface.planes[1].va - cmd->decode_surface.planes[0].va) |
                                  (cmd->decode_surface.planes[1].surf->tile_swizzle << 8);
   decode->mif_wrc_en = dec->vcn_version >= VCN_3_0_0;

   struct cmd_buffer cmd_buf = {
      .dec = dec,
      .cs = {
         .buf = cmd->cmd_buffer,
         .max_dw = decoder->max_decode_cmd_dw,
      },
      .it_probs_ptr = emb + dec->it_probs_offset,
   };

   if (decoder->ip_type == AMD_IP_VCN_UNIFIED) {
      sq_header(&cmd_buf.cs, &cmd_buf.sq);
      cmd_buf.decode_buffer = add_ib_decode_buffer(&cmd_buf.cs);
   }

   uint32_t codec_size = 0;

   switch (decoder->param.codec) {
   case AC_VIDEO_CODEC_AVC:
      index_codec->message_id = RDECODE_MESSAGE_AVC;
      codec_size = build_avc_msg(&cmd_buf, cmd, codec);
      ref_id_list = cmd->codec_param.avc.ref_frame_id_list;
      ref_id_size = cmd->codec_param.avc.curr_pic_ref_frame_num;
      break;
   case AC_VIDEO_CODEC_HEVC:
      index_codec->message_id = RDECODE_MESSAGE_HEVC;
      codec_size = build_hevc_msg(&cmd_buf, cmd, codec);
      ref_id_list = cmd->codec_param.hevc.ref_pic_id_list;
      ref_id_size = cmd->codec_param.hevc.sps_max_dec_pic_buffering_minus1 + 1;
      break;
   case AC_VIDEO_CODEC_VP9:
      index_codec->message_id = RDECODE_MESSAGE_VP9;
      codec_size = build_vp9_msg(&cmd_buf, cmd, codec);
      ref_id_list = cmd->codec_param.vp9.ref_frame_id_list;
      ref_id_size = VP9_NUM_REF_FRAMES;
      break;
   case AC_VIDEO_CODEC_AV1:
      index_codec->message_id = RDECODE_MESSAGE_AV1;
      codec_size = build_av1_msg(&cmd_buf, cmd, codec);
      ref_id_list = cmd->codec_param.av1.ref_frame_id_list;
      ref_id_size = AV1_NUM_REF_FRAMES;
      break;
   case AC_VIDEO_CODEC_MPEG2:
      index_codec->message_id = RDECODE_MESSAGE_MPEG2_VLD;
      codec_size = build_mpeg2_msg(&cmd_buf, cmd, codec);
      break;
   case AC_VIDEO_CODEC_VC1:
      index_codec->message_id = RDECODE_MESSAGE_VC1;
      codec_size = build_vc1_msg(&cmd_buf, cmd, codec);
      break;
   default:
      break;
   }

   if (codec_size == 0)
      return 1;

   index_codec->size = codec_size;
   header->total_size = size + codec_size;
   header->num_buffers = num_buffers;

   if (cmd->low_latency)
      decode_flags |= RDECODE_FLAGS_LOW_LATENCY_MASK;

   if (cmd->tier == AC_VIDEO_DEC_TIER0) {
      if (dec->vcn_version == VCN_5_0_0)
         decode->db_swizzle_mode = RDECODE_VCN5_256B_D;
   } else if (cmd->tier == AC_VIDEO_DEC_TIER1) {
      decode_flags |= RDECODE_FLAGS_USE_DYNAMIC_DPB_MASK | RDECODE_FLAGS_USE_PAL_MASK;
      if (cmd->dpb_resize)
         decode_flags |= RDECODE_FLAGS_DPB_RESIZE_MASK;

      dynamic_dpb->dpbArraySize = decoder->param.max_num_ref;
      dynamic_dpb->dpbLumaPitch = cmd->ref_surfaces[0].planes[0].surf->u.gfx9.surf_pitch;
      dynamic_dpb->dpbLumaAlignedHeight = cmd->ref_surfaces[0].planes[0].surf->u.gfx9.surf_height;
      dynamic_dpb->dpbLumaAlignedSize = cmd->ref_surfaces[0].planes[0].surf->u.gfx9.surf_slice_size;
      dynamic_dpb->dpbChromaPitch = cmd->ref_surfaces[0].planes[1].surf->u.gfx9.surf_pitch;
      dynamic_dpb->dpbChromaAlignedHeight = cmd->ref_surfaces[0].planes[1].surf->u.gfx9.surf_height;
      dynamic_dpb->dpbChromaAlignedSize = cmd->ref_surfaces[0].planes[1].surf->u.gfx9.surf_slice_size;
      dynamic_dpb->dpbReserved0[0] = dec->dpb_alignment;
   } else if (cmd->tier == AC_VIDEO_DEC_TIER2) {
      struct ac_video_surface *cur = NULL;

      for (uint32_t i = 0; i < cmd->num_refs; i++) {
         bool found = false;

         for (uint32_t j = 0; j < ref_id_size; ++j) {
            if (cmd->ref_id[i] != ref_id_list[j])
               continue;
            dynamic_dpb_t2->dpbAddrLo[j] = cmd->ref_surfaces[i].planes[0].va;
            dynamic_dpb_t2->dpbAddrHi[j] = cmd->ref_surfaces[i].planes[0].va >> 32;
            found = true;
         }

         if (cmd->ref_id[i] == cmd->cur_id)
            cur = &cmd->ref_surfaces[i];
         else if (!found)
            return 1;
      }

      if (!cur)
         cur = &cmd->decode_surface;

      decode_flags |= RDECODE_FLAGS_USE_DYNAMIC_DPB_MASK;
      decode->db_swizzle_mode = cur->planes[0].surf->u.gfx9.swizzle_mode;

      dynamic_dpb_t2->dpbArraySize = ref_id_size;
      dynamic_dpb_t2->dpbCurrLo = cur->planes[0].va;
      dynamic_dpb_t2->dpbCurrHi = cur->planes[0].va >> 32;
      dynamic_dpb_t2->dpbLumaPitch = cur->planes[0].surf->u.gfx9.surf_pitch;
      dynamic_dpb_t2->dpbLumaAlignedHeight = cur->planes[0].surf->u.gfx9.surf_height;
      dynamic_dpb_t2->dpbLumaAlignedSize = cur->planes[0].surf->u.gfx9.surf_slice_size;
      dynamic_dpb_t2->dpbChromaPitch = cur->planes[1].surf->u.gfx9.surf_pitch;
      dynamic_dpb_t2->dpbChromaAlignedHeight = cur->planes[1].surf->u.gfx9.surf_height;
      dynamic_dpb_t2->dpbChromaAlignedSize = cur->planes[1].surf->u.gfx9.surf_slice_size;
   } else if (cmd->tier == AC_VIDEO_DEC_TIER3) {
      uint32_t size =
         sizeof(rvcn_dec_ref_buffers_header_t) + sizeof(rvcn_dec_ref_buffer_t) * cmd->num_refs;
      rvcn_decode_ib_package_t *ib_header =
         (rvcn_decode_ib_package_t *)&(cmd_buf.cs.buf[cmd_buf.cs.cdw]);
      ib_header->package_size = size + sizeof(rvcn_decode_ib_package_t);
      ib_header->package_type = RDECODE_IB_PARAM_DYNAMIC_REFLIST_BUFFER;
      cmd_buf.cs.cdw += 2;

      rvcn_dec_ref_buffers_header_t *refs =
         (rvcn_dec_ref_buffers_header_t *)&(cmd_buf.cs.buf[cmd_buf.cs.cdw]);
      memset(refs, 0, size);
      refs->size = size;
      refs->num_bufs = cmd->num_refs;
      cmd_buf.cs.cdw += size / 4;

      for (uint32_t i = 0; i < cmd->num_refs; i++) {
         rvcn_dec_ref_buffer_t *ref = &refs->pBufs[i];
         ref->index = cmd->ref_id[i];
         ref->y_pitch = cmd->ref_surfaces[i].planes[0].surf->u.gfx9.surf_pitch;
         ref->y_aligned_height = cmd->ref_surfaces[i].planes[0].surf->u.gfx9.surf_height;
         ref->y_aligned_size = cmd->ref_surfaces[i].planes[0].surf->u.gfx9.surf_slice_size;
         ref->y_ref_buffer_address_hi = cmd->ref_surfaces[i].planes[0].va >> 32;
         ref->y_ref_buffer_address_lo = cmd->ref_surfaces[i].planes[0].va;
         ref->uv_pitch = cmd->ref_surfaces[i].planes[1].surf->u.gfx9.surf_pitch;
         ref->uv_aligned_height = cmd->ref_surfaces[i].planes[1].surf->u.gfx9.surf_height;
         ref->uv_aligned_size = cmd->ref_surfaces[i].planes[1].surf->u.gfx9.surf_slice_size;
         ref->uv_ref_buffer_address_hi = cmd->ref_surfaces[i].planes[1].va >> 32;
         ref->uv_ref_buffer_address_lo = cmd->ref_surfaces[i].planes[1].va;
      }
      decode_flags |= RDECODE_FLAGS_UNIFIED_DT_MASK;
      cmd_buf.decode_buffer_flags |= RDECODE_CMDBUF_FLAGS_REF_BUFFER;
   }

   if (dec->feedback_offset) {
      feedback = (rvcn_dec_feedback_header_t *)(emb + dec->feedback_offset);
      memset(feedback, 0, sizeof(*feedback));
      feedback->header_size = sizeof(rvcn_dec_feedback_header_t);
      feedback->total_size = sizeof(rvcn_dec_feedback_header_t);
   }

   if (cmd->protected_content.mode == AC_VIDEO_DEC_PROTECTED_CONTENT_LEGACY) {
      DECRYPT_PARAMETERS *decrypt = cmd->protected_content.key;

      drm->drm_cntl = 1 << DRM_CNTL_BYPASS_SHIFT;
      if (decrypt->u.s.cbc || decrypt->u.s.ctr) {
         uint32_t drm_cmd = 0;

         drm->drm_cntl = 0 << DRM_CNTL_BYPASS_SHIFT;
         drm_cmd |= 0xff << DRM_CMD_BYTE_MASK_SHIFT;
         if (decrypt->u.s.ctr)
            drm_cmd |= 0x00 << DRM_CMD_ALGORITHM_SHIFT;
         else if (decrypt->u.s.cbc)
            drm_cmd |= 0x02 << DRM_CMD_ALGORITHM_SHIFT;
         drm_cmd |= 1 << DRM_CMD_GEN_MASK_SHIFT;
         drm_cmd |= 1 << DRM_CMD_UNWRAP_KEY_SHIFT;
         drm_cmd |= 0 << DRM_CMD_OFFSET_SHIFT;
         drm_cmd |= 1 << DRM_CMD_CNT_DATA_SHIFT;
         drm_cmd |= 1 << DRM_CMD_CNT_KEY_SHIFT;
         drm_cmd |= 1 << DRM_CMD_KEY_SHIFT;
         drm_cmd |= decrypt->u.s.drm_id << DRM_CMD_SESSION_SEL_SHIFT;
         drm->drm_cmd = drm_cmd;
         memcpy(drm->drm_wrapped_key, decrypt->encrypted_key, 16);
         memcpy(drm->drm_key, decrypt->session_iv, 16);
         memcpy(drm->drm_counter, decrypt->encrypted_iv, 16);
      }
   } else if (cmd->protected_content.mode == AC_VIDEO_DEC_PROTECTED_CONTENT_CENC) {
      amd_secure_buffer_format *secure_buf = cmd->protected_content.key;
      uint32_t drm_cmd = 0;

      memcpy(drm->drm_wrapped_key, secure_buf->key_blob.wrapped_key, 16);
      memcpy(drm->drm_key, secure_buf->key_blob.wrapped_key_iv, 16);
      memcpy(drm->drm_counter, secure_buf->desc.iv, 16);
      drm->drm_subsample_size = secure_buf->desc.subsamples_length;
      drm->drm_cntl |= 0x3 << DRM_CNTL_CENC_ENABLE_SHIFT;
      drm_cmd |= 1 << DRM_CMD_KEY_SHIFT;
      drm_cmd |= 1 << DRM_CMD_CNT_KEY_SHIFT;
      drm_cmd |= 1 << DRM_CMD_CNT_DATA_SHIFT;
      drm_cmd |= 1 << DRM_CMD_OFFSET_SHIFT;
      drm_cmd |= 1 << DRM_CMD_GEN_MASK_SHIFT;
      drm_cmd |= 0xFF << DRM_CMD_BYTE_MASK_SHIFT;
      drm_cmd |= secure_buf->key_blob.u.s.drm_session_id << DRM_CMD_SESSION_SEL_SHIFT;
      drm_cmd |= 1 << DRM_CMD_UNWRAP_KEY_SHIFT;
      drm->drm_cmd = drm_cmd;

      memcpy(drm_keyblob->contentKey, secure_buf->key_blob.local_policy.wrapped_key, 16);
      memcpy(drm_keyblob->signature, secure_buf->key_blob.local_policy.signature, 16);
      memcpy(&drm_keyblob->policyIndex, secure_buf->key_blob.local_policy.native_policy.enabled_policy_index, 4);
      memcpy(drm_keyblob->policyArray, secure_buf->key_blob.local_policy.native_policy.policy_array, 32);

      int ss_length = MIN2(secure_buf->desc.subsamples_length, MAX_SUBSAMPLES);
      int total_ss_size = 0;
      uint32_t *ss_ptr = (uint32_t *)(emb + dec->subsample_offset);
      for (int i = 0; i < ss_length; i++) {
         memcpy(&ss_ptr[i * 2], &secure_buf->desc.subsamples[i].num_bytes_clear, 8);
         total_ss_size += ss_ptr[i * 2] + ss_ptr[i * 2 + 1];
      }
      assert(total_ss_size <= cmd->bitstream_size);
      if (ss_ptr[ss_length * 2 - 1] != 0)
         ss_ptr[ss_length * 2 - 1] += (cmd->bitstream_size - total_ss_size);
      else
         ss_ptr[ss_length * 2 - 2] += (cmd->bitstream_size - total_ss_size);
   }

   send_cmd(&cmd_buf, RDECODE_CMD_SESSION_CONTEXT_BUFFER, cmd->session_va);
   send_cmd(&cmd_buf, RDECODE_CMD_MSG_BUFFER, cmd->embedded_va);
   if (cmd->tier < AC_VIDEO_DEC_TIER2)
      send_cmd(&cmd_buf, RDECODE_CMD_DPB_BUFFER, cmd->ref_surfaces[0].planes[0].va);
   if (dec->hw_ctx_size) {
      uint64_t session_va = cmd->session_tmz_va ? cmd->session_tmz_va : cmd->session_va;
      send_cmd(&cmd_buf, RDECODE_CMD_CONTEXT_BUFFER, session_va + RDECODE_SESSION_CONTEXT_SIZE);
   }
   if (decoder->param.codec == AC_VIDEO_CODEC_AVC || decoder->param.codec == AC_VIDEO_CODEC_HEVC)
      send_cmd(&cmd_buf, RDECODE_CMD_IT_SCALING_TABLE_BUFFER, cmd->embedded_va + dec->it_probs_offset);
   if (decoder->param.codec == AC_VIDEO_CODEC_VP9 || decoder->param.codec == AC_VIDEO_CODEC_AV1)
      send_cmd(&cmd_buf, RDECODE_CMD_PROB_TBL_BUFFER, cmd->embedded_va + dec->it_probs_offset);
   if (cmd->protected_content.mode == AC_VIDEO_DEC_PROTECTED_CONTENT_CENC)
      send_cmd(&cmd_buf, RDECODE_CMD_SUBSAMPLE, cmd->embedded_va + dec->subsample_offset);
   if (feedback)
      send_cmd(&cmd_buf, RDECODE_CMD_FEEDBACK_BUFFER, cmd->embedded_va + dec->feedback_offset);
   send_cmd(&cmd_buf, RDECODE_CMD_BITSTREAM_BUFFER, cmd->bitstream_va);
   send_cmd(&cmd_buf, RDECODE_CMD_DECODING_TARGET_BUFFER, cmd->decode_surface.planes[0].va);

   decode->decode_flags = decode_flags;
   decode->decode_buffer_flags = cmd_buf.decode_buffer_flags & ~RDECODE_CMDBUF_FLAGS_SESSION_CONTEXT_BUFFER;

   if (decoder->ip_type == AMD_IP_VCN_UNIFIED) {
      cmd_buf.decode_buffer->valid_buf_flag = cmd_buf.decode_buffer_flags;
      sq_tail(&cmd_buf.cs, &cmd_buf.sq);
   } else {
      ac_cmdbuf_begin(&cmd_buf.cs);
      ac_cmdbuf_emit(RDECODE_PKT0(dec->reg.cntl >> 2, 0));
      ac_cmdbuf_emit(1);
      ac_cmdbuf_end();
   }

   cmd->out.cmd_dw = cmd_buf.cs.cdw;
   return 0;
}

static void
vcn_dec_destroy(struct ac_video_dec *decoder)
{
   struct ac_vcn_decoder *dec = (struct ac_vcn_decoder *)decoder;

   FREE(dec);
}

uint32_t
ac_vcn_dec_dpb_size(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   unsigned dpb_size;
   unsigned width = align(param->max_width, 16);
   unsigned height = align(param->max_height, 16);
   unsigned max_references = param->max_num_ref;
   unsigned dpb_alignment = ac_vcn_dec_dpb_alignment(info, param);

   /* aligned size of a single frame */
   unsigned image_size = align(width, dpb_alignment) * align(height, dpb_alignment);
   image_size += image_size / 2;
   image_size = align(image_size, 1024);

   /* picture width & height in 16 pixel units */
   unsigned width_in_mb = width / 16;
   unsigned height_in_mb = align(height / 16, 2);

   switch (param->codec) {
   case AC_VIDEO_CODEC_AVC:
      return image_size * max_references;

   case AC_VIDEO_CODEC_HEVC:
      if (param->max_bit_depth == 10) {
         return align((align(width, dpb_alignment) *
                align(height, dpb_alignment) * 9) / 4, 256) * max_references;
      }
      return align((align(width, dpb_alignment) *
             align(height, dpb_alignment) * 3) / 2, 256) * max_references;

   case AC_VIDEO_CODEC_VP9:
      dpb_size = info->vcn_ip_version >= VCN_2_0_0 ? 8192 * 4320 : 4096 * 3000;
      dpb_size = dpb_size * 3 / 2 * max_references;
      if (param->max_bit_depth == 10)
         dpb_size = dpb_size * 3 / 2;
      return dpb_size;

   case AC_VIDEO_CODEC_AV1:
      return 8192 * 4320 * 3 / 2 * max_references * 3 / 2;

   case AC_VIDEO_CODEC_MPEG2:
      return image_size * max_references;

   case AC_VIDEO_CODEC_VC1:
      dpb_size = image_size * max_references;
      /* CONTEXT_BUFFER */
      dpb_size += width_in_mb * height_in_mb * 128;
      /* IT surface buffer */
      dpb_size += width_in_mb * 64;
      /* DB surface buffer */
      dpb_size += width_in_mb * 128;
      /* BP */
      return dpb_size + align(MAX2(width_in_mb, height_in_mb) * 7 * 16, 64);

   default:
      return 0;
   }
}

uint32_t
ac_vcn_dec_dpb_alignment(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   if (info->vcn_ip_version < VCN_2_0_0 || param->max_width <= 32)
      return 32;

   if (info->vcn_ip_version >= VCN_5_0_0)
      return 64;

   switch (param->codec) {
   case AC_VIDEO_CODEC_HEVC:
      return param->max_bit_depth == 10 ? 64 : 32;
   case AC_VIDEO_CODEC_VP9:
   case AC_VIDEO_CODEC_AV1:
      return 64;
   default:
      return 32;
   }
}

struct ac_video_dec *
ac_vcn_create_video_decoder(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   struct ac_vcn_decoder *dec = CALLOC_STRUCT(ac_vcn_decoder);
   if (!dec)
      return NULL;

   dec->vcn_version = info->vcn_ip_version;
   dec->stream_handle = get_stream_handle();

   switch (dec->vcn_version) {
   case VCN_1_0_0:
   case VCN_1_0_1:
      dec->reg.data0 = RDECODE_VCN1_GPCOM_VCPU_DATA0;
      dec->reg.data1 = RDECODE_VCN1_GPCOM_VCPU_DATA1;
      dec->reg.cmd = RDECODE_VCN1_GPCOM_VCPU_CMD;
      dec->reg.cntl = RDECODE_VCN1_ENGINE_CNTL;
      break;
   case VCN_2_0_0:
   case VCN_2_0_2:
   case VCN_2_0_3:
   case VCN_2_2_0:
      dec->reg.data0 = RDECODE_VCN2_GPCOM_VCPU_DATA0;
      dec->reg.data1 = RDECODE_VCN2_GPCOM_VCPU_DATA1;
      dec->reg.cmd = RDECODE_VCN2_GPCOM_VCPU_CMD;
      dec->reg.cntl = RDECODE_VCN2_ENGINE_CNTL;
      break;
   case VCN_2_5_0:
   case VCN_2_6_0:
   case VCN_3_0_0:
   case VCN_3_0_2:
   case VCN_3_0_16:
   case VCN_3_0_33:
   case VCN_3_1_1:
   case VCN_3_1_2:
      dec->reg.data0 = RDECODE_VCN2_5_GPCOM_VCPU_DATA0;
      dec->reg.data1 = RDECODE_VCN2_5_GPCOM_VCPU_DATA1;
      dec->reg.cmd = RDECODE_VCN2_5_GPCOM_VCPU_CMD;
      dec->reg.cntl = RDECODE_VCN2_5_ENGINE_CNTL;
      break;
   case VCN_4_0_3:
      dec->addr_mode = RDECODE_ARRAY_MODE_ADDRLIB_SEL_GFX9;
      dec->av1_version = RDECODE_AV1_VER_1;
      break;
   case VCN_4_0_0:
   case VCN_4_0_2:
   case VCN_4_0_4:
   case VCN_4_0_5:
   case VCN_4_0_6:
      dec->addr_mode = RDECODE_ARRAY_MODE_ADDRLIB_SEL_GFX11;
      dec->av1_version = RDECODE_AV1_VER_1;
      break;
   case VCN_5_0_0:
      dec->addr_mode = RDECODE_ARRAY_MODE_ADDRLIB_SEL_GFX11;
      dec->av1_version = RDECODE_AV1_VER_2;
      break;
   case VCN_5_0_1:
      dec->addr_mode = RDECODE_ARRAY_MODE_ADDRLIB_SEL_GFX9;
      dec->av1_version = RDECODE_AV1_VER_2;
      break;
   case VCN_5_3_0:
      dec->addr_mode = RDECODE_ARRAY_MODE_ADDRLIB_SEL_GFX11;
      dec->av1_version = RDECODE_AV1_VER_2;
      break;
   default:
      assert(!"unsupported vcn version");
   }

   if (dec->vcn_version >= VCN_4_0_0)
      dec->base.ip_type = AMD_IP_VCN_UNIFIED;
   else
      dec->base.ip_type = AMD_IP_VCN_DEC;

   if (param->codec == AC_VIDEO_CODEC_VP9 && dec->vcn_version <= VCN_2_6_0)
      dec->base.tiers |= AC_VIDEO_DEC_TIER1;

   if (param->codec == AC_VIDEO_CODEC_AVC || param->codec == AC_VIDEO_CODEC_HEVC ||
       param->codec == AC_VIDEO_CODEC_VP9 || param->codec == AC_VIDEO_CODEC_AV1) {
      if (dec->vcn_version >= VCN_3_0_0)
         dec->base.tiers |= AC_VIDEO_DEC_TIER2;
      if (dec->vcn_version >= VCN_5_0_0 && param->max_bit_depth != 12)
         dec->base.tiers |= AC_VIDEO_DEC_TIER3;
   }

   dec->dpb_alignment = ac_vcn_dec_dpb_alignment(info, param);

   dec->base.param = *param;
   dec->base.max_create_cmd_dw = 64;
   dec->base.max_decode_cmd_dw = 512;
   dec->base.session_size = get_session_size(dec);
   if (dec->vcn_version < VCN_2_2_0)
      dec->base.session_tmz_size = dec->base.session_size;
   dec->base.embedded_size = get_embedded_size(dec);

   dec->base.destroy = vcn_dec_destroy;
   if (param->codec == AC_VIDEO_CODEC_VP9 || param->codec == AC_VIDEO_CODEC_AV1)
      dec->base.init_session_buf = vcn_init_session_buf;
   dec->base.build_create_cmd = vcn_build_create_cmd;
   dec->base.build_decode_cmd = vcn_build_decode_cmd;

   return &dec->base;
}

#define RDECODE_JPEG_VER_1 0
#define RDECODE_JPEG_VER_2 1
#define RDECODE_JPEG_VER_3 2

#define set_reg(reg, cond, type, val) do { \
   ac_cmdbuf_emit(RDECODE_PKTJ((reg), (cond), (type))); \
   ac_cmdbuf_emit(val); \
} while (0);

struct ac_vcn_jpeg_decoder {
   struct ac_video_dec base;

   enum amd_gfx_level gfx_level;
   enum vcn_version vcn_version;
   uint32_t jpeg_version;
   struct ac_video_dec_session_param session_param;

   struct {
      uint32_t jpeg_dec_soft_rst;
      uint32_t jrbc_ib_cond_rd_timer;
      uint32_t jrbc_ib_ref_data;
      uint32_t lmi_jpeg_read_64bit_bar_high;
      uint32_t lmi_jpeg_read_64bit_bar_low;
      uint32_t jpeg_rb_base;
      uint32_t jpeg_rb_size;
      uint32_t jpeg_rb_wptr;
      uint32_t jpeg_pitch;
      uint32_t jpeg_uv_pitch;
      uint32_t dec_addr_mode;
      uint32_t dec_y_tiling_surface;
      uint32_t dec_uv_tiling_surface;
      uint32_t lmi_jpeg_write_64bit_bar_high;
      uint32_t lmi_jpeg_write_64bit_bar_low;
      uint32_t jpeg_tier_cntl2;
      uint32_t jpeg_outbuf_rptr;
      uint32_t jpeg_outbuf_cntl;
      uint32_t jpeg_int_en;
      uint32_t jpeg_cntl;
      uint32_t jpeg_rb_rptr;
      uint32_t jpeg_outbuf_wptr;
      uint32_t jpeg_luma_base0_0;
      uint32_t jpeg_chroma_base0_0;
      uint32_t jpeg_chromav_base0_0;
      uint32_t jpeg_index;
      uint32_t jpeg_data;
   } reg;
};

static int
vcn_jpeg_build_decode_cmd(struct ac_video_dec *decoder, struct ac_video_dec_decode_cmd *cmd)
{
   struct ac_vcn_jpeg_decoder *dec = (struct ac_vcn_jpeg_decoder *)decoder;
   uint32_t dt_top_offset[3] = {0, 0, 0};
   uint32_t dt_pitch[3] = {0, 0, 0};
   uint32_t dt_addr_mode = 0;
   uint32_t fc_sps_info = 0;
   uint32_t crop_x = ROUND_DOWN_TO(cmd->codec_param.mjpeg.crop_x, 16);
   uint32_t crop_y = ROUND_DOWN_TO(cmd->codec_param.mjpeg.crop_y, 16);
   uint32_t crop_width = align(cmd->codec_param.mjpeg.crop_width, 16);
   uint32_t crop_height = align(cmd->codec_param.mjpeg.crop_height, 16);

   for (uint32_t i = 0; i < cmd->decode_surface.num_planes; i++) {
      struct radeon_surf *surf = cmd->decode_surface.planes[i].surf;
      dt_top_offset[i] = surf->u.gfx9.surf_offset | (surf->tile_swizzle << 8);
      dt_pitch[i] = surf->u.gfx9.surf_pitch * (i == 0 ? surf->blk_w : surf->bpe);
   }

   switch (cmd->decode_surface.format) {
   case PIPE_FORMAT_R8_G8_B8_UNORM:
      fc_sps_info = 1 | (1 << 5) | (0xff << 8);
      break;
   case PIPE_FORMAT_R8G8B8A8_UNORM:
      fc_sps_info = 1 | (1 << 4) | (0xff << 8);
      break;
   case PIPE_FORMAT_A8R8G8B8_UNORM:
      fc_sps_info = 1 | (1 << 4) | (1 << 5) | (0xff << 8);
      break;
   default:
      break;
   }

   if (dec->gfx_level >= GFX12) {
      switch (cmd->decode_surface.planes[0].surf->u.gfx9.swizzle_mode) {
      case ADDR3_256B_2D:
      case ADDR3_4KB_2D:
      case ADDR3_64KB_2D:
      case ADDR3_256KB_2D:
         dt_addr_mode = RDECODE_TILE_8X8;
         break;
      case ADDR3_LINEAR:
      default:
         dt_addr_mode = RDECODE_TILE_LINEAR;
         break;
      }
   } else {
      switch (cmd->decode_surface.planes[0].surf->u.gfx9.swizzle_mode) {
      case ADDR_SW_256B_D:
      case ADDR_SW_4KB_D:
      case ADDR_SW_64KB_D:
      case ADDR_SW_4KB_D_X:
      case ADDR_SW_64KB_D_X:
      case ADDR_SW_256KB_S_X:
      case ADDR_SW_256KB_D_X:
      case ADDR_SW_256KB_R_X:
         dt_addr_mode = RDECODE_TILE_8X8;
         break;
      case ADDR_SW_256B_S:
      case ADDR_SW_4KB_S:
      case ADDR_SW_64KB_S:
      case ADDR_SW_4KB_S_X:
      case ADDR_SW_64KB_S_X:
      case ADDR_SW_64KB_R_X:
         dt_addr_mode = RDECODE_TILE_32AS8;
         break;
      case ADDR_SW_LINEAR:
      default:
         dt_addr_mode = RDECODE_TILE_LINEAR;
         break;
      }
   }

   struct ac_cmdbuf cs = {
      .buf = cmd->cmd_buffer,
      .max_dw = decoder->max_decode_cmd_dw,
   };

   ac_cmdbuf_begin(&cs);

   if (dec->jpeg_version == RDECODE_JPEG_VER_1) {
      /* jpeg soft reset */
      set_reg(dec->reg.jpeg_cntl, COND0, TYPE0, 1);

      /* ensuring the Reset is asserted in SCLK domain */
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C2);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0x01400200);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 1 << 9);
      set_reg(SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3, 1 << 9);

      /* wait mem */
      set_reg(dec->reg.jpeg_cntl, COND0, TYPE0, 0);

      /* ensuring the Reset is de-asserted in SCLK domain */
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0 << 9);
      set_reg(SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3, 1 << 9);
   } else {
      /* jpeg soft reset */
      set_reg(dec->reg.jpeg_dec_soft_rst, COND0, TYPE0, 1);

      /* ensuring the Reset is asserted in SCLK domain */
      set_reg(dec->reg.jrbc_ib_cond_rd_timer, COND0, TYPE0, 0x01400200);
      set_reg(dec->reg.jrbc_ib_ref_data, COND0, TYPE0, (0x1 << 0x10));
      set_reg(dec->reg.jpeg_dec_soft_rst, COND3, TYPE3, (0x1 << 0x10));

      /* wait mem */
      set_reg(dec->reg.jpeg_dec_soft_rst, COND0, TYPE0, 0);

      /* ensuring the Reset is de-asserted in SCLK domain */
      set_reg(dec->reg.jrbc_ib_ref_data, COND0, TYPE0, (0 << 0x10));
      set_reg(dec->reg.jpeg_dec_soft_rst, COND3, TYPE3, (0x1 << 0x10));
   }

   /* set UVD_LMI_JPEG_READ_64BIT_BAR_LOW/HIGH based on bitstream buffer address */
   set_reg(dec->reg.lmi_jpeg_read_64bit_bar_high, COND0, TYPE0, cmd->bitstream_va >> 32);
   set_reg(dec->reg.lmi_jpeg_read_64bit_bar_low, COND0, TYPE0, cmd->bitstream_va);

   /* set jpeg_rb_base */
   set_reg(dec->reg.jpeg_rb_base, COND0, TYPE0, 0);

   /* set jpeg_rb_base */
   set_reg(dec->reg.jpeg_rb_size, COND0, TYPE0, 0xfffffff0);

   /* set jpeg_rb_wptr */
   set_reg(dec->reg.jpeg_rb_wptr, COND0, TYPE0, cmd->bitstream_size >> 2);

   if (dec->jpeg_version == RDECODE_JPEG_VER_3 && fc_sps_info) {
      set_reg(dec->reg.jpeg_pitch, COND0, TYPE0, dt_pitch[0]);
      set_reg(dec->reg.jpeg_uv_pitch, COND0, TYPE0, dt_pitch[1]);
   } else {
      set_reg(dec->reg.jpeg_pitch, COND0, TYPE0, dt_pitch[0] >> 4);
      set_reg(dec->reg.jpeg_uv_pitch, COND0, TYPE0, dt_pitch[1] >> 4);
   }

   if (dec->jpeg_version == RDECODE_JPEG_VER_1) {
      uint32_t tiling = dt_addr_mode | (cmd->decode_surface.planes[0].surf->u.gfx9.swizzle_mode << 3);
      set_reg(dec->reg.dec_y_tiling_surface, COND0, TYPE0, tiling);
      set_reg(dec->reg.dec_uv_tiling_surface, COND0, TYPE0, tiling);
   } else {
      uint32_t tiling = cmd->decode_surface.planes[0].surf->u.gfx9.swizzle_mode;
      set_reg(dec->reg.dec_addr_mode, COND0, TYPE0, dt_addr_mode | (dt_addr_mode << 2));
      set_reg(dec->reg.dec_y_tiling_surface, COND0, TYPE0, tiling);
      set_reg(dec->reg.dec_uv_tiling_surface, COND0, TYPE0, tiling);
   }

   /* set UVD_LMI_JPEG_WRITE_64BIT_BAR_LOW/HIGH based on target buffer address */
   set_reg(dec->reg.lmi_jpeg_write_64bit_bar_high, COND0, TYPE0, cmd->decode_surface.planes[0].va >> 32);
   set_reg(dec->reg.lmi_jpeg_write_64bit_bar_low, COND0, TYPE0, cmd->decode_surface.planes[0].va);

   /* set output buffer data address */
   if (dec->jpeg_version <= RDECODE_JPEG_VER_2) {
      set_reg(dec->reg.jpeg_index, COND0, TYPE0, 0);
      set_reg(dec->reg.jpeg_data, COND0, TYPE0, dt_top_offset[0]);
      set_reg(dec->reg.jpeg_index, COND0, TYPE0, 1);
      set_reg(dec->reg.jpeg_data, COND0, TYPE0, dt_top_offset[1]);
      if (dec->jpeg_version == RDECODE_JPEG_VER_2) {
         set_reg(dec->reg.jpeg_index, COND0, TYPE0, 2);
         set_reg(dec->reg.jpeg_data, COND0, TYPE0, dt_top_offset[2]);
         set_reg(dec->reg.jpeg_tier_cntl2, COND0, 0, 0);
      } else {
         set_reg(dec->reg.jpeg_tier_cntl2, COND0, TYPE3, 0);
      }
   } else {
      set_reg(dec->reg.jpeg_luma_base0_0, COND0, TYPE0, dt_top_offset[0]);
      set_reg(dec->reg.jpeg_chroma_base0_0, COND0, TYPE0, dt_top_offset[1]);
      set_reg(dec->reg.jpeg_chromav_base0_0, COND0, TYPE0, dt_top_offset[2]);
      if (crop_width && crop_height) {
         set_reg(vcnipUVD_JPEG_ROI_CROP_POS_START, COND0, TYPE0, (crop_y << 16) | crop_x);
         set_reg(vcnipUVD_JPEG_ROI_CROP_POS_STRIDE, COND0, TYPE0, (crop_height << 16) | crop_width);
      } else {
         set_reg(vcnipUVD_JPEG_ROI_CROP_POS_START, COND0, TYPE0, (0 << 16) | 0);
         set_reg(vcnipUVD_JPEG_ROI_CROP_POS_STRIDE, COND0, TYPE0, (1 << 16) | 1);
      }
      if (fc_sps_info) {
         /* set fc timeout control */
         set_reg(vcnipUVD_JPEG_FC_TMEOUT_CNT, COND0, TYPE0, 4244373504);
         /* set alpha position and packed format */
         set_reg(vcnipUVD_JPEG_FC_SPS_INFO, COND0, TYPE0, fc_sps_info);
         /* coefs */
         set_reg(vcnipUVD_JPEG_FC_R_COEF, COND0, TYPE0, 256 | (0 << 10) | (403 << 20));
         set_reg(vcnipUVD_JPEG_FC_G_COEF, COND0, TYPE0, 256 | (976 << 10) | (904 << 20));
         set_reg(vcnipUVD_JPEG_FC_B_COEF, COND0, TYPE0, 256 | (475 << 10) | (0 << 20));
         set_reg(vcnipUVD_JPEG_FC_VUP_COEF_CNTL0, COND0, TYPE0, 128 | (384 << 16));
         set_reg(vcnipUVD_JPEG_FC_VUP_COEF_CNTL1, COND0, TYPE0, 384 | (128 << 16));
         set_reg(vcnipUVD_JPEG_FC_VUP_COEF_CNTL2, COND0, TYPE0, 128 | (384 << 16));
         set_reg(vcnipUVD_JPEG_FC_VUP_COEF_CNTL3, COND0, TYPE0, 384 | (128 << 16));
         set_reg(vcnipUVD_JPEG_FC_HUP_COEF_CNTL0, COND0, TYPE0, 128 | (384 << 16));
         set_reg(vcnipUVD_JPEG_FC_HUP_COEF_CNTL1, COND0, TYPE0, 384 | (128 << 16));
         set_reg(vcnipUVD_JPEG_FC_HUP_COEF_CNTL2, COND0, TYPE0, 128 | (384 << 16));
         set_reg(vcnipUVD_JPEG_FC_HUP_COEF_CNTL3, COND0, TYPE0, 384 | (128 << 16));
      } else {
         set_reg(vcnipUVD_JPEG_FC_SPS_INFO, COND0, TYPE0, 1 | (1 << 5) | (255 << 8));
      }
      set_reg(dec->reg.jpeg_tier_cntl2, COND0, 0, 0);
   }

   /* set output buffer read pointer */
   set_reg(dec->reg.jpeg_outbuf_rptr, COND0, TYPE0, 0);
   if (dec->jpeg_version > RDECODE_JPEG_VER_1)
      set_reg(dec->reg.jpeg_outbuf_cntl, COND0, TYPE0, (0x00001587 & ~0x00000180) | (0x1 << 0x7) | (0x1 << 0x6));

   /* enable error interrupts */
   set_reg(dec->reg.jpeg_int_en, COND0, TYPE0, 0xfffffffe);

   /* start engine command */
   uint32_t val = 0x6;
   if (dec->jpeg_version == RDECODE_JPEG_VER_3) {
      if (crop_width && crop_height)
         val = val | (0x1 << 24);
      if (fc_sps_info)
         val = val | (1 << 16) | (1 << 18);
   }
   set_reg(dec->reg.jpeg_cntl, COND0, TYPE0, val);

   if (dec->jpeg_version == RDECODE_JPEG_VER_1) {
      /* wait for job completion, wait for job JBSI fetch done */
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, cmd->bitstream_size >> 2);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C2);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0x01400200);
      set_reg(dec->reg.jpeg_rb_rptr, COND0, TYPE3, 0xFFFFFFFF);

      /* wait for job jpeg outbuf idle */
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0xFFFFFFFF);
      set_reg(dec->reg.jpeg_outbuf_wptr, COND0, TYPE3, 0x00000001);

      /* stop engine */
      set_reg(dec->reg.jpeg_cntl, COND0, TYPE0, 0x4);

      /* asserting jpeg lmi drop */
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x0005);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (1 << 23 | 1 << 0));
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE1, 0);

      /* asserting jpeg reset */
      set_reg(dec->reg.jpeg_cntl, COND0, TYPE0, 1);

      /* ensure reset is asserted in sclk domain */
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (1 << 9));
      set_reg(SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3, (1 << 9));

      /* de-assert jpeg reset */
      set_reg(dec->reg.jpeg_cntl, COND0, TYPE0, 0);

      /* ensure reset is de-asserted in sclk domain */
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (0 << 9));
      set_reg(SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3, (1 << 9));

      /* de-asserting jpeg lmi drop */
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x0005);
      set_reg(SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0);
   } else {
      /* wait for job completion, wait for job JBSI fetch done */
      set_reg(dec->reg.jrbc_ib_ref_data, COND0, TYPE0, cmd->bitstream_size >> 2);
      set_reg(dec->reg.jrbc_ib_cond_rd_timer, COND0, TYPE0, 0x01400200);
      set_reg(dec->reg.jpeg_rb_rptr, COND3, TYPE3, 0xffffffff);

      /* wait for job jpeg outbuf idle */
      set_reg(dec->reg.jrbc_ib_ref_data, COND0, TYPE0, 0xffffffff);
      set_reg(dec->reg.jpeg_outbuf_wptr, COND3, TYPE3, 0x00000001);

      if (dec->jpeg_version == RDECODE_JPEG_VER_3 && fc_sps_info) {
         val = val | (0x7 << 16);
         set_reg(dec->reg.jrbc_ib_ref_data, COND0, TYPE0, 0);
         set_reg(vcnipUVD_JPEG_INT_STAT, COND3, TYPE3, val);
      }

      /* stop engine */
      set_reg(dec->reg.jpeg_cntl, COND0, TYPE0, 0x4);
   }

   ac_cmdbuf_end();

   cmd->out.cmd_dw = cs.cdw;
   return 0;
}

static void
vcn_jpeg_dec_destroy(struct ac_video_dec *decoder)
{
   struct ac_vcn_jpeg_decoder *dec = (struct ac_vcn_jpeg_decoder *)decoder;

   FREE(dec);
}

struct ac_video_dec *
ac_vcn_create_jpeg_decoder(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   struct ac_vcn_jpeg_decoder *dec = CALLOC_STRUCT(ac_vcn_jpeg_decoder);
   if (!dec)
      return NULL;

   dec->gfx_level = info->gfx_level;
   dec->vcn_version = info->vcn_ip_version;
   dec->session_param = *param;

   if (dec->vcn_version >= VCN_5_0_0 || dec->vcn_version == VCN_4_0_3)
      dec->jpeg_version = RDECODE_JPEG_VER_3;
   else if (dec->vcn_version >= VCN_2_0_0)
      dec->jpeg_version = RDECODE_JPEG_VER_2;
   else
      dec->jpeg_version = RDECODE_JPEG_VER_1;

   if (dec->jpeg_version == RDECODE_JPEG_VER_1) {
      dec->reg.lmi_jpeg_read_64bit_bar_high = SOC15_REG_ADDR(mmUVD_LMI_JPEG_READ_64BIT_BAR_HIGH);
      dec->reg.lmi_jpeg_read_64bit_bar_low = SOC15_REG_ADDR(mmUVD_LMI_JPEG_READ_64BIT_BAR_LOW);
      dec->reg.jpeg_rb_base = SOC15_REG_ADDR(mmUVD_JPEG_RB_BASE);
      dec->reg.jpeg_rb_size = SOC15_REG_ADDR(mmUVD_JPEG_RB_SIZE);
      dec->reg.jpeg_rb_wptr = SOC15_REG_ADDR(mmUVD_JPEG_RB_WPTR);
      dec->reg.jpeg_pitch = SOC15_REG_ADDR(mmUVD_JPEG_PITCH);
      dec->reg.jpeg_uv_pitch = SOC15_REG_ADDR(mmUVD_JPEG_UV_PITCH);
      dec->reg.dec_y_tiling_surface = SOC15_REG_ADDR(mmUVD_JPEG_TILING_CTRL);
      dec->reg.dec_uv_tiling_surface = SOC15_REG_ADDR(mmUVD_JPEG_UV_TILING_CTRL);
      dec->reg.lmi_jpeg_write_64bit_bar_high = SOC15_REG_ADDR(mmUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH);
      dec->reg.lmi_jpeg_write_64bit_bar_low = SOC15_REG_ADDR(mmUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW);
      dec->reg.jpeg_tier_cntl2 = SOC15_REG_ADDR(mmUVD_JPEG_TIER_CNTL2);
      dec->reg.jpeg_outbuf_rptr = SOC15_REG_ADDR(mmUVD_JPEG_OUTBUF_RPTR);
      dec->reg.jpeg_int_en = SOC15_REG_ADDR(mmUVD_JPEG_INT_EN);
      dec->reg.jpeg_cntl = SOC15_REG_ADDR(mmUVD_JPEG_CNTL);
      dec->reg.jpeg_rb_rptr = SOC15_REG_ADDR(mmUVD_JPEG_RB_RPTR);
      dec->reg.jpeg_outbuf_wptr = SOC15_REG_ADDR(mmUVD_JPEG_OUTBUF_WPTR);
      dec->reg.jpeg_index = SOC15_REG_ADDR(mmUVD_JPEG_INDEX);
      dec->reg.jpeg_data = SOC15_REG_ADDR(mmUVD_JPEG_DATA);
   } else {
      dec->reg.jrbc_ib_cond_rd_timer = vcnipUVD_JRBC_IB_COND_RD_TIMER;
      dec->reg.jrbc_ib_ref_data = vcnipUVD_JRBC_IB_REF_DATA;
      dec->reg.jpeg_rb_base = vcnipUVD_JPEG_RB_BASE;
      dec->reg.jpeg_rb_size = vcnipUVD_JPEG_RB_SIZE;
      dec->reg.jpeg_rb_wptr = vcnipUVD_JPEG_RB_WPTR;
      dec->reg.jpeg_int_en = vcnipUVD_JPEG_INT_EN;
      dec->reg.jpeg_cntl = vcnipUVD_JPEG_CNTL;
      dec->reg.jpeg_rb_rptr = vcnipUVD_JPEG_RB_RPTR;
      if (dec->jpeg_version == RDECODE_JPEG_VER_2) {
         dec->reg.jpeg_dec_soft_rst = vcnipUVD_JPEG_DEC_SOFT_RST;
         dec->reg.lmi_jpeg_read_64bit_bar_high = vcnipUVD_LMI_JPEG_READ_64BIT_BAR_HIGH;
         dec->reg.lmi_jpeg_read_64bit_bar_low = vcnipUVD_LMI_JPEG_READ_64BIT_BAR_LOW;
         dec->reg.jpeg_pitch = vcnipUVD_JPEG_PITCH;
         dec->reg.jpeg_uv_pitch = vcnipUVD_JPEG_UV_PITCH;
         dec->reg.dec_addr_mode = vcnipJPEG_DEC_ADDR_MODE;
         dec->reg.dec_y_tiling_surface = vcnipJPEG_DEC_Y_GFX10_TILING_SURFACE;
         dec->reg.dec_uv_tiling_surface = vcnipJPEG_DEC_UV_GFX10_TILING_SURFACE;
         dec->reg.lmi_jpeg_write_64bit_bar_high = vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH;
         dec->reg.lmi_jpeg_write_64bit_bar_low = vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW;
         dec->reg.jpeg_tier_cntl2 = vcnipUVD_JPEG_TIER_CNTL2;
         dec->reg.jpeg_outbuf_cntl = vcnipUVD_JPEG_OUTBUF_CNTL;
         dec->reg.jpeg_outbuf_rptr = vcnipUVD_JPEG_OUTBUF_RPTR;
         dec->reg.jpeg_outbuf_wptr = vcnipUVD_JPEG_OUTBUF_WPTR;
         dec->reg.jpeg_index = vcnipUVD_JPEG_INDEX;
         dec->reg.jpeg_data = vcnipUVD_JPEG_DATA;
      } else if (dec->jpeg_version == RDECODE_JPEG_VER_3) {
         dec->reg.jpeg_dec_soft_rst = vcnipUVD_JPEG_DEC_SOFT_RST_1;
         dec->reg.lmi_jpeg_read_64bit_bar_high = vcnipUVD_LMI_JPEG_READ_64BIT_BAR_HIGH_1;
         dec->reg.lmi_jpeg_read_64bit_bar_low = vcnipUVD_LMI_JPEG_READ_64BIT_BAR_LOW_1;
         dec->reg.jpeg_pitch = vcnipUVD_JPEG_PITCH_1;
         dec->reg.jpeg_uv_pitch = vcnipUVD_JPEG_UV_PITCH_1;
         dec->reg.dec_addr_mode = vcnipJPEG_DEC_ADDR_MODE_1;
         dec->reg.dec_y_tiling_surface = vcnipJPEG_DEC_Y_GFX10_TILING_SURFACE_1;
         dec->reg.dec_uv_tiling_surface = vcnipJPEG_DEC_UV_GFX10_TILING_SURFACE_1;
         dec->reg.lmi_jpeg_write_64bit_bar_high = vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH_1;
         dec->reg.lmi_jpeg_write_64bit_bar_low = vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW_1;
         dec->reg.jpeg_tier_cntl2 = vcnipUVD_JPEG_TIER_CNTL2_1;
         dec->reg.jpeg_outbuf_cntl = vcnipUVD_JPEG_OUTBUF_CNTL_1;
         dec->reg.jpeg_outbuf_rptr = vcnipUVD_JPEG_OUTBUF_RPTR_1;
         dec->reg.jpeg_outbuf_wptr = vcnipUVD_JPEG_OUTBUF_WPTR_1;
         dec->reg.jpeg_luma_base0_0 = vcnipUVD_JPEG_LUMA_BASE0_0;
         dec->reg.jpeg_chroma_base0_0 = vcnipUVD_JPEG_CHROMA_BASE0_0;
         dec->reg.jpeg_chromav_base0_0 = vcnipUVD_JPEG_CHROMAV_BASE0_0;
      }
   }

   dec->base.ip_type = AMD_IP_VCN_JPEG;
   dec->base.param = *param;
   dec->base.max_decode_cmd_dw = 128;

   dec->base.destroy = vcn_jpeg_dec_destroy;
   dec->base.build_decode_cmd = vcn_jpeg_build_decode_cmd;

   return &dec->base;
}
