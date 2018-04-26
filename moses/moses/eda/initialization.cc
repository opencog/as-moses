/*
 * moses/moses/eda/initialization.cc
 *
 * Copyright (C) 2002-2008 Novamente LLC
 * All Rights Reserved
 *
 * Written by Moshe Looks
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
#include "initialization.h"
#include "../moses/neighborhood_sampling.h"

namespace opencog { 
namespace moses {

using namespace std;

void occam_randomize_contin(const field_set& fs, instance& inst,
                            field_set::contin_iterator it,
                            opencog::RandGen& rng)
{
    unsigned int n = rng.randint(fs.contin()[it.idx()].depth);
    moses::generate_contin_neighbor(fs, inst, it, n, rng);
}

// Tree should be roughly balanced for this to be effective -
// otherwise there will be a bias towards smaller programs
void occam_randomize_term(const field_set& fs, instance& inst,
                          field_set::const_term_iterator it,
                          opencog::RandGen& rng)
{
    //if there are n levels
    size_t begin = fs.term_to_raw_idx(it.idx());
    size_t end = begin + fs.term()[it.idx()].depth;
    size_t middle = begin + rng.randint(end - begin + 1);

    const term_tree& tr = (*fs.term()[it.idx()].tr);
    term_tree::iterator node = tr.begin();

    for (;begin != middle && !node.is_childless();++begin) {
        int child_idx = rng.randint(node.number_of_children());
        fs.set_raw(inst, begin,
                   field_set::term_spec::from_child_idx(child_idx));
        node = tr.child(node, child_idx);
    }
    for (;begin != end;++begin)
        fs.set_raw(inst, begin, field_set::term_spec::Stop);
}

void occam_randomize_term(const field_set& fs, instance& inst,
                          opencog::RandGen& rng)
{
    for (field_set::const_term_iterator it = fs.begin_term(inst);
            it != fs.end_term(inst);++it)
        occam_randomize_term(fs, inst, it, rng);
}

void occam_randomize_contin(const field_set& fs, instance& inst,
                            opencog::RandGen& rng)
{
    for (field_set::contin_iterator it = fs.begin_contin(inst);
         it != fs.end_contin(inst); ++it)
        occam_randomize_contin(fs, inst, it, rng);
}

void uniform_randomize_bit(const field_set& fs, instance& inst,
                           opencog::RandGen& rng)
{
    //could be faster
    generate(fs.begin_bit(inst), fs.end_bit(inst),
             bind(&RandGen::randbool, ref(rng)));
}

void uniform_randomize_disc(const field_set& fs, instance& inst,
                            opencog::RandGen& rng)
{
    for (field_set::disc_iterator it = fs.begin_disc(inst);
            it != fs.end_disc(inst);++it)
        it.randomize(rng);
}

void randomize(const field_set& fs, instance& inst,
               opencog::RandGen& rng)
{
	occam_randomize_term(fs, inst, rng);
	occam_randomize_contin(fs, inst, rng);
	uniform_randomize_disc(fs, inst, rng);
	uniform_randomize_bit(fs, inst, rng);
}

} // ~namespace moses
} // ~namespace opencog

