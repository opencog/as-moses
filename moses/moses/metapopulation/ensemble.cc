/**
 * ensemble.cc ---
 *
 * Copyright (C) 2014 Aidyia Limited
 *
 * Authors: Linas Vepstas
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <math.h>

#include <algorithm>
#include <iostream>

#include  <opencog/util/oc_assert.h>
#include  <moses/comboreduct/reduct/reduct.h>
#include  <moses/moses/moses/complexity.h>
#include "ensemble.h"

namespace opencog {
namespace moses {

using namespace combo;

ensemble::ensemble(behave_cscore& cs, const ensemble_parameters& ep) :
	_params(ep), _bscorer(cs.get_bscorer())
{
	// Don't mess with the scorer weights if not doing boosting.
	if (not ep.do_boosting) return;

	// Initialize the weights that the scorer will use.
	_bscorer.reset_weights();

	// _tolerance is an estimate of the accumulated rounding error
	// that arises when totaling the bscores.  As usual, assumes a
	// normal distribution for this, so that its a square-root.
	// Typical values should be about 1e-5 for floats, 1e-14 for doubles.
	size_t bslen = _bscorer.size();
	_tolerance = 2.0 * epsilon_score;
	_tolerance *= sqrt((double) bslen);

	_bias = 0.0;
	if (not ep.exact_experts) {
		_row_bias = std::vector<double>(bslen, 0.0);
	}

	logger().info() << "Boosting: number to promote: " << ep.num_to_promote;
	if (ep.experts) {
		logger().info() << "Boosting: exact experts: " << ep.exact_experts;
		logger().info() << "Boosting: expalpha: " << ep.expalpha;
	}
}

// Is this behavioral score correct? For boolean scores, correct is 0.0
// and incorrect is -1.0.
static inline bool is_correct(score_t val)
{
	return -0.5 < val;
}

/**
 * Implement a boosted ensemble. Candidate combo trees are added to
 * the ensemble one at a time, weights are adjusted, and etc.
 */
void ensemble::add_candidates(scored_combo_tree_set& cands)
{
	if (_params.experts) {
		add_expert(cands);
		return;
	}
	add_adaboost(cands);
}

/**
 * Implement a boosted ensemble, using the classic AdaBoost algo.
 */
void ensemble::add_adaboost(scored_combo_tree_set& cands)
{
	int promoted = 0;

	while (true) {
		// Find the element (the combo tree) with the least error. This is
		// the element with the highest score.
		scored_combo_tree_set::iterator best_p =
			std::min_element(cands.begin(), cands.end(),
				[](const scored_combo_tree& a, const scored_combo_tree& b) {
					return a.get_score() > b.get_score(); });

		logger().info() << "Boosting: candidate score=" << best_p->get_score();
		double err = _bscorer.get_error(best_p->get_bscore());
		OC_ASSERT(0.0 <= err and err < 1.0, "boosting score out of range; got %g", err);

		// This condition indicates "perfect score". It does not typically
		// happen, but if it does, then we have no need for all the boosting
		// done up to this point.  Thus, we erase the entire ensemble; its
		// now superfluous, and replace it by the single best answer.
		if (err < _tolerance) {
			logger().info() << "Boosting: perfect score found: " << &best_p;

			// Wipe out the ensemble.
			_scored_trees.clear();
			scored_combo_tree best(*best_p);
			best.set_weight(1.0);
			_scored_trees.insert(best);

			// Clear out the scorer weights, to avoid down-stream confusion.
			_bscorer.reset_weights();

			return;
		}

		// Any score worse than half is terrible. Half gives a weight of zero.
		if (0.5 <= err) {
			logger().info() << "Boosting: no improvement, ensemble not expanded";
			break;
		}

		// Compute alpha
		double alpha = 0.5 * log ((1.0 - err) / err);
		double expalpha = exp(alpha);
		double rcpalpha = 1.0 / expalpha;
		logger().info() << "Boosting: add to ensemble " << *best_p  << std::endl
			<< "With err=" << err << " alpha=" << alpha <<" exp(alpha)=" << expalpha;

		// Set the weight for the tree, and stick it in the ensemble
		scored_combo_tree best = *best_p;
		best.set_weight(alpha);
		_scored_trees.insert(best);

		// Recompute the weights
		const behavioral_score& bs = best_p->get_bscore();
		size_t bslen = _bscorer.size();
		std::vector<double> weights(bslen);
		for (size_t i=0; i<bslen; i++)
		{
			weights[i] = is_correct(bs[i]) ? rcpalpha : expalpha;
		}
      _bscorer.update_weights(weights);

		// Remove from the set of candidates.
		cands.erase(best_p);

		// Are we done yet?
		promoted ++;
		if (_params.num_to_promote <= promoted) break;
		if (cands.empty()) break;
	}
}

/**
 * Add trees that expertly select rows.  These are trees that select
 * only a handful of rows, those that are exactly the true-postive rows.
 * If the flag exact_experts is NOT set, then some inexact trees are
 * admitted to the ensemble; a voting/iasing scheme is used to make sure
 * that they don't do too much damage.
 *
 * The current algo, as implemented, admits any tree that expertly
 * selects rows, even if the ensemble already knows how to select those
 * same rows. This may be good, or it may be bad ... depending on what
 * you want.   An alternative would be to add only those experts that
 * know things that the ensemble does not yet know about.
 *
 * At most, _params.num_to_promote are added.
 */
void ensemble::add_expert(scored_combo_tree_set& cands)
{
	int promoted = 0;
	int num = 0;
	for (const scored_combo_tree& sct : cands) {
		num++;
		// Add all elements that have a perfect score, or at least,
		// a good-enough score.
		double err = _bscorer.get_error(sct.get_tree());

		OC_ASSERT(0.0 <= err+_tolerance and err-_tolerance <= 1.0,
		          "boosting score out of range; got %g", err);

		// Set the weight for the tree, and stick it in the ensemble
		if (_params.exact_experts) {
			// This condition indicates "perfect score". This is the only type
			// of tree that we accept into the ensemble.
			if (_tolerance < err) {
				logger().debug() << "Exact expert " << num
				                 << " not good enough, err=" << err
				                 << " score=" << sct.get_score();
				continue;
			}

			logger().info() << "Exact expert " << num << " add to ensemble: " << sct;
			_scored_trees.insert(sct);

			// Increase the importance of all remaining, unselected rows.
			double expalpha = _params.expalpha; // Add-hoc boosting value ...
			double rcpalpha = 1.0 / expalpha;

			// Trick the scorer into using the flat scorer.  Do this by
			// sticking the single tree into a tree set.
			scored_combo_tree_set treeset;
         treeset.insert(sct);
			behavioral_score bs(_bscorer.operator()(treeset));
			size_t bslen = _bscorer.size();
			std::vector<double> weights(bslen);
			for (size_t i=0; i<bslen; i++)
			{
				// Again, here we explicitly assume the pre scorer: A row is
				// correctly selected if its score is strictly positive.
				// The weights of positive and selected rows must decrease.
				// (because we would like to not select them again).
				// For the weights of unselected rows, positive or negative,
				// well hey, we have a choice: we could increase them, or we
				// could leave them alone. We should leave them alone, as 
				// otherwise, we are increasing the weight on rows that had
				// been previously selected, but aren't now.  We can do this
				// here, or in the pre scorer ... probably best to do it in
				// the pre scorer.
				weights[i] = (0.0 < bs[i]) ? rcpalpha : expalpha;
			}
			_bscorer.update_weights(weights);

		} else {
			// if we are here, its the in-exact experts code.  XXX currently broken ...
			// Any score worse than half is terrible. Half gives a weight of zero.
			// More than half gives negative weights, which wreaks things.
			if (0.5 <= err) {
				logger().debug() <<
				    "Expert: terrible precision, ensemble not expanded: " << err;
				continue;
			}
			// AdaBoost-style alpha; except we allow perfect scorers.
			if (err < _tolerance) err = _tolerance;
			double alpha = 0.5 * log ((1.0 - err) / err);
			double expalpha = exp(alpha);
			logger().info() << "Expert: add to ensemble; err=" << err
			                << " alpha=" << alpha <<" exp(alpha)=" << expalpha
			                << std::endl << sct;

			scored_combo_tree kopy(sct);
			kopy.set_weight(alpha);
			_scored_trees.insert(kopy);

			// Adjust the bias, if needed.
			const behavioral_score& bs = sct.get_bscore();
			size_t bslen = _bscorer.size();

			// XXX the logic below is probably wrong.
			OC_ASSERT(false, "this doesn't work right now.");
			// Now, look to see where this scorer was wrong, and bump the
			// bias for that.  Here, we make the defacto assumption that
			// the scorer is the "pre" scorer, and so the bs ranges from
			// -0.5 to +0.5, with zero denoting "row not selected by tree",
			// a positive score denoting "row correctly selected by tree",
			// and a negative score denoting "row wrongly selected by tree".
			// The scores differ from +/-0.5 only if the rows are degenerate.
			// Thus, the ensemble will incorrectly pick a row if it picks
			// the row, and the weight isn't at least _bias.  (Keep in mind
			// that alpha is the weight of the current tree.)
			for (size_t i=0; i<bslen; i++) {
				if (bs[i] >= 0.0) continue;
				// -2.0 to cancel the -0.5 for bad row.
				_row_bias[i] += -2.0 * alpha * bs[i];
				if (_bias < _row_bias[i]) _bias = _row_bias[i];
			}
			logger().info() << "Experts: bias is now: " << _bias;

			// Increase the importance of all remaining, unselected rows.
			double rcpalpha = 1.0 / expalpha;
			std::vector<double> weights(bslen);
			for (size_t i=0; i<bslen; i++)
			{
				// Again, here we explicitly assume the pre scorer: A row is
				// correctly selected if its score is strictly positive.
				// The weights of unselected rows must increase.
				weights[i] = (0.0 < bs[i]) ? rcpalpha : expalpha;
			}
			_bscorer.update_weights(weights);
		}

		// Are we done yet?
		promoted ++;
		if (_params.num_to_promote <= promoted) break;
	}
}

/// Return the ensemble contents as a single, large tree.
///
const combo::combo_tree& ensemble::get_weighted_tree() const
{
	if (_params.experts) {
		if (_params.exact_experts)
			get_exact_tree();
		else
			get_expert_tree();
	} else {
		get_adaboost_tree();
	}
	return _weighted_tree;
}

/// Return the ensemble contents as a single, large weighted tree.
///
/// Returns the combo tree expressing
///    (sum_i weight_i * (tree_i ? 1.0 : -1.0)) > 0)
/// i.e. true if the summation is positive, else false, as per standard
/// AdaBoost definition.
///
const combo::combo_tree& ensemble::get_adaboost_tree() const
{
	_weighted_tree.clear();

	if (1 == _scored_trees.size()) {
		_weighted_tree = _scored_trees.begin()->get_tree();
		return _weighted_tree;
	}

	combo::combo_tree::pre_order_iterator head, plus;

	head = _weighted_tree.set_head(combo::id::greater_than_zero);
	plus = _weighted_tree.append_child(head, combo::id::plus);

	for (const scored_combo_tree& sct : _scored_trees)
	{
		combo::combo_tree::pre_order_iterator times, minus, impulse;

		// times is (weight * (tree - 0.5))
		times = _weighted_tree.append_child(plus, combo::id::times);
		vertex weight = sct.get_weight();
		_weighted_tree.append_child(times, weight);

		// minus is (tree - 0.5) so that minus is equal to +0.5 if
		// tree is true, else it is equal to -0.5
		minus = _weighted_tree.append_child(times, combo::id::plus);
		vertex half = -0.5;
		_weighted_tree.append_child(minus, half);
		impulse = _weighted_tree.append_child(minus, combo::id::impulse);
		_weighted_tree.append_child(impulse, sct.get_tree().begin());
	}

	return _weighted_tree;
}

/// Return the ensemble contents as a single, large tree.
///
/// Returns the combo tree expressing
///   or_i tree_i
/// i.e. true if any member of the ensemble picked true.
///
const combo::combo_tree& ensemble::get_exact_tree() const
{
	_weighted_tree.clear();
	if (1 == _scored_trees.size()) {
		_weighted_tree = _scored_trees.begin()->get_tree();
		return _weighted_tree;
	}

	combo::combo_tree::pre_order_iterator head;
	head = _weighted_tree.set_head(combo::id::logical_or);

	for (const scored_combo_tree& sct : _scored_trees)
		_weighted_tree.append_child(head, sct.get_tree().begin());

	// Very surprisingly, reduct does not seem to make a differrence! Hmmm.
	// std::cout << "before reduct " << tree_complexity(_weighted_tree) << std::endl;
	// reduct::logical_reduce(1, _weighted_tree);
	// std::cout << "ater reduct " << tree_complexity(_weighted_tree) << std::endl;

	return _weighted_tree;
}

/// Return the ensemble contents as a single, large tree.
///
/// Returns the combo tree expressing
///    (sum_i weight_i * (tree_i ? 1.0 : 0.0)) > _bias)
/// i.e. true if the summation is greater than the bias, else false.
/// The goal of the bias is to neuter those members of the ensemble
/// that are making mistakes in their selection.
///
const combo::combo_tree& ensemble::get_expert_tree() const
{
	_weighted_tree.clear();
	if (1 == _scored_trees.size()) {
		_weighted_tree = _scored_trees.begin()->get_tree();
		return _weighted_tree;
	}

	combo::combo_tree::pre_order_iterator head, plus;

	head = _weighted_tree.set_head(combo::id::greater_than_zero);
	plus = _weighted_tree.append_child(head, combo::id::plus);

	// score must be bettter than the bias.
	vertex bias = -_bias * _params.bias_scale;
	_weighted_tree.append_child(plus, bias);

	for (const scored_combo_tree& sct : _scored_trees)
	{
		combo::combo_tree::pre_order_iterator times, impulse;

		// times is (weight * tree)
		times = _weighted_tree.append_child(plus, combo::id::times);
		vertex weight = sct.get_weight();
		_weighted_tree.append_child(times, weight);

		// impulse is +1.0 if tree is true, else it is equal to 0.0
		impulse = _weighted_tree.append_child(times, combo::id::impulse);
		_weighted_tree.append_child(impulse, sct.get_tree().begin());
	}
	return _weighted_tree;
}

/**
 * Return the plain, unweighted, "flat" score for the ensemble as a
 * whole.  This is the score that the ensemble would get when used
 * for prediction; by contrast, the weighted score only applies for
 * training, and is always driven to be 50% wrong, on average.
 */
score_t ensemble::flat_score() const
{
	behavioral_score bs(_bscorer(_scored_trees));
	return boost::accumulate(bs, 0.0);
}


}}; // namespace opencog::moses

