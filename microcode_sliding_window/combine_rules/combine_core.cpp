#include "combine_rule_defs.hpp"
#include "combine_core.hpp"

#include "combine_shift_and.hpp"
#include "bnot_and_low.hpp"
#include "shift_mod_bitsize.hpp"
#include "bytewise_shift.hpp"
#include "add_masked_one_conditional.hpp"
#include "bitor_followed_by_signbit_extrq.hpp"
#include "raise_bitlut_test_to_multieq_compare.hpp"
#include "byte_shifts_and_adds_to_ors.hpp"
#include "division_magic_number_rules.hpp"
#include "combine_cndop_and_xor_1.hpp"
#include "shr_shl_bittest_by_same_variable.hpp"
#include "div_and_mul_by_same_const_to_mask.hpp"
#include "combine_x86_64_bitand_high.hpp"
#include "combine_or_shift_seq.hpp"
#include "simd_bitops_simplify.hpp"
#include "rewrite_float_bitops.hpp"
#include "cleanup_late_flag_ops.hpp"
#include "memory_oper_rules.hpp"
#include "pow2_rounding_recognizers.hpp"

#include "bitlogic_misc.hpp"

class negated_sets_t : public mcombiner_rule_t {
public:
	virtual bool run_combine(mcombine_t* state);

	virtual const char* name() const {
		return "Negated sets";
	}
};
//neg (xdu (sets rcx.8, .-1).1, .-1).4, .-1).4
bool negated_sets_t::run_combine(mcombine_t* state) {
	

	minsn_t* insn = state->insn();
	

	if (insn->op() != m_neg)
		return false;

	//if (!is_definitely_topinsn_p(insn))
	//	return false;

	xdu_extraction_t initial_xdu{};

	if (!try_extract_xdu(&insn->l, &initial_xdu))
		return false;

	
	mop_t* firstxdu = initial_xdu.xdu_operand();
	/*
		auto [neginsn, expect_inner_xdu] = firstxdu->descend_to_unary_insn(m_neg, mop_d);

	if (!neginsn)
		return false;

	xdu_extraction_t innerxd{};

	if (!try_extract_xdu(expect_inner_xdu, &innerxd))
		return false;*/




	auto [setsinsn, mreg] = firstxdu->descend_to_unary_insn(m_sets, mop_r);


	if (!setsinsn)
		return false;

	if (mreg->size != insn->d.size)
		return false;

	mop_t backup_mr = *mreg;

	
	insn->opcode = m_sar;

	insn->l = backup_mr;

	insn->r.make_number((backup_mr.size * 8) - 1, 1);

	return true;

}
negated_sets_t negated_sets;

class xor_to_or_t : public mcombiner_rule_t {

public:
	virtual bool run_combine(mcombine_t*);
	virtual const char* name()  const {
		return "Xor non-overlapped to Or";
	}
};

bool xor_to_or_t::run_combine(mcombine_t* state) {
	minsn_t* insn = state->insn();
	if (insn->opcode != m_xor)
		return false;

	potential_valbits_t bitsl = try_compute_opnd_potential_valbits(&insn->r);
	potential_valbits_t bitsr = try_compute_opnd_potential_valbits(&insn->l);

	if (!(bitsl.value() & bitsr.value())) {

		insn->opcode = m_or;
		return true;
	}
	return false;
}

xor_to_or_t xor_to_or{};

class mul_to_and_t : public mcombiner_rule_t {

public:
	virtual bool run_combine(mcombine_t*);

	virtual const char* name() const {
		return "Mul to And";
	}
};

bool mul_to_and_t::run_combine(mcombine_t* state) {
	minsn_t* insn = state->insn();

	if (insn->op() != m_mul)
		return false;

	potential_valbits_t bitsl = try_compute_opnd_potential_valbits(&insn->l);
	potential_valbits_t bitsr = try_compute_opnd_potential_valbits(&insn->r);

	if (bitsl.value() == 1 && bitsr.value() == 1) {
		insn->opcode = m_and;
		return true;
	}
	return false;
}
mul_to_and_t mul_to_and{};

class shr_sar_to_and_not_t : public mcombiner_rule_t {
public:
	virtual bool run_combine(mcombine_t* state);

	virtual const char* name()  const {
		return "Shr/Sar to and not";
	}
};

bool shr_sar_to_and_not_t::run_combine(mcombine_t* state) {
	auto insn = state->insn();

	if (!is_mcode_shift_right(insn->op()))
		return false;

	auto [lb, rb] = try_compute_lr_potential_valbits(insn);

	if (!lb.is_boolish() || !rb.is_boolish() )
		return false;


	/*
		todo: handle nonbyte operands
	*/

	if (!insn->lr_both_size(1))
		return false;

	insn->opcode = m_and;

	mop_t backup_r = insn->r;

	insn->r.make_unary_insn_larg(m_lnot, backup_r, 1);


	return true;

}

shr_sar_to_and_not_t shr_sar_to_and_not{};

class add_onebits_equals_two_to_and_t : public mcombiner_rule_t {
public:
	virtual bool run_combine(mcombine_t* state);

	virtual const char* name()  const {
		return "True plus True test with Two";
	}
};

bool add_onebits_equals_two_to_and_t::run_combine(mcombine_t* state) {
	minsn_t* insn = state->insn();

	if (!is_mcode_zf_cond(insn->op()))
		return false;

	auto [addop, num] = insn->arrange_by(mop_d, mop_n);

	if (!addop || !num->is_equal_to(2ULL, false))
		return false;

	auto [addinsn, addl, addr] = addop->descend_to_binary_insn(m_add);
	if (!addinsn)
		return false;


	auto [lbits, rbits] = try_compute_lr_potential_valbits(addinsn);

	if (!lbits.is_boolish() || !rbits.is_boolish())
		return false;

	if (num != &insn->r)
		return false;

	addinsn->opcode = m_and;

	// (x+y) != 2 -> (x&y) != 1
	//(x+y) == 2 -> (x&y) == 1

	num->nnn->update_value(1ULL);

	return true;
}

add_onebits_equals_two_to_and_t add_onebits_equals_to_and{};

class sets_negated_bool_to_zftest_t : public mcombiner_rule_t {
public:
	virtual bool run_combine(mcombine_t* state);
	virtual const char* name()  const {
		return "Sets negated bool to ZF test";
	}
};
//dis no work
bool sets_negated_bool_to_zftest_t::run_combine(mcombine_t* state) {
	auto insn = state->insn();

	if (insn->op() != m_sets)
		return false;

	auto [neginsn, boolop] = insn->l.descend_to_unary_insn(m_neg);
	if (!neginsn)
		return false;

	auto valbits = try_compute_opnd_potential_valbits(boolop);

	if (!valbits.is_boolish())
		return false;
	mop_t backup_boolop = *boolop;

	insn->opcode = m_setnz;

	insn->l = backup_boolop;

	insn->r.make_number(0ULL, 1);
	return true;
}
sets_negated_bool_to_zftest_t sets_negated_bool_to_zftest{};

class xor_xy_sets_to_sets_and_t : public mcombiner_rule_t {
public:
	virtual bool run_combine(mcombine_t* state);

	virtual const char* name() const {
		return "Xor XY sign to sets and sets";
	}
};

bool xor_xy_sets_to_sets_and_t::run_combine(mcombine_t* state) {
	auto insn = state->insn();
	/*
		cannot run on topinsns for some reason
	*/

	if (is_definitely_topinsn_p(insn))
		return false;
	if (insn->op() != m_sets)
		return false;

	auto [xorinsn, x, y] = insn->l.descend_to_binary_insn(m_xor);
	if (!xorinsn)
		return false;

	mop_t bx = *x;
	mop_t by = *y;
	insn->opcode = m_and;

	insn->l.make_unary_insn_larg(m_sets, bx, 1);
	insn->r.make_unary_insn_larg(m_sets, by, 1);

	return true;
}

xor_xy_sets_to_sets_and_t xor_xy_sets_to_sets_and{};





/*
	disabling many rules that need fixing or need extensive testing
*/
static mcombiner_rule_t* g_allrules[] = {
	&combine_shift_and_rule,
	&raise_bitlut_multieq,
	& negated_sets,
	& xor_to_or,
	& mul_to_and,
	& shr_sar_to_and_not,
	& add_onebits_equals_to_and,
	//& sets_negated_bool_to_zftest,
//	& xor_xy_sets_to_sets_and,
	& stx_stx_combine,

	//& join_zf_jcnd
	//combine_bnot_and_1,
	/*combine_jzf_and_bnot,
	combine_shift_mod_bitsize,
	combine_bytewise_shift,
	combine_sign_shift_neg,
	combine_add_masked_one_conditional,
	combine_signbit_shift_and_bitop,
//combine_byte_shifts_and_adds_to_ors,

	division_magic_num_rule_1,
	shl_and_low,
	,
	*/
	&combine_or_shift,
	&div_and_mul_in_conditional_to_modulus_test,
	&combine_cndop_and_xor_1,
	& combine_bnot_and_one,
	&combine_jzf_and_bnot,
	& simd_ld_shrtrim,
	& detect_xdu_in_xor128,
	& detect_bitwise_negate_floatop,
	& locate_abs_value_floatpath,
	& combine_ltzero_result_shifted_to_highbit,
	& sift_down_flagcomps,
	& interblock_jcc_deps_combiner,
	& merge_shortcircuit_nosideeffects,
	& distribute_constant_sub_in_const_comp,
	&division_magic_num_rule_1,
	&preload_repetitive_ldx,
	& replace_boolean_flow_with_boolean_logic,
	& merge_short_circuit_or_with_no_side_effects,
	& merge_multi_setz_chain_interval,
	& interblock_flagop_merger,
	& recognize_overcombined_round_up_pow2,
	& setnez_1bit_to_logical_not,
	& comp1bit_to_jcnd,
	& sets_sub_to_cmp,
	& popcnt_bool_fold
};

void toggle_common_combination_rules(bool enabled) {
	for (auto&& comb : g_allrules) {
		hexext::install_combine_cb(comb, enabled);
	}
}

static  mcombiner_rule_t* const g_x86_rules[] = {
		/*combine_x86_64_bitand_high,
	combine_x86_64_bitor_high*/
	&combine_x86_band_high,
	&combine_x86_bitor_high,
	//unfortunately highbyte_used_with_lowbyte just gets turned back into highbyte causing an infinite loop
	//& highbyte_used_with_lowbyte
};

void toggle_archspec_combination_rules(bool enabled) {
	if (hexext::currarch() == hexext_arch_e::x86) {

		for (auto&& comb : g_x86_rules) {
			hexext::install_combine_cb(comb, enabled);
		}

	}
}