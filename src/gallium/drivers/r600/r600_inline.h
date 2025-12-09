#ifndef R600_INLINE_H
#define R600_INLINE_H

static inline void r600_disable_cliprect_rule(struct radeon_cmdbuf *const cs)
{
	radeon_set_context_reg_seq(cs, R_02820C_PA_SC_CLIPRECT_RULE, 1);
	radeon_emit(cs, 0xffff);
}

#endif
