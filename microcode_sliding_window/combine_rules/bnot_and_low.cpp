
#include "combine_rule_defs.hpp"
#include "bnot_and_low.hpp"



/*
and (bnot (low (shr (ldx ds.2, (add rcx.8, #4.8).8).4, #4.1).4, .-1).1, .-1).1, #1.1, rax.1

*/

/*
 Take (~(x) & 1) and turn it into (x) & 1 == 0
 GCC likes to generate conditionals like these on ARM32 and x86-64
*/
COMB_RULE_IMPLEMENT(combine_bnot_and_one){

	mblock_t* blk = state->block(); 
	
	minsn_t* insn_ = state->insn();

	if (insn_->op() != m_and)
		return false;

	auto [bnotop, numop] = insn_->arrange_by_insn(m_bnot);
	if (!bnotop || !numop || !numop->is_equal_to(1ULL, false))
		return false;

	auto [bninsn, bnoperand] = bnotop->descend_to_unary_insn(m_bnot);
	if (!bninsn)
		return false;
	mop_t dstguy{};
	bool hasdst = false;
	if (insn_->d.t != mop_z) {
		dstguy = insn_->d;
		hasdst = true;
	}
	mop_t x = *bnoperand;
	mop_t y = *numop;
	y.size = x.size;
	unsigned xtsize = insn_->d.size;
	setup_ztest_bitand(insn_, true, &x, &y, xtsize);

	if (hasdst) {
		insn_->d = dstguy;
	}
	return true;
}



//jnz (and (bnot (low (shr rsi.4, #3.1).4, .-1).1, .-1).1, #1.1).1, #0.1, 2

COMB_RULE_IMPLEMENT(combine_jzf_and_bnot){
	mblock_t* blk = state->block();

	minsn_t* insn = state->insn();

	mcode_t op = insn->opcode;

	if (op != m_jz && op != m_jnz)
		return false;

	auto constop = insn->get_eitherlr(mop_n);
	auto insnop = insn->get_eitherlr(mop_d);

	if (!constop || !insnop)
		return false;

	if (constop->nnn->value != 0ULL)
		return false;

	auto inner = insnop->d;

	if (inner->opcode != m_and) {
		return false;
	}
	auto notop = inner->get_either_insnop(m_bnot);


	if (!notop) {
		return false;
	}



	auto negated_jzf = negate_mcode_relation(op);

	insn->opcode = negated_jzf;

	auto old_bnot = notop->d;

	notop->d = old_bnot->l.d;
	notop->t = old_bnot->l.t;

	old_bnot->l.t = mop_z;

	delete old_bnot;

	return true;
}


