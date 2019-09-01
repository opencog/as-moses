/** populate_atomspace.h ---
 *
 * Copyright (C) 2018 OpenCog Foundation
 *
 * Authors: Kasim Ebrahim <se.kasim.ebrahim@gmail.com>
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


#ifndef MOSES_POPULATE_ATOMSPACE_H
#define MOSES_POPULATE_ATOMSPACE_H

#include <opencog/data/table/table.h>
#include <opencog/data/table/table_io.h>
#include <opencog/atomspace/AtomSpace.h>
#include <opencog/utils/value_key.h>
#include <opencog/atoms/base/Link.h>
#include <opencog/utils/valueUtils.h>

namespace opencog { namespace moses {
using namespace combo;

/**
 * Populate the given atomspace with the input data.
 * @param Atomspace&     as
 * @param Itable&     itable
 *
 */
void populate(AtomSpace *as, const ITable &itable);

/**
 * Populate the given atomspace with input features of compressed table.
 * @param Atomspace&     as
 * @param Ctable&     ctable
 *
 */
void populate(AtomSpace *as, const CTable &ctable);

/**
 * Convertes vertex sequences to its corresponding values
 * @param vseq
 * @param tnode
 * @return ValuePtr containing values such as FloatValue, LinKValues...etc
 */
ValuePtr vertex_seq_to_value(const vertex_seq& vseq, id::type_node tnode);
#define GET_ENUM [](const vertex &ver) { \
    auto en = get_enum_type(ver); \
    return ValuePtr(createNode(CONCEPT_NODE, en.getContent())); \
    }
		
template <typename... HandleArgs>
inline void populate_frm_ctable(CTable _ctable, HandleArgs &&... handleArgs) {
	std::vector<Handle> vect1{std::forward<HandleArgs>(handleArgs)...};
	int index = 0;
	for (Handle h: vect1) {
		Type type = h->get_type();
		ValueSeq _f_values;
		if (type == PREDICATE_NODE) {
			boost::transform(_ctable.get_input_col_data(index), back_inserter(_f_values), bool_vertex_to_value);
			ValuePtr f_proto_atom(new LinkValue(_f_values));
			h->setValue(moses::compressed_value_key, f_proto_atom);
		} else if (type == CONCEPT_NODE) {
			ValueSeq _f_values;
			boost::transform(_ctable.get_input_col_data(index), back_inserter(_f_values), GET_ENUM);
			ValuePtr f_proto_atom(new LinkValue(_f_values));
			h->setValue(moses::compressed_value_key, f_proto_atom);
		} else if (type == SCHEMA_NODE) {
			std::vector<double> _f_values;
			boost::transform(_ctable.get_input_col_data(index), back_inserter(_f_values), get_contin);
			ValuePtr f_proto_atom(new FloatValue(_f_values));
			h->setValue(moses::compressed_value_key, f_proto_atom);
		} else OC_ASSERT(false, "Node Type don't exist or not implemented!");
		index++;
	}
}

template <typename... HandleArgs>
inline void populate_frm_table(Table _table, HandleArgs &&... handleArgs) {
	std::vector<Handle> vect1{std::forward<HandleArgs>(handleArgs)...};
	int index = 0;
	for (Handle h: vect1) {
	Type type = h->get_type();
	ValueSeq _f_values;
	if (type == PREDICATE_NODE) {
		boost::transform(_table.itable.get_column_data(index), back_inserter(_f_values),
		                 bool_vertex_to_value);
		ValuePtr f_proto_atom(new LinkValue(_f_values));
		h->setValue(moses::value_key, f_proto_atom);
	} else if (type == CONCEPT_NODE) {
		ValueSeq _f_values;
		boost::transform(_table.itable.get_column_data(index), back_inserter(_f_values), GET_ENUM);
		ValuePtr f_proto_atom(new LinkValue(_f_values));
		h->setValue(moses::value_key, f_proto_atom);
	} else if (type == SCHEMA_NODE) {
		std::vector<double> _f_values;
		boost::transform(_table.itable.get_column_data(index), back_inserter(_f_values), get_contin);
		ValuePtr f_proto_atom(new FloatValue(_f_values));
		h->setValue(moses::value_key, f_proto_atom);
	} else OC_ASSERT(false, "Node Type don't exist or not implemented!");
	index++;
	}
}

HandleSeq populate(const CTable& ctable);


} // ~namespace moses
} // ~namespace opencog
#endif

