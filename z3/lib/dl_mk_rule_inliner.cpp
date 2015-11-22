/*++
Copyright (c) 2006 Microsoft Corporation

Module Name:

    dl_mk_rule_inliner.cpp

Abstract:

    Rule transformer which simplifies interpreted tails

Author:

    Krystof Hoder (t-khoder) 2011-10-01.

Revision History:

    Added linear_inline 2012-9-10 (nbjorner)

    
Notes:

Resolution transformation (resolve):

    P(x) :- Q(y), phi(x,y)      Q(y) :- R(z), psi(y,z)
    --------------------------------------------------
             P(x) :- R(z), phi(x,y), psi(y,z)

    Proof converter: 

       replace assumption (*) by rule and upper assumptions.


Subsumption transformation (remove rule):

    P(x) :- Q(y), phi(x,y)      Rules
    ---------------------------------
    Rules
 
    
    Model converter: 

       P(x) := P(x) or (exists y . Q(y) & phi(x,y))


--*/


#include <sstream>
#include "ast_pp.h"
#include "dl_finite_product_relation.h"
#include "dl_product_relation.h"
#include "dl_sieve_relation.h"
#include "rewriter.h"
#include "rewriter_def.h"
#include "dl_mk_rule_inliner.h"

namespace datalog {

    // -----------------------------------
    //
    // mk_rule_inliner::rule_unifier
    //
    // -----------------------------------

    bool rule_unifier::unify_rules(const rule& tgt, unsigned tgt_idx, const rule& src) {
        var_counter& vc = m_rm.get_var_counter();
        unsigned var_cnt = std::max(vc.get_max_var(tgt), vc.get_max_var(src))+1;
        m_subst.reset();
        m_subst.reserve(2, var_cnt);
        
        m_ready = m_unif(tgt.get_tail(tgt_idx), src.get_head(), m_subst);

        if (m_ready) {
            m_deltas[0] = 0;
            m_deltas[1] = var_cnt;
            TRACE("dl", 
                  output_predicate(m_context, src.get_head(), tout << "unify rules "); 
                  output_predicate(m_context, tgt.get_head(), tout << "\n"); 
                  tout << "\n";);
        }
        return m_ready;
    }

    void rule_unifier::apply(app * a, bool is_tgt, app_ref& res) {
        expr_ref res_e(m);
        TRACE("dl", output_predicate(m_context, a, tout); tout << "\n";);
        m_subst.apply(2, m_deltas, expr_offset(a, is_tgt ? 0 : 1), res_e);
        SASSERT(is_app(res_e.get()));
        res = to_app(res_e.get());
    }

    void rule_unifier::apply(
        rule& r, bool is_tgt, unsigned skipped_index, 
        app_ref_vector& res, svector<bool>& res_neg) {
        unsigned rule_len = r.get_tail_size();
        for (unsigned i = 0; i < rule_len; i++) {
            if (i != skipped_index) { //i can never be UINT_MAX, so we'll never skip if we're not supposed to
                app_ref new_tail_el(m);
                apply(r.get_tail(i), is_tgt, new_tail_el);
                res.push_back(new_tail_el);
                res_neg.push_back(r.is_neg_tail(i));
            }
        }
    }

    bool rule_unifier::apply(rule& tgt, unsigned tail_index, rule& src, rule_ref& res) {
        SASSERT(m_ready);
        app_ref new_head(m);
        app_ref_vector tail(m);
        svector<bool> tail_neg;
        rule_ref simpl_rule(m_rm);
        apply(tgt.get_head(), true, new_head);
        apply(tgt, true,  tail_index, tail, tail_neg);
        apply(src, false, UINT_MAX,   tail, tail_neg);
        mk_rule_inliner::remove_duplicate_tails(tail, tail_neg);
        SASSERT(tail.size()==tail_neg.size());
        res = m_rm.mk(new_head, tail.size(), tail.c_ptr(), tail_neg.c_ptr());
        res->set_accounting_parent_object(m_context, &tgt);
        res->norm_vars(m_rm);
        if (m_context.fix_unbound_vars()) {
            m_rm.fix_unbound_vars(res, true);
        }
        if (m_interp_simplifier.transform_rule(res.get(), simpl_rule)) {
            res = simpl_rule;
            return true;
        }
        else {
            return false;
        }
    }

    expr_ref_vector rule_unifier::get_rule_subst(const rule& r, bool is_tgt) {
        SASSERT(m_ready);
        expr_ref_vector result(m);
        sort_ref_vector sorts(m);
        expr_ref v(m), w(m);
        r.get_vars(sorts);
        for (unsigned i = 0; i < sorts.size(); ++i) {
            if (!sorts[i].get()) {
                sorts[i] = m.mk_bool_sort();
            }
            v = m.mk_var(i, sorts[i].get());
            m_subst.apply(2, m_deltas, expr_offset(v, is_tgt?0:1), w);
            result.push_back(w);            
        }        
        return result;
    }


    // -----------------------------------
    //
    // mk_rule_inliner
    //
    // -----------------------------------

    /**
       Inline occurrences of rule src at tail_index in tgt and return the result in res.
    */
    bool mk_rule_inliner::try_to_inline_rule(rule& tgt, rule& src, unsigned tail_index, rule_ref& res)
    {
        SASSERT(tail_index<tgt.get_positive_tail_size());
        SASSERT(!tgt.is_neg_tail(tail_index));

        tgt.norm_vars(m_context.get_rule_manager());

        if (has_quantifier(src)) {
            return false;
        }

        if (!m_unifier.unify_rules(tgt, tail_index, src)) {
            return false;
        }

        if (m_unifier.apply(tgt, tail_index, src, res)) {
            TRACE("dl",
                  tgt.display(m_context,  tout << "tgt (" << tail_index << "): \n");
                  src.display(m_context,  tout << "src:\n");
                  res->display(m_context, tout << "res\n");
                  //m_unifier.display(tout << "subst:\n");
                  );
            if (m_pc) {
                expr_ref_vector s1 = m_unifier.get_rule_subst(tgt, true);
                expr_ref_vector s2 = m_unifier.get_rule_subst(src, false);
                datalog::resolve_rule(m_pc, tgt, src, tail_index, s1, s2, *res.get());
            }
            return true;        
        }
        else {
            TRACE("dl", res->display(m_context, tout << "interpreted tail is unsat\n"););
            //the interpreted part is unsatisfiable
            return false;
        }
    }

    bool mk_rule_inliner::has_quantifier(rule const& r) const {
        unsigned utsz = r.get_uninterpreted_tail_size();
        for (unsigned i = utsz; i < r.get_tail_size(); ++i) {
            if (r.get_tail(i)->has_quantifiers()) return true;
        }
        return false;
    }

    void mk_rule_inliner::count_pred_occurrences(rule_set const & orig)
    {
        m_context.get_rmanager().collect_non_empty_predicates(m_preds_with_facts);

        rule_set::iterator rend = orig.end();
        for (rule_set::iterator rit = orig.begin(); rit!=rend; ++rit) {
            rule * r = *rit;
            func_decl * head_pred = r->get_decl();
            m_head_pred_ctr.inc(head_pred);

            if (r->get_tail_size()>0) {
                m_head_pred_non_empty_tails_ctr.inc(head_pred);
            }

            unsigned ut_len = r->get_uninterpreted_tail_size();
            for (unsigned i=0; i<ut_len; i++) {
                func_decl * pred = r->get_decl(i);
                m_tail_pred_ctr.inc(pred);

                if (r->is_neg_tail(i)) {
                    m_preds_with_neg_occurrence.insert(pred);
                }
            }
        }
    }

    bool mk_rule_inliner::inlining_allowed(func_decl * pred)
    {
        if (//these three conditions are important for soundness
            m_context.is_output_predicate(pred) ||
            m_preds_with_facts.contains(pred) ||
            m_preds_with_neg_occurrence.contains(pred) ||
            //this condition is used for breaking of cycles among inlined rules
            m_forbidden_preds.contains(pred)) {
            return false;
        }

        //these conditions are optional, they avoid possible exponential increase 
        //in the size of the problem

        return 
            //m_head_pred_non_empty_tails_ctr.get(pred)<=1
            m_head_pred_ctr.get(pred) <= 1
            || (m_tail_pred_ctr.get(pred) <= 1 && m_head_pred_ctr.get(pred) <= 4)
            ;
    }

    /** Caller has to dealloc the returned object */
    rule_set * mk_rule_inliner::create_allowed_rule_set(rule_set const & orig) 
    {
        rule_set * res = alloc(rule_set, m_context);
        unsigned rcnt = orig.get_num_rules();
        for (unsigned i=0; i<rcnt; i++) {
            rule * r = orig.get_rule(i);
            if (inlining_allowed(r->get_decl())) {
                res->add_rule(r);
            }
        }
        //the rule set should be stratified, since orig (which is its superset) is as well
        VERIFY(res->close());
        return res;
    }

    /**
    Try to make the set of inlined predicates acyclic by forbidding inlining of one
    predicate from each strongly connected component. Return true if we did forbide some 
    predicate, and false if the set of rules is already acyclic.
    */
    bool mk_rule_inliner::forbid_preds_from_cycles(rule_set const & r)
    {
        SASSERT(r.is_closed());

        bool something_forbidden = false;
        
        const rule_stratifier::comp_vector& comps = r.get_stratifier().get_strats();

        rule_stratifier::comp_vector::const_iterator cend = comps.end();
        for (rule_stratifier::comp_vector::const_iterator it = comps.begin(); it!=cend; ++it) {
            rule_stratifier::item_set * stratum = *it;
            if (stratum->size()==1) {
                continue;
            }
            SASSERT(stratum->size()>1);
            func_decl * first_stratum_pred = *stratum->begin();

            //we're trying to break cycles by removing one predicate from each of them
            m_forbidden_preds.insert(first_stratum_pred);
            something_forbidden = true;
        }
        return something_forbidden;
    }

    bool mk_rule_inliner::forbid_multiple_multipliers(const rule_set & orig, 
            rule_set const & proposed_inlined_rules) {

        bool something_forbidden = false;

        const rule_stratifier::comp_vector& comps = 
            proposed_inlined_rules.get_stratifier().get_strats();

        rule_stratifier::comp_vector::const_iterator cend = comps.end();
        for (rule_stratifier::comp_vector::const_iterator it = comps.begin(); it!=cend; ++it) {
            rule_stratifier::item_set * stratum = *it;

            SASSERT(stratum->size()==1);
            func_decl * head_pred = *stratum->begin();

            bool is_multi_head_pred = m_head_pred_ctr.get(head_pred)>1;
            bool is_multi_occurrence_pred = m_tail_pred_ctr.get(head_pred)>1;

            const rule_vector& pred_rules = proposed_inlined_rules.get_predicate_rules(head_pred);
            rule_vector::const_iterator iend = pred_rules.end();
            for (rule_vector::const_iterator iit = pred_rules.begin(); iit!=iend; ++iit) {
                rule * r = *iit;

                unsigned pt_len = r->get_positive_tail_size();
                for (unsigned ti = 0; ti<pt_len; ++ti) {
                    func_decl * tail_pred = r->get_decl(ti);
                    if (!inlining_allowed(tail_pred)) {
                        continue;
                    }
                    unsigned tail_pred_head_cnt = m_head_pred_ctr.get(tail_pred);
                    if (tail_pred_head_cnt<=1) {
                        continue;
                    }
                    if (is_multi_head_pred) {
                        m_forbidden_preds.insert(head_pred);
                        something_forbidden = true;
                        goto process_next_pred;
                    }
                    if (is_multi_occurrence_pred) {
                        m_forbidden_preds.insert(tail_pred);
                        something_forbidden = true;
                    }
                    else {
                        is_multi_head_pred = true;
                        m_head_pred_ctr.get(head_pred) = 
                            m_head_pred_ctr.get(head_pred)*tail_pred_head_cnt;
                    }
                }

            }

        process_next_pred:;
        }


        unsigned rule_cnt = orig.get_num_rules();
        for (unsigned ri=0; ri<rule_cnt; ri++) {
            rule * r = orig.get_rule(ri);

            func_decl * head_pred = r->get_decl();

            if (inlining_allowed(head_pred)) {
                //we have already processed inlined rules
                continue;
            }

            bool has_multi_head_pred = false;
            unsigned pt_len = r->get_positive_tail_size();
            for (unsigned ti = 0; ti<pt_len; ++ti) {
                func_decl * pred = r->get_decl(ti);
                if (!inlining_allowed(pred)) {
                    continue;
                }
                if (m_head_pred_ctr.get(pred)<=1) {
                    continue;
                }
                if (has_multi_head_pred) {
                    m_forbidden_preds.insert(pred);
                    something_forbidden = true;
                }
                else {
                    has_multi_head_pred = true;
                }
            }
        }
        return something_forbidden;
    }

    void mk_rule_inliner::plan_inlining(rule_set const & orig)
    {
        count_pred_occurrences(orig);
        
        scoped_ptr<rule_set> candidate_inlined_set = create_allowed_rule_set(orig);
        while (forbid_preds_from_cycles(*candidate_inlined_set)) {
            candidate_inlined_set = create_allowed_rule_set(orig);
        }

        if (forbid_multiple_multipliers(orig, *candidate_inlined_set)) {
            candidate_inlined_set = create_allowed_rule_set(orig);
        }

        TRACE("dl", tout<<"rules to be inlined:\n" << (*candidate_inlined_set); );

        // now we start filling in the set of the inlined rules in a topological order,
        // so that we inline rules into other rules

        SASSERT(m_inlined_rules.get_num_rules()==0);

        const rule_stratifier::comp_vector& comps = candidate_inlined_set->get_stratifier().get_strats();

        rule_stratifier::comp_vector::const_iterator cend = comps.end();
        for (rule_stratifier::comp_vector::const_iterator it = comps.begin(); it!=cend; ++it) {
            rule_stratifier::item_set * stratum = *it;
            SASSERT(stratum->size()==1);
            func_decl * pred = *stratum->begin();

            const rule_vector& pred_rules = candidate_inlined_set->get_predicate_rules(pred);
            rule_vector::const_iterator iend = pred_rules.end();
            for (rule_vector::const_iterator iit = pred_rules.begin(); iit!=iend; ++iit) {
                transform_rule(*iit, m_inlined_rules);
            }
        }

        TRACE("dl", tout << "inlined rules after mutual inlining:\n" << m_inlined_rules;  );
    }

    bool mk_rule_inliner::transform_rule(rule * r0, rule_set& tgt) {
        bool modified = false;
        rule_ref_vector todo(m_rm);

        todo.push_back(r0);



        while (!todo.empty()) {
            rule_ref r(todo.back(), m_rm);
            todo.pop_back();
            unsigned pt_len = r->get_positive_tail_size();

            unsigned i = 0;

            for  (; i < pt_len && !inlining_allowed(r->get_decl(i)); ++i) {};

            if (has_quantifier(*r.get())) {
                continue;
            }

            if (i == pt_len) {
                //there's nothing we can inline in this rule
                tgt.add_rule(r);
                continue;
            }
            modified = true;

            func_decl * pred = r->get_decl(i);
            const rule_vector& pred_rules = m_inlined_rules.get_predicate_rules(pred);
            rule_vector::const_iterator iend = pred_rules.end();
            for (rule_vector::const_iterator iit = pred_rules.begin(); iit!=iend; ++iit) {
                rule * inl_rule = *iit;
                rule_ref inl_result(m_rm);
                if (try_to_inline_rule(*r.get(), *inl_rule, i, inl_result)) {
                    todo.push_back(inl_result);
                }
            }
        }
        return modified;
    }

    bool mk_rule_inliner::transform_rules(const rule_set & orig, rule_set & tgt) {

        bool something_done = false;

        rule_set::iterator rend = orig.end();
        for (rule_set::iterator rit = orig.begin(); rit!=rend; ++rit) {
            rule_ref r(*rit, m_rm);
            func_decl * pred = r->get_decl();

            // if inlining is allowed, then we are eliminating 
            // this relation through inlining, 
            // so we don't add its rules to the result

            something_done |= !inlining_allowed(pred) && transform_rule(r, tgt);
        }
        
        return something_done;
    }

    /**
       Check whether rule r is oriented in a particular ordering.
       This is to avoid infinite cycle of inlining in the eager inliner.
       
       Out ordering is lexicographic, comparing atoms first on stratum they are in,
       then on arity and then on ast ID of their func_decl.
    */
    bool mk_rule_inliner::is_oriented_rewriter(rule * r, rule_stratifier const& strat) {
        func_decl * head_pred = r->get_decl();
        unsigned head_strat = strat.get_predicate_strat(head_pred);

        unsigned head_arity = head_pred->get_arity();

        //var_idx_set head_vars;
        //var_idx_set same_strat_vars;
        //collect_vars(m, r->get_head(), head_vars);

        unsigned pt_len = r->get_positive_tail_size();
        for (unsigned ti=0; ti<pt_len; ++ti) {
            
            func_decl * pred = r->get_decl(ti);

            unsigned pred_strat = strat.get_predicate_strat(pred);
            SASSERT(pred_strat<=head_strat);

            if (pred_strat==head_strat) {
                //collect_vars(m, r->get_head(), same_strat_vars);
                if (pred->get_arity()>head_arity
                    || (pred->get_arity()==head_arity && pred->get_id()>=head_pred->get_id()) ) {
                    return false;
                }
            }
        }
        return true;
    }


    bool mk_rule_inliner::do_eager_inlining(rule * r, rule_set const& rules, rule_ref& res) {

        SASSERT(rules.is_closed());
        const rule_stratifier& strat = rules.get_stratifier();

        func_decl * head_pred = r->get_decl();

        unsigned pt_len = r->get_positive_tail_size();
        for (unsigned ti = 0; ti < pt_len; ++ti) {
            
            func_decl * pred = r->get_decl(ti);
            if (pred == head_pred || m_preds_with_facts.contains(pred)) { continue; }


            const rule_vector& pred_rules = rules.get_predicate_rules(pred);
            rule * inlining_candidate = 0;
            unsigned rule_cnt = pred_rules.size();
            if (rule_cnt == 0) {
                inlining_candidate = 0;
            }
            else if (rule_cnt == 1) {
                inlining_candidate = pred_rules[0];
            }
            else {
                inlining_candidate = 0;
                
                for (unsigned ri = 0; ri < rule_cnt; ++ri) {
                    rule * pred_rule = pred_rules[ri];
                    if (!m_unifier.unify_rules(*r, ti, *pred_rule)) {
                        //we skip rules which don't unify with the tail atom
                        continue;
                    }
                    if (inlining_candidate != 0) {
                        // We have two rules that can be inlined into the current 
                        // tail predicate. In this situation we don't do inlinning
                        // on this tail atom, as we don't want the overall number 
                        // of rules to increase.
                        goto process_next_tail;
                    }
                    inlining_candidate = pred_rule;
                }
            }
            if (inlining_candidate == 0) {
                // nothing unifies with the tail atom, therefore the rule is unsatisfiable
                // (we can say this because relation pred doesn't have any ground facts either)
                res = 0;
                datalog::del_rule(m_mc, *r);
                return true;
            }
            if (!is_oriented_rewriter(inlining_candidate, strat)) {
                // The rule which should be used for inlining isn't oriented
                // in a simplifying direction. Inlining with such rule might lead to
                // infinite loops, so we don't do it.
                goto process_next_tail;
            }
            if (!try_to_inline_rule(*r, *inlining_candidate, ti, res)) {
                datalog::del_rule(m_mc, *r);
                res = 0;
            }
            return true;

        process_next_tail:;
        }
        return false;
    }

    bool mk_rule_inliner::do_eager_inlining(scoped_ptr<rule_set> & rules) {
        scoped_ptr<rule_set> res = alloc(rule_set, m_context);
        bool done_something = false;

        rule_set::iterator rend = rules->end();
        for (rule_set::iterator rit = rules->begin(); rit!=rend; ++rit) {
            rule_ref r(*rit, m_rm);

            rule_ref replacement(m_rm);
            while (r && do_eager_inlining(r, *rules, replacement)) {
                r = replacement;
                done_something = true;
            }

            if (!r) {
                continue;
            }
            res->add_rule(r);
        }
        if (done_something) {
            rules = res.detach();
        }
        return done_something;
    }

    /**
       Inline predicates that are known to not be join-points.

          P(1,x) :- P(0,y), phi(x,y)
          P(0,x) :- P(1,z), psi(x,z)

       ->

          P(1,x) :- P(1,z), phi(x,y), psi(y,z)

       whenever P(0,x) is not unifiable with the 
       body of the rule where it appears (P(1,z))
       and P(0,x) is unifiable with at most one (?) 
       other rule (and it does not occur negatively).
     */
    bool mk_rule_inliner::visitor::operator()(expr* e) {
        m_unifiers.append(m_positions.find(e));
        TRACE("dl", 
              tout << "unifier: " << (m_unifiers.empty()?0:m_unifiers.back());
              tout << " num unifiers: " << m_unifiers.size();
              tout << " num positions: " << m_positions.find(e).size() << "\n";
              output_predicate(m_context, to_app(e), tout); tout << "\n";);
        return true;
    }

    void mk_rule_inliner::visitor::reset(unsigned sz) {
        m_unifiers.reset();
        m_can_remove.reset();
        m_can_remove.resize(sz, true);
        m_can_expand.reset();
        m_can_expand.resize(sz, true);
        m_positions.reset();
    }

    unsigned_vector const& mk_rule_inliner::visitor::add_position(expr* e, unsigned j) {
        obj_map<expr, unsigned_vector>::obj_map_entry * et = m_positions.insert_if_not_there2(e, unsigned_vector());
        et->get_data().m_value.push_back(j);
        return et->get_data().m_value;
    }

    unsigned_vector const& mk_rule_inliner::visitor::del_position(expr* e, unsigned j) {
        obj_map<expr, unsigned_vector>::obj_map_entry * et = m_positions.find_core(e);        
        SASSERT(et && et->get_data().m_value.contains(j));
        et->get_data().m_value.erase(j);
        return et->get_data().m_value;
    }

    void mk_rule_inliner::add_rule(rule* r, unsigned i) {
        svector<bool>& can_remove = m_head_visitor.can_remove();
        svector<bool>& can_expand = m_head_visitor.can_expand();
        app* head = r->get_head();
        func_decl* headd = head->get_decl();
        m_head_visitor.add_position(head, i);
        m_head_index.insert(head);
        m_pinned.push_back(r);
            
        if (m_context.is_output_predicate(headd) ||
            m_preds_with_facts.contains(headd)) {
            can_remove.set(i, false);
            TRACE("dl", output_predicate(m_context, head, tout << "cannot remove: " << i << " "); tout << "\n";);
        }

        unsigned tl_sz = r->get_uninterpreted_tail_size();
        for (unsigned j = 0; j < tl_sz; ++j) {
            app* tail = r->get_tail(j);
            m_tail_visitor.add_position(tail, i);
            m_tail_index.insert(tail);
        }
        bool can_exp = 
            tl_sz == 1
            && r->get_positive_tail_size() == 1 
            && !m_preds_with_facts.contains(r->get_decl(0)) 
            && !m_context.is_output_predicate(r->get_decl(0));
        can_expand.set(i, can_exp);
    }

    void mk_rule_inliner::del_rule(rule* r, unsigned i) {
        app* head = r->get_head();
        m_head_visitor.del_position(head, i);
        unsigned tl_sz = r->get_uninterpreted_tail_size();
        for (unsigned j = 0; j < tl_sz; ++j) {
            app* tail = r->get_tail(j);
            m_tail_visitor.del_position(tail, i);
        }        
    }


#define PRT(_x_) ((_x_)?"T":"F")

    bool mk_rule_inliner::inline_linear(scoped_ptr<rule_set>& rules) {
        scoped_ptr<rule_set> res = alloc(rule_set, m_context);
        bool done_something = false;        
        unsigned sz = rules->get_num_rules();

        m_head_visitor.reset(sz);
        m_tail_visitor.reset(sz);
        m_head_index.reset();
        m_tail_index.reset();

        TRACE("dl", rules->display(tout););

        rule_ref_vector acc(m_rm);
        for (unsigned i = 0; i < sz; ++i) {
            acc.push_back(rules->get_rule(i));
        }

        // set up unification index. 
        svector<bool>& can_remove = m_head_visitor.can_remove();
        svector<bool>& can_expand = m_head_visitor.can_expand();

        for (unsigned i = 0; i < sz; ++i) {
            add_rule(acc[i].get(), i);
        }

        // initialize substitution.
        var_counter& vc = m_rm.get_var_counter();
        unsigned max_var = 0;
        for (unsigned i = 0; i < sz; ++i) {
            rule* r = acc[i].get();
            max_var = std::max(max_var, vc.get_max_var(r->get_head()));
            unsigned tl_sz = r->get_uninterpreted_tail_size();
            for (unsigned j = 0; j < tl_sz; ++j) {
                max_var = std::max(max_var, vc.get_max_var(r->get_tail(j)));
            }
        }
        m_subst.reset();
        m_subst.reserve_vars(max_var+1);
        m_subst.reserve_offsets(std::max(m_tail_index.get_approx_num_regs(), m_head_index.get_approx_num_regs()));

        svector<bool> valid;
        valid.reset();
        valid.resize(sz, true);        

        params_ref const& params = m_context.get_params();
        bool allow_branching = params.get_bool(":inline-linear-branch", false);

        for (unsigned i = 0; i < sz; ++i) {

            while (true) {

                rule_ref r(acc[i].get(), m_rm);
                
                TRACE("dl", r->display(m_context, tout << "processing: " << i << "\n"););
                
                if (!valid.get(i)) {
                    TRACE("dl", tout << "invalid: " << i << "\n";);
                    break;
                }
                if (!can_expand.get(i)) {
                    TRACE("dl", tout << "cannot expand: " << i << "\n";);
                    break;
                }

                m_head_visitor.reset();
                m_head_index.unify(r->get_tail(0), m_head_visitor);
                unsigned num_head_unifiers = m_head_visitor.get_unifiers().size();
                if (num_head_unifiers != 1) {
                    TRACE("dl", tout << "no unique unifier " << num_head_unifiers << "\n";);
                    break;
                }
                unsigned j = m_head_visitor.get_unifiers()[0];
                if (!can_remove.get(j) || !valid.get(j) || i == j) {
                    TRACE("dl", tout << PRT(can_remove.get(j)) << " " << PRT(valid.get(j)) << " " << PRT(i != j) << "\n";);
                    break;
                }
                
                rule* r2 = acc[j].get();
               
                // check that the head of r2 only unifies with this single body position.
                TRACE("dl", output_predicate(m_context, r2->get_head(), tout << "unify head: "); tout << "\n";);
                m_tail_visitor.reset();
                m_tail_index.unify(r2->get_head(), m_tail_visitor);
                unsigned_vector const& tail_unifiers = m_tail_visitor.get_unifiers();
                unsigned num_tail_unifiers = tail_unifiers.size();
                SASSERT(!tail_unifiers.empty());
                if (!allow_branching && num_tail_unifiers != 1) {
                    TRACE("dl", tout << "too many tails " << num_tail_unifiers << "\n";);
                    break;
                }
                
                rule_ref rl_res(m_rm);
                if (!try_to_inline_rule(*r.get(), *r2, 0, rl_res)) {
                    TRACE("dl", r->display(m_context, tout << "inlining failed\n"); r2->display(m_context, tout);  );
                    break;
                }
                done_something = true;
                TRACE("dl", r->display(m_context, tout); r2->display(m_context, tout); rl_res->display(m_context, tout); );

                del_rule(r, i);
                add_rule(rl_res.get(), i);
                

                r = rl_res;
                acc[i] = r.get();
                can_expand.set(i, can_expand.get(j));
                
                if (num_tail_unifiers == 1) {
                    TRACE("dl", tout << "setting invalid: " << j << "\n";);
                    valid.set(j, false);
                    datalog::del_rule(m_mc, *r2);
                    del_rule(r2, j);
                }

                max_var = std::max(max_var, vc.get_max_var(*r.get()));
                m_subst.reserve_vars(max_var+1);

            }
        }
        if (done_something) {
            rules = alloc(rule_set, m_context);
            for (unsigned i = 0; i < sz; ++i) {
                if (valid.get(i)) {
                    rules->add_rule(acc[i].get());
                }
            }
            TRACE("dl", rules->display(tout););
        }        
        return done_something;
    }

    rule_set * mk_rule_inliner::operator()(rule_set const & source, model_converter_ref& mc, proof_converter_ref& pc) {

        bool something_done = false;
        ref<horn_subsume_model_converter> hsmc;        
        ref<replace_proof_converter> hpc;

        if (source.get_num_rules() == 0) {
            return 0;
        }

        if (mc) {
            hsmc = alloc(horn_subsume_model_converter, m);
        }
        if (pc) {
            hpc = alloc(replace_proof_converter, m);
        }
        m_mc = hsmc.get();
        m_pc = hpc.get();

        plan_inlining(source);

        scoped_ptr<rule_set> res = alloc(rule_set, m_context);

        something_done = transform_rules(source, *res);

        VERIFY(res->close()); //this transformation doesn't break the negation stratification

        // try eager inlining
        if (do_eager_inlining(res)) {
            something_done = true;
        }

        params_ref const& params = m_context.get_params();
            if (params.get_bool(":inline-linear", true) && inline_linear(res)) {
            something_done = true;
        }

        if (!something_done) {
            res = 0;
        }
        else {
            if (mc) {
                mc = concat(mc.get(), hsmc.get());
            }
            if (pc) {
                pc = concat(pc.get(), hpc.get());
            }
        }

        return res.detach();
    }
  
};

