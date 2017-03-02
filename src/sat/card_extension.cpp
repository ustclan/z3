/*++
Copyright (c) 2017 Microsoft Corporation

Module Name:

    card_extension.cpp

Abstract:

    Extension for cardinality and xor reasoning.

Author:

    Nikolaj Bjorner (nbjorner) 2017-01-30

Revision History:

--*/

#include"card_extension.h"
#include"sat_types.h"

namespace sat {

    card_extension::card::card(unsigned index, literal lit, literal_vector const& lits, unsigned k):
        m_index(index),
        m_lit(lit),
        m_k(k),
        m_size(lits.size())
    {
        for (unsigned i = 0; i < lits.size(); ++i) {
            m_lits[i] = lits[i];
        }
    }

    void card_extension::card::negate() {
        m_lit.neg();
        for (unsigned i = 0; i < m_size; ++i) {
            m_lits[i].neg();
        }
        m_k = m_size - m_k + 1;
        SASSERT(m_size >= m_k && m_k > 0);
    }

    card_extension::xor::xor(unsigned index, literal lit, literal_vector const& lits):
        m_index(index),
        m_lit(lit),
        m_size(lits.size())
    {
        for (unsigned i = 0; i < lits.size(); ++i) {
            m_lits[i] = lits[i];
        }
    }

    void card_extension::init_watch(bool_var v) {
        if (m_var_infos.size() <= static_cast<unsigned>(v)) {
            m_var_infos.resize(static_cast<unsigned>(v)+100);
        }
    }

    void card_extension::init_watch(card& c, bool is_true) {
        clear_watch(c);
        if (c.lit() != null_literal && c.lit().sign() == is_true) {
            c.negate();
        }
        TRACE("sat", display(tout << "init watch: ", c, true););
        SASSERT(c.lit() == null_literal || value(c.lit()) == l_true);
        unsigned j = 0, sz = c.size(), bound = c.k();
        if (bound == sz) {
            for (unsigned i = 0; i < sz && !s().inconsistent(); ++i) {
                assign(c, c[i]);
            }
            return;
        }
        // put the non-false literals into the head.
        for (unsigned i = 0; i < sz; ++i) {
            if (value(c[i]) != l_false) {
                if (j != i) {
                    c.swap(i, j);
                }
                ++j;
            }
        }
        DEBUG_CODE(
            bool is_false = false;
            for (unsigned k = 0; k < sz; ++k) {
                SASSERT(!is_false || value(c[k]) == l_false);
                is_false = value(c[k]) == l_false;
            });

        // j is the number of non-false, sz - j the number of false.
        if (j < bound) {
            SASSERT(0 < bound && bound < sz);
            literal alit = c[j];
            
            //
            // we need the assignment level of the asserting literal to be maximal.
            // such that conflict resolution can use the asserting literal as a starting
            // point.
            //

            for (unsigned i = bound; i < sz; ++i) {                
                if (lvl(alit) < lvl(c[i])) {
                    c.swap(i, j);
                    alit = c[j];
                }
            }
            set_conflict(c, alit);
        }
        else if (j == bound) {
            for (unsigned i = 0; i < bound && !s().inconsistent(); ++i) {
                assign(c, c[i]);                
            }
        }
        else {
            for (unsigned i = 0; i <= bound; ++i) {
                watch_literal(c, c[i]);
            }
        }
    }

    void card_extension::clear_watch(card& c) {
        unsigned sz = std::min(c.k() + 1, c.size());
        for (unsigned i = 0; i < sz; ++i) {
            unwatch_literal(c[i], &c);            
        }
    }

    void card_extension::unwatch_literal(literal lit, card* c) {
        if (m_var_infos.size() <= static_cast<unsigned>(lit.var())) {
            return;
        }
        ptr_vector<card>*& cards = m_var_infos[lit.var()].m_card_watch[lit.sign()];
        if (!is_tag_empty(cards)) {
            if (remove(*cards, c)) {
                cards = set_tag_empty(cards);
            }        
        }
    }

    void card_extension::assign(card& c, literal lit) {
        switch (value(lit)) {
        case l_true: 
            break;
        case l_false: 
            set_conflict(c, lit); 
            break;
        default:
            m_stats.m_num_propagations++;
            m_num_propagations_since_pop++;
            //TRACE("sat", tout << "#prop: " << m_stats.m_num_propagations << " - " << c.lit() << " => " << lit << "\n";);
            SASSERT(validate_unit_propagation(c));
            if (s().m_config.m_drat) {
                svector<drat::premise> ps;
                literal_vector lits;
                if (c.lit() != null_literal) lits.push_back(~c.lit());
                for (unsigned i = c.k(); i < c.size(); ++i) {
                    lits.push_back(c[i]);
                }
                lits.push_back(lit);
                ps.push_back(drat::premise(drat::s_ext(), c.lit())); // null_literal case.
                s().m_drat.add(lits, ps);
            }
            s().assign(lit, justification::mk_ext_justification(c.index()));
            break;
        }
    }

    void card_extension::watch_literal(card& c, literal lit) {
        TRACE("sat_verbose", tout << "watch: " << lit << "\n";);
        init_watch(lit.var());
        ptr_vector<card>* cards = m_var_infos[lit.var()].m_card_watch[lit.sign()];
        if (cards == 0) {
            cards = alloc(ptr_vector<card>);
            m_var_infos[lit.var()].m_card_watch[lit.sign()] = cards;
        }
        else if (is_tag_empty(cards)) {
            cards = set_tag_non_empty(cards);
            m_var_infos[lit.var()].m_card_watch[lit.sign()] = cards;            
        }
        TRACE("sat_verbose", tout << "insert: " << lit.var() << " " << lit.sign() << "\n";);
        cards->push_back(&c);
    }

    void card_extension::set_conflict(card& c, literal lit) {
        TRACE("sat", display(tout, c, true); );
        SASSERT(validate_conflict(c));
        s().set_conflict(justification::mk_ext_justification(c.index()), ~lit);
        SASSERT(s().inconsistent());
    }

    void card_extension::clear_watch(xor& x) {
        unwatch_literal(x[0], &x);
        unwatch_literal(x[1], &x);         
    }

    void card_extension::unwatch_literal(literal lit, xor* c) {
        if (m_var_infos.size() <= static_cast<unsigned>(lit.var())) {
            return;
        }
        xor_watch* xors = m_var_infos[lit.var()].m_xor_watch;
        if (!is_tag_empty(xors)) {
            if (remove(*xors, c)) {
                xors = set_tag_empty(xors);
            }        
        }
    }

    bool card_extension::parity(xor const& x, unsigned offset) const {
        bool odd = false;
        unsigned sz = x.size();
        for (unsigned i = offset; i < sz; ++i) {
            SASSERT(value(x[i]) != l_undef);
            if (value(x[i]) == l_true) {
                odd = !odd;
            }
        }
        return odd;
    }

    void card_extension::init_watch(xor& x, bool is_true) {
        clear_watch(x);
        if (x.lit() != null_literal && x.lit().sign() == is_true) {
            x.negate();
        }
        unsigned sz = x.size();
        unsigned j = 0;
        for (unsigned i = 0; i < sz && j < 2; ++i) {
            if (value(x[i]) == l_undef) {
                x.swap(i, j);
                ++j;
            }
        }
        switch (j) {
        case 0: 
            if (!parity(x, 0)) {
                set_conflict(x, x[0]);
            }
            break;
        case 1: 
            assign(x, parity(x, 1) ? ~x[0] : x[0]);
            break;
        default: 
            SASSERT(j == 2);
            watch_literal(x, x[0]);
            watch_literal(x, x[1]);
            break;
        }
    }

    void card_extension::assign(xor& x, literal lit) {
        switch (value(lit)) {
        case l_true: 
            break;
        case l_false: 
            set_conflict(x, lit); 
            break;
        default:
            m_stats.m_num_propagations++;
            m_num_propagations_since_pop++;
            if (s().m_config.m_drat) {
                svector<drat::premise> ps;
                literal_vector lits;
                if (x.lit() != null_literal) lits.push_back(~x.lit());
                for (unsigned i = 1; i < x.size(); ++i) {
                    lits.push_back(x[i]);
                }
                lits.push_back(lit);
                ps.push_back(drat::premise(drat::s_ext(), x.lit()));
                s().m_drat.add(lits, ps);
            }
            s().assign(lit, justification::mk_ext_justification(x.index()));
            break;
        }
    }

    void card_extension::watch_literal(xor& x, literal lit) {
        TRACE("sat_verbose", tout << "watch: " << lit << "\n";);
        init_watch(lit.var());
        xor_watch*& xors = m_var_infos[lit.var()].m_xor_watch;
        if (xors == 0) {
            xors = alloc(ptr_vector<xor>);
        }
        else if (is_tag_empty(xors)) {
            xors = set_tag_non_empty(xors);
        }
        xors->push_back(&x);
        TRACE("sat_verbose", tout << "insert: " << lit.var() << " " << lit.sign() << "\n";);
    }


    void card_extension::set_conflict(xor& x, literal lit) {
        TRACE("sat", display(tout, x, true); );
        SASSERT(validate_conflict(x));
        s().set_conflict(justification::mk_ext_justification(x.index()), ~lit);
        SASSERT(s().inconsistent());
    }

    lbool card_extension::add_assign(xor& x, literal alit) {
        // literal is assigned     
        unsigned sz = x.size();
        TRACE("sat", tout << "assign: " << x.lit() << ": " << ~alit << "@" << lvl(~alit) << "\n";);

        SASSERT(value(alit) != l_undef);
        SASSERT(x.lit() == null_literal || value(x.lit()) == l_true);
        unsigned index = 0;
        for (; index <= 2; ++index) {
            if (x[index].var() == alit.var()) break;
        }
        if (index == 2) {
            // literal is no longer watched.
            return l_undef;
        }
        SASSERT(x[index].var() == alit.var());
        
        // find a literal to swap with:
        for (unsigned i = 2; i < sz; ++i) {
            literal lit2 = x[i];
            if (value(lit2) == l_undef) {
                x.swap(index, i);
                watch_literal(x, lit2);
                return l_undef;
            }
        }
        if (index == 0) {
            x.swap(0, 1);
        }
        // alit resides at index 1.
        SASSERT(x[1].var() == alit.var());        
        if (value(x[0]) == l_undef) {
            bool p = parity(x, 1);
            assign(x, p ? ~x[0] : x[0]);            
        }
        else if (!parity(x, 0)) {
            set_conflict(x, x[0]);
        }      
        return s().inconsistent() ? l_false : l_true;  
    }

    
    void card_extension::normalize_active_coeffs() {
        while (!m_active_var_set.empty()) m_active_var_set.erase();
        unsigned i = 0, j = 0, sz = m_active_vars.size();
        for (; i < sz; ++i) {
            bool_var v = m_active_vars[i];
            if (!m_active_var_set.contains(v) && get_coeff(v) != 0) {
                m_active_var_set.insert(v);
                if (j != i) {
                    m_active_vars[j] = m_active_vars[i];
                }
                ++j;
            }
        }
        sz = j;
        m_active_vars.shrink(sz);
    }

    void card_extension::inc_coeff(literal l, int offset) {
        SASSERT(offset > 0);
        bool_var v = l.var();
        SASSERT(v != null_bool_var);
        if (static_cast<bool_var>(m_coeffs.size()) <= v) {
            m_coeffs.resize(v + 1, 0);
        }
        int coeff0 = m_coeffs[v];
        if (coeff0 == 0) {
            m_active_vars.push_back(v);
        }
        
        int inc = l.sign() ? -offset : offset;
        int coeff1 = inc + coeff0;
        m_coeffs[v] = coeff1;

        if (coeff0 > 0 && inc < 0) {
            m_bound -= coeff0 - std::max(0, coeff1);
        }
        else if (coeff0 < 0 && inc > 0) {
            m_bound -= std::min(0, coeff1) - coeff0;
        }
        // reduce coefficient to be no larger than bound.
        if (coeff1 > m_bound) {
            m_coeffs[v] = m_bound;
        }
        else if (coeff1 < 0 && -coeff1 > m_bound) {
            m_coeffs[v] = -m_bound;
        }
    }

    int card_extension::get_coeff(bool_var v) const {
        return m_coeffs.get(v, 0);
    }

    int card_extension::get_abs_coeff(bool_var v) const {
        return abs(get_coeff(v));
    }

    void card_extension::reset_coeffs() {
        for (unsigned i = 0; i < m_active_vars.size(); ++i) {
            m_coeffs[m_active_vars[i]] = 0;
        }
        m_active_vars.reset();
    }


    bool card_extension::resolve_conflict() {        
        if (0 == m_num_propagations_since_pop) 
            return false;
        reset_coeffs();
        m_num_marks = 0;
        m_bound = 0;
        literal consequent = s().m_not_l;
        justification js = s().m_conflict;
        m_conflict_lvl = s().get_max_lvl(consequent, js);
        if (consequent != null_literal) {
            consequent.neg();
            process_antecedent(consequent, 1);
        }
        literal_vector const& lits = s().m_trail;
        unsigned idx = lits.size()-1;
        int offset = 1;

        DEBUG_CODE(active2pb(m_A););

        unsigned init_marks = m_num_marks;

        vector<justification> jus;

        do {

            if (offset == 0) {
                goto process_next_resolvent;            
            }
            // TBD: need proper check for overflow.
            if (offset > (1 << 12)) {
                IF_VERBOSE(12, verbose_stream() << "offset: " << offset << "\n";
                           active2pb(m_A);
                           display(verbose_stream(), m_A);
                           );
                goto bail_out;
            }

            // TRACE("sat", display(tout, m_A););
            TRACE("sat", tout << "process consequent: " << consequent << ":\n"; s().display_justification(tout, js) << "\n";);
            SASSERT(offset > 0);
            SASSERT(m_bound >= 0);

            DEBUG_CODE(justification2pb(js, consequent, offset, m_B););
            
            switch(js.get_kind()) {
            case justification::NONE:
                SASSERT (consequent != null_literal);
                m_bound += offset;
                break;
            case justification::BINARY:
                m_bound += offset;
                SASSERT (consequent != null_literal);
                inc_coeff(consequent, offset);
                process_antecedent(js.get_literal(), offset);
                break;
            case justification::TERNARY:
                m_bound += offset;
                SASSERT (consequent != null_literal);
                inc_coeff(consequent, offset);
                process_antecedent(js.get_literal1(), offset);
                process_antecedent(js.get_literal2(), offset);
                break;
            case justification::CLAUSE: {
                m_bound += offset;
                clause & c = *(s().m_cls_allocator.get_clause(js.get_clause_offset()));
                unsigned i = 0;
                if (consequent != null_literal) {
                    inc_coeff(consequent, offset);
                    if (c[0] == consequent) {
                        i = 1;
                    }
                    else {
                        SASSERT(c[1] == consequent);
                        process_antecedent(c[0], offset);
                        i = 2;
                    }
                }
                unsigned sz = c.size();
                for (; i < sz; i++)
                    process_antecedent(c[i], offset);
                break;
            }
            case justification::EXT_JUSTIFICATION: {
                unsigned index = js.get_ext_justification_idx();
                if (is_card_index(index)) {
                    card& c = index2card(index);
                    m_bound += offset * c.k();
                    process_card(c, offset);
                }
                else {
                    // jus.push_back(js);
                    m_lemma.reset();
                    m_bound += offset;
                    inc_coeff(consequent, offset);
                    get_xor_antecedents(idx, m_lemma);
                    // get_antecedents(consequent, index, m_lemma);
                    for (unsigned i = 0; i < m_lemma.size(); ++i) {
                        process_antecedent(~m_lemma[i], offset);
                    }
                }
                break;
            }
            default:
                UNREACHABLE();
                break;
            }
            
            SASSERT(validate_lemma());            


            DEBUG_CODE(
                active2pb(m_C);
                //SASSERT(validate_resolvent());
                m_A = m_C;);

            TRACE("sat", display(tout << "conflict:\n", m_A););
            // cut();

        process_next_resolvent:
            
            // find the next marked variable in the assignment stack
            //
            bool_var v;
            while (true) {
                consequent = lits[idx];
                v = consequent.var();
                if (s().is_marked(v)) break;
                if (idx == 0) {
                    IF_VERBOSE(2, verbose_stream() << "did not find marked literal\n";);
                    goto bail_out;
                }
                SASSERT(idx > 0);
                --idx;
            }
            
            SASSERT(lvl(v) == m_conflict_lvl);
            s().reset_mark(v);
            --idx;
            --m_num_marks;
            js = s().m_justification[v];
            offset = get_abs_coeff(v);
            if (offset > m_bound) {
                m_coeffs[v] = (get_coeff(v) < 0) ? -m_bound : m_bound;
                offset = m_bound;
                DEBUG_CODE(active2pb(m_A););
            }
            SASSERT(value(consequent) == l_true);

        }        
        while (m_num_marks > 0);
        
        DEBUG_CODE(for (bool_var i = 0; i < static_cast<bool_var>(s().num_vars()); ++i) SASSERT(!s().is_marked(i)););
        SASSERT(validate_lemma());

        normalize_active_coeffs();
        
        int slack = -m_bound;
        for (unsigned i = 0; i < m_active_vars.size(); ++i) { 
            bool_var v = m_active_vars[i];
            slack += get_abs_coeff(v);
        }

        m_lemma.reset();        

#if 1
        m_lemma.push_back(null_literal);
        for (unsigned i = 0; 0 <= slack && i < m_active_vars.size(); ++i) { 
            bool_var v = m_active_vars[i];
            int coeff = get_coeff(v);
            lbool val = m_solver->value(v);
            bool is_true = val == l_true;
            bool append = coeff != 0 && val != l_undef && (coeff < 0 == is_true);
            if (append) {
                literal lit(v, !is_true);
                if (lvl(lit) == m_conflict_lvl) {
                    if (m_lemma[0] == null_literal) {
                        slack -= abs(coeff);
                        m_lemma[0] = ~lit;
                    }
                }
                else {
                    slack -= abs(coeff);
                    m_lemma.push_back(~lit);
                }
            }
        }

        if (jus.size() > 1) {
            std::cout << jus.size() << "\n";
            for (unsigned i = 0; i < jus.size(); ++i) {
                s().display_justification(std::cout, jus[i]); std::cout << "\n";
            }
            std::cout << m_lemma << "\n";
            active2pb(m_A);
            display(std::cout, m_A);
        }


        if (slack >= 0) {
            IF_VERBOSE(2, verbose_stream() << "(sat.card bail slack objective not met " << slack << ")\n";);
            goto bail_out;
        }

        if (m_lemma[0] == null_literal) {
            m_lemma[0] = m_lemma.back();
            m_lemma.pop_back();
            unsigned level = lvl(m_lemma[0]);
            for (unsigned i = 1; i < m_lemma.size(); ++i) {
                if (lvl(m_lemma[i]) > level) {
                    level = lvl(m_lemma[i]);
                    std::swap(m_lemma[0], m_lemma[i]);
                }
            }
            IF_VERBOSE(2, verbose_stream() << "(sat.card set level to " << level << " < " << m_conflict_lvl << ")\n";);
        }        
#else
        ++idx;
        while (0 <= slack) {            
            literal lit = lits[idx];
            bool_var v = lit.var();
            if (m_active_var_set.contains(v)) {
                int coeff = get_coeff(v);
                if (coeff < 0 && !lit.sign()) {
                    slack += coeff;
                    m_lemma.push_back(~lit);
                }
                else if (coeff > 0 && lit.sign()) {
                    slack -= coeff;
                    m_lemma.push_back(~lit);
                }
            }
            if (idx == 0 && slack >= 0) {
                IF_VERBOSE(2, verbose_stream() << "(sat.card non-asserting)\n";);
                goto bail_out;
            }
            SASSERT(idx > 0 || slack < 0);
            --idx;
        }
        if (m_lemma.size() >= 2 && lvl(m_lemma[1]) == m_conflict_lvl) {
            // TRACE("sat", tout << "Bail out on no progress " << lit << "\n";);
            IF_VERBOSE(2, verbose_stream() << "(sat.card bail non-asserting resolvent)\n";);
            goto bail_out;
        }

#endif


        SASSERT(slack < 0);

        SASSERT(validate_conflict(m_lemma, m_A));
        
        TRACE("sat", tout << m_lemma << "\n";);

        if (s().m_config.m_drat) {
            svector<drat::premise> ps; // TBD fill in
            s().m_drat.add(m_lemma, ps);
        }
        
        s().m_lemma.reset();
        s().m_lemma.append(m_lemma);
        for (unsigned i = 1; i < m_lemma.size(); ++i) {
            CTRACE("sat", s().is_marked(m_lemma[i].var()), tout << "marked: " << m_lemma[i] << "\n";);
            s().mark(m_lemma[i].var());
        }
        m_stats.m_num_conflicts++;

        return true;

    bail_out:
        while (m_num_marks > 0 && idx >= 0) {
            bool_var v = lits[idx].var();
            if (s().is_marked(v)) {
                s().reset_mark(v);
                --m_num_marks;
            }
            --idx;
        }
        return false;
    }

    void card_extension::process_card(card& c, int offset) {
        literal lit = c.lit();
        SASSERT(c.k() <= c.size());       
        SASSERT(lit == null_literal || value(lit) == l_true);
        SASSERT(0 < offset);
        for (unsigned i = c.k(); i < c.size(); ++i) {
            process_antecedent(c[i], offset);
        }
        for (unsigned i = 0; i < c.k(); ++i) {
            inc_coeff(c[i], offset);                        
        }
        if (lit != null_literal) {
            process_antecedent(~lit, c.k() * offset);
        }
    }

    void card_extension::process_antecedent(literal l, int offset) {
        SASSERT(value(l) == l_false);
        bool_var v = l.var();
        unsigned level = lvl(v);

        if (level > 0 && !s().is_marked(v) && level == m_conflict_lvl) {
            s().mark(v);
            ++m_num_marks;
        }
        inc_coeff(l, offset);                
    }   

    literal card_extension::get_asserting_literal(literal p) {
        if (get_abs_coeff(p.var()) != 0) {
            return p;
        }
        unsigned level = 0;        
        for (unsigned i = 0; i < m_active_vars.size(); ++i) { 
            bool_var v = m_active_vars[i];
            literal lit(v, get_coeff(v) < 0);
            if (value(lit) == l_false && lvl(lit) > level) {
                p = lit;
                level = lvl(lit);
            }
        }
        return p;        
    }

    card_extension::card_extension(): m_solver(0), m_has_xor(false) {        
        TRACE("sat", tout << this << "\n";);
    }

    card_extension::~card_extension() {
        for (unsigned i = 0; i < m_var_infos.size(); ++i) {
            m_var_infos[i].reset();
        }
        m_var_trail.reset();
        m_var_lim.reset();
        m_stats.reset();
    }

    void card_extension::add_at_least(bool_var v, literal_vector const& lits, unsigned k) {
        unsigned index = 2*m_cards.size();
        literal lit = v == null_bool_var ? null_literal : literal(v, false);
        card* c = new (memory::allocate(card::get_obj_size(lits.size()))) card(index, lit, lits, k);
        m_cards.push_back(c);
        if (v == null_bool_var) {
            // it is an axiom.
            init_watch(*c, true);
            m_card_axioms.push_back(c);
        }
        else {
            init_watch(v);
            m_var_infos[v].m_card = c;
            m_var_trail.push_back(v);
        }
    }

    void card_extension::add_xor(bool_var v, literal_vector const& lits) {
        m_has_xor = true;
        unsigned index = 2*m_xors.size()+1;
        xor* x = new (memory::allocate(xor::get_obj_size(lits.size()))) xor(index, literal(v, false), lits);
        m_xors.push_back(x);
        init_watch(v);
        m_var_infos[v].m_xor = x;
        m_var_trail.push_back(v);
    }


    void card_extension::propagate(literal l, ext_constraint_idx idx, bool & keep) {
        UNREACHABLE();
    }


    void card_extension::ensure_parity_size(bool_var v) {
        if (m_parity_marks.size() <= static_cast<unsigned>(v)) {
            m_parity_marks.resize(static_cast<unsigned>(v) + 1, 0);
        }
    }
    
    unsigned card_extension::get_parity(bool_var v) {
        return m_parity_marks.get(v, 0);
    }

    void card_extension::inc_parity(bool_var v) {
        ensure_parity_size(v);
        m_parity_marks[v]++;
    }

    void card_extension::reset_parity(bool_var v) {
        ensure_parity_size(v);
        m_parity_marks[v] = 0;
    }
    
    /**
       \brief perform parity resolution on xor premises.
       The idea is to collect premises based on xor resolvents. 
       Variables that are repeated an even number of times cancel out.
     */
    void card_extension::get_xor_antecedents(unsigned index, literal_vector& r) {
        literal_vector const& lits = s().m_trail;
        literal l = lits[index + 1];
        unsigned level = lvl(l);
        bool_var v = l.var();
        SASSERT(s().m_justification[v].get_kind() == justification::EXT_JUSTIFICATION);
        SASSERT(!is_card_index(s().m_justification[v].get_ext_justification_idx()));

        unsigned num_marks = 0;
        unsigned count = 0;
        while (true) {
            ++count;
            justification js = s().m_justification[v];
            if (js.get_kind() == justification::EXT_JUSTIFICATION) {
                unsigned idx = js.get_ext_justification_idx();
                if (is_card_index(idx)) {
                    r.push_back(l);
                }
                else {
                    xor& x = index2xor(idx);
                    if (x.lit() != null_literal && lvl(x.lit()) > 0) r.push_back(x.lit());
                    if (x[1].var() == l.var()) {
                        x.swap(0, 1);
                    }
                    SASSERT(x[0].var() == l.var());
                    for (unsigned i = 1; i < x.size(); ++i) {
                        literal lit(value(x[i]) == l_true ? x[i] : ~x[i]);
                        inc_parity(lit.var());
                        if (true || lvl(lit) == level) {
                            ++num_marks;
                        }
                        else {
                            m_parity_trail.push_back(lit);
                        }
                    }
                }
            }
            else {
                r.push_back(l);
            }
            while (num_marks > 0) {
                l = lits[index];
                v = l.var();
                unsigned n = get_parity(v);
                if (n > 0) {
                    reset_parity(v);
                    if (n > 1) {
                        IF_VERBOSE(2, verbose_stream() << "parity greater than 1: " << l << " " << n << "\n";);
                    }
                    if (n % 2 == 1) {
                        break;
                    }
                    IF_VERBOSE(2, verbose_stream() << "skip even parity: " << l << "\n";);
                    --num_marks;
                }
                --index;
            }
            if (num_marks == 0) {
                break;
            }
            --index;
            --num_marks;
        }

        // now walk the defined literals 

        for (unsigned i = 0; i < m_parity_trail.size(); ++i) {
            literal lit = m_parity_trail[i];
            if (get_parity(lit.var()) % 2 == 1) {
                r.push_back(lit);
            }
            else {
                IF_VERBOSE(2, verbose_stream() << "skip even parity: " << lit << "\n";);
            }
            reset_parity(lit.var());
        }
        m_parity_trail.reset();
    }

    void card_extension::get_antecedents(literal l, ext_justification_idx idx, literal_vector & r) {
        if (is_card_index(idx)) {
            card& c = index2card(idx);
        
            DEBUG_CODE(
                bool found = false;
                for (unsigned i = 0; !found && i < c.k(); ++i) {
                    found = c[i] == l;
                }
                SASSERT(found););
            
            if (c.lit() != null_literal) r.push_back(c.lit());
            SASSERT(c.lit() == null_literal || value(c.lit()) == l_true);
            for (unsigned i = c.k(); i < c.size(); ++i) {
                SASSERT(value(c[i]) == l_false);
                r.push_back(~c[i]);
            }
        }
        else {
            xor& x = index2xor(idx);
            if (x.lit() != null_literal) r.push_back(x.lit());
            TRACE("sat", display(tout << l << " ", x, true););
            SASSERT(x.lit() == null_literal || value(x.lit()) == l_true);
            SASSERT(x[0].var() == l.var() || x[1].var() == l.var());
            if (x[0].var() == l.var()) {
                SASSERT(value(x[1]) != l_undef);
                r.push_back(value(x[1]) == l_true ? x[1] : ~x[1]);                
            }
            else {
                SASSERT(value(x[0]) != l_undef);
                r.push_back(value(x[0]) == l_true ? x[0] : ~x[0]);                
            }
            for (unsigned i = 2; i < x.size(); ++i) {
                SASSERT(value(x[i]) != l_undef);
                r.push_back(value(x[i]) == l_true ? x[i] : ~x[i]);                
            }
        }
    }


    lbool card_extension::add_assign(card& c, literal alit) {
        // literal is assigned to false.        
        unsigned sz = c.size();
        unsigned bound = c.k();
        TRACE("sat", tout << "assign: " << c.lit() << ": " << ~alit << "@" << lvl(~alit) << "\n";);

        SASSERT(0 < bound && bound < sz);
        SASSERT(value(alit) == l_false);
        SASSERT(c.lit() == null_literal || value(c.lit()) == l_true);
        unsigned index = 0;
        for (index = 0; index <= bound; ++index) {
            if (c[index] == alit) {
                break;
            }
        }
        if (index == bound + 1) {
            // literal is no longer watched.
            return l_undef;
        }
        SASSERT(index <= bound);
        SASSERT(c[index] == alit);
        
        // find a literal to swap with:
        for (unsigned i = bound + 1; i < sz; ++i) {
            literal lit2 = c[i];
            if (value(lit2) != l_false) {
                c.swap(index, i);
                watch_literal(c, lit2);
                return l_undef;
            }
        }

        // conflict
        if (bound != index && value(c[bound]) == l_false) {
            TRACE("sat", tout << "conflict " << c[bound] << " " << alit << "\n";);
            set_conflict(c, alit);
            return l_false;
        }

        // TRACE("sat", tout << "no swap " << index << " " << alit << "\n";);
        // there are no literals to swap with,
        // prepare for unit propagation by swapping the false literal into 
        // position bound. Then literals in positions 0..bound-1 have to be
        // assigned l_true.
        if (index != bound) {
            c.swap(index, bound);
        }
        SASSERT(validate_unit_propagation(c));
        
        for (unsigned i = 0; i < bound && !s().inconsistent(); ++i) {
            assign(c, c[i]);
        }

        return s().inconsistent() ? l_false : l_true;
    }

    void card_extension::asserted(literal l) {
        bool_var v = l.var();
        if (s().inconsistent()) return;
        if (v >= m_var_infos.size()) return;
        var_info& vinfo = m_var_infos[v];
        ptr_vector<card>* cards = vinfo.m_card_watch[!l.sign()];
        card* crd = vinfo.m_card;
        xor* x = vinfo.m_xor;
        ptr_vector<xor>* xors = vinfo.m_xor_watch;

        if (!is_tag_empty(cards)) {
            ptr_vector<card>::iterator begin = cards->begin();
            ptr_vector<card>::iterator it = begin, it2 = it, end = cards->end();
            for (; it != end; ++it) {
                card& c = *(*it);
                if (c.lit() != null_literal && value(c.lit()) != l_true) {
                    continue;
                }
                switch (add_assign(c, ~l)) {
                case l_false: // conflict
                    for (; it != end; ++it, ++it2) {
                        *it2 = *it;
                    }
                    SASSERT(s().inconsistent());
                    cards->set_end(it2);
                    return;
                case l_undef: // watch literal was swapped
                    break;
                case l_true: // unit propagation, keep watching the literal
                    if (it2 != it) {
                        *it2 = *it;
                    }
                    ++it2;
                    break;
                }
            }
            cards->set_end(it2);
            if (cards->empty()) {
                m_var_infos[v].m_card_watch[!l.sign()] = set_tag_empty(cards);
            }
        }

        if (crd != 0 && !s().inconsistent()) {
            init_watch(*crd, !l.sign());
        }
        if (m_has_xor && !s().inconsistent()) {
            asserted_xor(l, xors, x);
        }
    }


    void card_extension::asserted_xor(literal l, ptr_vector<xor>* xors, xor* x) {
        TRACE("sat", tout << l << " " << !is_tag_empty(xors) << " " << (x != 0) << "\n";);
        if (!is_tag_empty(xors)) {
            ptr_vector<xor>::iterator begin = xors->begin();
            ptr_vector<xor>::iterator it = begin, it2 = it, end = xors->end();
            for (; it != end; ++it) {
                xor& c = *(*it);
                if (c.lit() != null_literal && value(c.lit()) != l_true) {
                    continue;
                }
                switch (add_assign(c, ~l)) {
                case l_false: // conflict
                    for (; it != end; ++it, ++it2) {
                        *it2 = *it;
                    }
                    SASSERT(s().inconsistent());
                    xors->set_end(it2);
                    return;
                case l_undef: // watch literal was swapped
                    break;
                case l_true: // unit propagation, keep watching the literal
                    if (it2 != it) {
                        *it2 = *it;
                    }
                    ++it2;
                    break;
                }
            }
            xors->set_end(it2);
            if (xors->empty()) {
                m_var_infos[l.var()].m_xor_watch = set_tag_empty(xors);
            }
        }

        if (x != 0 && !s().inconsistent()) {
            init_watch(*x, !l.sign());
        }
    }

    check_result card_extension::check() { return CR_DONE; }

    void card_extension::push() {
        m_var_lim.push_back(m_var_trail.size());
    }

    void card_extension::pop(unsigned n) {        
        TRACE("sat_verbose", tout << "pop:" << n << "\n";);
        unsigned new_lim = m_var_lim.size() - n;
        unsigned sz = m_var_lim[new_lim];
        while (m_var_trail.size() > sz) {
            bool_var v = m_var_trail.back();
            m_var_trail.pop_back();
            if (v != null_bool_var) {
                card* c = m_var_infos[v].m_card;
                clear_watch(*c);
                m_var_infos[v].m_card = 0;
                dealloc(c);
                xor* x = m_var_infos[v].m_xor;
                clear_watch(*x);
                m_var_infos[v].m_xor = 0;
                dealloc(x);
            }
        }
        m_var_lim.resize(new_lim);
        m_num_propagations_since_pop = 0;
    }

    void card_extension::simplify() {}
    void card_extension::clauses_modifed() {}
    lbool card_extension::get_phase(bool_var v) { return l_undef; }

    extension* card_extension::copy(solver* s) {
        card_extension* result = alloc(card_extension);
        result->set_solver(s);
        for (unsigned i = 0; i < m_cards.size(); ++i) {
            literal_vector lits;
            card& c = *m_cards[i];
            for (unsigned i = 0; i < c.size(); ++i) {
                lits.push_back(c[i]);
            }
            bool_var v = c.lit() == null_literal ? null_bool_var : c.lit().var();
            result->add_at_least(v, lits, c.k());
        }
        for (unsigned i = 0; i < m_xors.size(); ++i) {
            literal_vector lits;
            xor& x = *m_xors[i];
            for (unsigned i = 0; i < x.size(); ++i) {
                lits.push_back(x[i]);
            }
            bool_var v = x.lit() == null_literal ? null_bool_var : x.lit().var();
            result->add_xor(v, lits);
        }
        return result;
    }

    void card_extension::find_mutexes(literal_vector& lits, vector<literal_vector> & mutexes) {
        literal_set slits(lits);
        bool change = false;
        for (unsigned i = 0; i < m_cards.size(); ++i) {
            card& c = *m_cards[i];
            if (c.size() == c.k() + 1) {
                literal_vector mux;
                for (unsigned j = 0; j < c.size(); ++j) {
                    literal lit = ~c[j];
                    if (slits.contains(lit)) {
                        mux.push_back(lit);
                    }
                }
                if (mux.size() <= 1) {
                    continue;
                }

                for (unsigned j = 0; j < mux.size(); ++j) {
                    slits.remove(mux[j]);
                }
                change = true;
                mutexes.push_back(mux);
            }
        }        
        if (!change) return;
        literal_set::iterator it = slits.begin(), end = slits.end();
        lits.reset();
        for (; it != end; ++it) {
            lits.push_back(*it);
        }
    }

     void card_extension::display_watch(std::ostream& out, bool_var v, bool sign) const {
        card_watch const* w = m_var_infos[v].m_card_watch[sign];
        if (!is_tag_empty(w)) {
            card_watch const& wl = *w;
            out << literal(v, sign) << " |-> ";
            for (unsigned i = 0; i < wl.size(); ++i) {
                out << wl[i]->lit() << " ";
            }
            out << "\n";        
        }
    }

    void card_extension::display_watch(std::ostream& out, bool_var v) const {
        xor_watch const* w = m_var_infos[v].m_xor_watch;
        if (!is_tag_empty(w)) {
            xor_watch const& wl = *w;
            out << "v" << v << " |-> ";
            for (unsigned i = 0; i < wl.size(); ++i) {
                out << wl[i]->lit() << " ";
            }
            out << "\n";        
        }
    }

    void card_extension::display(std::ostream& out, ineq& ineq) const {
        for (unsigned i = 0; i < ineq.m_lits.size(); ++i) {
            out << ineq.m_coeffs[i] << "*" << ineq.m_lits[i] << " ";
        }
        out << ">= " << ineq.m_k << "\n";
    }

    void card_extension::display(std::ostream& out, xor& x, bool values) const {
        out << "xor " << x.lit();
        if (x.lit() != null_literal && values) {
            out << "@(" << value(x.lit());
            if (value(x.lit()) != l_undef) {
                out << ":" << lvl(x.lit());
            }
            out << "): ";
        }
        else {
            out << ": ";
        }
        for (unsigned i = 0; i < x.size(); ++i) {
            literal l = x[i];
            out << l;
            if (values) {
                out << "@(" << value(l);
                if (value(l) != l_undef) {
                    out << ":" << lvl(l);
                }
                out << ") ";
            }
            else {
                out << " ";
            }
        }        
        out << "\n";
    }

    void card_extension::display(std::ostream& out, card& c, bool values) const {
        out << c.lit() << "[" << c.size() << "]";
        if (c.lit() != null_literal && values) {
            out << "@(" << value(c.lit());
            if (value(c.lit()) != l_undef) {
                out << ":" << lvl(c.lit());
            }
            out << "): ";
        }
        else {
            out << ": ";
        }
        for (unsigned i = 0; i < c.size(); ++i) {
            literal l = c[i];
            out << l;
            if (values) {
                out << "@(" << value(l);
                if (value(l) != l_undef) {
                    out << ":" << lvl(l);
                }
                out << ") ";
            }
            else {
                out << " ";
            }
        }
        out << ">= " << c.k()  << "\n";
    }

    std::ostream& card_extension::display(std::ostream& out) const {
        for (unsigned vi = 0; vi < m_var_infos.size(); ++vi) {
            display_watch(out, vi, false);
            display_watch(out, vi, true);
            display_watch(out, vi);
        }
        for (unsigned vi = 0; vi < m_var_infos.size(); ++vi) {
            card* c = m_var_infos[vi].m_card;
            if (c) display(out, *c, false);
            xor* x = m_var_infos[vi].m_xor;
            if (x) display(out, *x, false);
        }
        return out;
    }

    std::ostream& card_extension::display_justification(std::ostream& out, ext_justification_idx idx) const {
        if (is_card_index(idx)) {
            card& c = index2card(idx);
            out << "bound " << c.lit() << ": ";
            for (unsigned i = 0; i < c.size(); ++i) {
                out << c[i] << " ";
            }
            out << ">= " << c.k();
        }
        else {
            xor& x = index2xor(idx);
            out << "xor " << x.lit() << ": ";
            for (unsigned i = 0; i < x.size(); ++i) {
                out << x[i] << " ";
            }            
        }
        return out;
    }

    void card_extension::collect_statistics(statistics& st) const {
        st.update("cardinality propagations", m_stats.m_num_propagations);
        st.update("cardinality conflicts", m_stats.m_num_conflicts);
    }

    bool card_extension::validate_conflict(card& c) { 
        if (!validate_unit_propagation(c)) return false;
        for (unsigned i = 0; i < c.k(); ++i) {
            if (value(c[i]) == l_false) return true;
        }
        return false;
    }
    bool card_extension::validate_conflict(xor& x) {
        return !parity(x, 0);
    }
    bool card_extension::validate_unit_propagation(card const& c) { 
        if (c.lit() != null_literal && value(c.lit()) != l_true) return false;
        for (unsigned i = c.k(); i < c.size(); ++i) {
            if (value(c[i]) != l_false) return false;
        }
        return true;
    }
    bool card_extension::validate_lemma() { 
        int val = -m_bound;
        normalize_active_coeffs();
        for (unsigned i = 0; i < m_active_vars.size(); ++i) {
            bool_var v = m_active_vars[i];
            int coeff = get_coeff(v);
            literal lit(v, false);
            SASSERT(coeff != 0);
            if (coeff < 0 && value(lit) != l_true) {
                val -= coeff;
            }
            else if (coeff > 0 && value(lit) != l_false) {
                val += coeff;
            }
        }
        return val < 0;
    }

    void card_extension::active2pb(ineq& p) {
        normalize_active_coeffs();
        p.reset(m_bound);
        for (unsigned i = 0; i < m_active_vars.size(); ++i) {
            bool_var v = m_active_vars[i];
            literal lit(v, get_coeff(v) < 0);
            p.m_lits.push_back(lit);
            p.m_coeffs.push_back(get_abs_coeff(v));
        }
    }

    void card_extension::justification2pb(justification const& js, literal lit, unsigned offset, ineq& p) {
        switch (js.get_kind()) {
        case justification::NONE:
            p.reset(offset);
            p.push(lit, offset);
            break;
        case justification::BINARY:
            p.reset(offset);
            p.push(lit, offset);
            p.push(js.get_literal(), offset);
            break;
        case justification::TERNARY:
            p.reset(offset);
            p.push(lit, offset);
            p.push(js.get_literal1(), offset);
            p.push(js.get_literal2(), offset);
            break;
        case justification::CLAUSE: {
            p.reset(offset);
            clause & c = *(s().m_cls_allocator.get_clause(js.get_clause_offset()));
            unsigned sz  = c.size();
            for (unsigned i = 0; i < sz; i++)
                p.push(c[i], offset);
            break;
        }
        case justification::EXT_JUSTIFICATION: {
            unsigned index = js.get_ext_justification_idx();
            if (is_card_index(index)) {
                card& c = index2card(index);
                p.reset(offset*c.k());
                for (unsigned i = 0; i < c.size(); ++i) {
                    p.push(c[i], offset);
                }
                if (c.lit() != null_literal) p.push(~c.lit(), offset*c.k());
            }
            else {
                literal_vector ls;
                get_antecedents(lit, index, ls);                
                p.reset(offset);
                for (unsigned i = 0; i < ls.size(); ++i) {
                    p.push(~ls[i], offset);
                }
                literal lxor = index2xor(index).lit();                
                if (lxor != null_literal) p.push(~lxor, offset);
            }
            break;
        }
        default:
            UNREACHABLE();
            break;
        }
    }


    // validate that m_A & m_B implies m_C

    bool card_extension::validate_resolvent() {
        u_map<unsigned> coeffs;
        unsigned k = m_A.m_k + m_B.m_k;
        for (unsigned i = 0; i < m_A.m_lits.size(); ++i) {
            unsigned coeff = m_A.m_coeffs[i];
            SASSERT(!coeffs.contains(m_A.m_lits[i].index()));
            coeffs.insert(m_A.m_lits[i].index(), coeff);
        }
        for (unsigned i = 0; i < m_B.m_lits.size(); ++i) {
            unsigned coeff1 = m_B.m_coeffs[i], coeff2;
            literal lit = m_B.m_lits[i];
            if (coeffs.find((~lit).index(), coeff2)) {
                if (coeff1 == coeff2) {
                    coeffs.remove((~lit).index());
                    k += coeff1;
                }
                else if (coeff1 < coeff2) {
                    coeffs.insert((~lit).index(), coeff2 - coeff1);
                    k += coeff1;
                }
                else {
                    SASSERT(coeff2 < coeff1);
                    coeffs.remove((~lit).index());
                    coeffs.insert(lit.index(), coeff1 - coeff2);
                    k += coeff2;
                }
            }
            else if (coeffs.find(lit.index(), coeff2)) {
                coeffs.insert(lit.index(), coeff1 + coeff2);
            }
            else {
                coeffs.insert(lit.index(), coeff1);
            }
        }
        // C is above the sum of A and B
        for (unsigned i = 0; i < m_C.m_lits.size(); ++i) {
            literal lit = m_C.m_lits[i];
            unsigned coeff;
            if (coeffs.find(lit.index(), coeff)) {
                if (coeff > m_C.m_coeffs[i] && m_C.m_coeffs[i] < m_C.m_k) {
                    std::cout << i << ": " << m_C.m_coeffs[i] << " " << m_C.m_k << "\n";
                    goto violated;
                }
                coeffs.remove(lit.index());
            }
        }
        if (!coeffs.empty()) goto violated;
        if (m_C.m_k > k) goto violated;
        SASSERT(coeffs.empty());
        SASSERT(m_C.m_k <= k);
        return true;

    violated:
        display(std::cout, m_A);
        display(std::cout, m_B);
        display(std::cout, m_C);
        u_map<unsigned>::iterator it = coeffs.begin(), end = coeffs.end();
        for (; it != end; ++it) {
            std::cout << to_literal(it->m_key) << ": " << it->m_value << "\n";
        }
        
        return false;
    }

    bool card_extension::validate_conflict(literal_vector const& lits, ineq& p) { 
        for (unsigned i = 0; i < lits.size(); ++i) {
            if (value(lits[i]) != l_false) {
                TRACE("sat", tout << "literal " << lits[i] << " is not false\n";);
                return false;
            }
        }
        unsigned value = 0;
        for (unsigned i = 0; i < p.m_lits.size(); ++i) {
            unsigned coeff = p.m_coeffs[i];
            if (!lits.contains(p.m_lits[i])) {
                value += coeff;
            }
        }
        CTRACE("sat", value >= p.m_k, tout << "slack: " << value << " bound " << p.m_k << "\n";
               display(tout, p);
               tout << lits << "\n";);
        return value < p.m_k;
    }
    
};

