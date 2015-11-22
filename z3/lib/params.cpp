/*++
Copyright (c) 2011 Microsoft Corporation

Module Name:

    params.cpp

Abstract:

    Parameters

Author:

    Leonardo (leonardo) 2011-05-09

Notes:

--*/
#include"params.h"
#include"ast.h"
#include"rational.h"
#include"symbol.h"
#include"dictionary.h"

struct param_descrs::imp {
    typedef std::pair<param_kind, char const *> info;
    dictionary<info> m_info;
    svector<symbol> m_names;

    void insert(symbol const & name, param_kind k, char const * descr) {
        SASSERT(!name.is_numerical());
        info i;
        if (m_info.find(name, i)) {
            SASSERT(i.first == k);
            return;
        }
        m_info.insert(name, info(k, descr));
        m_names.push_back(name);
    }

    void erase(symbol const & name) {
        m_info.erase(name);
    }
                                    
    param_kind get_kind(symbol const & name) const {
        info i;
        if (m_info.find(name, i))
            return i.first;
        return CPK_INVALID;
    }

    unsigned size() const {
        return m_names.size();
    }
    
    symbol get_param_name(unsigned idx) const {
        return m_names[idx];
    }

    struct lt {
        bool operator()(symbol const & s1, symbol const & s2) const { return strcmp(s1.bare_str(), s2.bare_str()) < 0; }
    };

    void display(std::ostream & out, unsigned indent) const {
        svector<symbol> names;
        dictionary<info>::iterator it  = m_info.begin();
        dictionary<info>::iterator end = m_info.end();
        for (; it != end; ++it) {
            names.push_back(it->m_key);
        }
        std::sort(names.begin(), names.end(), lt());
        svector<symbol>::iterator it2  = names.begin();
        svector<symbol>::iterator end2 = names.end();
        for (; it2 != end2; ++it2) {
            for (unsigned i = 0; i < indent; i++) out << " ";
            out << *it2;
            info d;
            d.second = 0;
            m_info.find(*it2, d);
            SASSERT(d.second);
            out << " (" << d.first << ") " << d.second << "\n";
        }
    }
};

param_descrs::param_descrs() {
    m_imp = alloc(imp);
}

param_descrs::~param_descrs() {
    dealloc(m_imp);
}

void param_descrs::insert(symbol const & name, param_kind k, char const * descr) {
    m_imp->insert(name, k, descr);
}

void param_descrs::insert(char const * name, param_kind k, char const * descr) {
    insert(symbol(name), k, descr);
}

void param_descrs::erase(symbol const & name) {
    m_imp->erase(name);
}

void param_descrs::erase(char const * name) {
    erase(symbol(name));
}

param_kind param_descrs::get_kind(symbol const & name) const {
    return m_imp->get_kind(name);
}

param_kind param_descrs::get_kind(char const * name) const {
    return get_kind(symbol(name));
}

unsigned param_descrs::size() const {
    return m_imp->size();
}

symbol param_descrs::get_param_name(unsigned i) const {
    return m_imp->get_param_name(i);
}

void param_descrs::display(std::ostream & out, unsigned indent) const {
    return m_imp->display(out, indent);
}

void insert_max_memory(param_descrs & r) {
    r.insert(":max-memory", CPK_UINT, "(default: infty) maximum amount of memory in megabytes.");
}

void insert_max_steps(param_descrs & r) {
    r.insert(":max-steps", CPK_UINT, "(default: infty) maximum number of steps.");
}

void insert_produce_models(param_descrs & r) {
    r.insert(":produce-models", CPK_BOOL, "(default: false) model generation.");
}

void insert_produce_proofs(param_descrs & r) {
    r.insert(":produce-proofs", CPK_BOOL, "(default: false) proof generation.");
}

void insert_timeout(param_descrs & r) {
    r.insert(":timeout", CPK_UINT, "(default: infty) timeout in milliseconds.");
}

class params {
    friend class params_ref;
    struct value {
        param_kind m_kind;
        union {
            bool          m_bool_value;
            unsigned      m_uint_value;
            double        m_double_value;
            char const *  m_str_value;
            char const *  m_sym_value;
            rational *    m_rat_value;
            ast *         m_ast_value;
        };
    };
    typedef std::pair<symbol, value> entry;
    ast_manager *  m_manager;
    svector<entry> m_entries;
    unsigned       m_ref_count;
    
    void del_value(entry & e);
    void del_values();

public:
    params():m_manager(0), m_ref_count(0) {}
    ~params() {
        reset();
    }

    void inc_ref() { m_ref_count++; }
    void dec_ref() { SASSERT(m_ref_count > 0); m_ref_count--; if (m_ref_count == 0) dealloc(this); }

    void set_manager(ast_manager & m);

    bool empty() const { return m_entries.empty(); }
    bool contains(symbol const & k) const;
    bool contains(char const * k) const;

    void reset();
    void reset(symbol const & k);
    void reset(char const * k);

    void validate(param_descrs const & p) const {
        svector<params::entry>::const_iterator it  = m_entries.begin();  
        svector<params::entry>::const_iterator end = m_entries.end();
        for (; it != end; ++it) {                                
            param_kind expected = p.get_kind(it->first);
            if (expected == CPK_INVALID)
                throw default_exception("unknown parameter '%s'", it->first.str().c_str());
            if (it->second.m_kind != expected) 
                throw default_exception("parameter kind mismatch '%s'", it->first.str().c_str());
        }
    }
    
    // getters
    bool get_bool(symbol const & k, bool _default) const;
    bool get_bool(char const * k, bool _default) const;
    unsigned get_uint(symbol const & k, unsigned _default) const;
    unsigned get_uint(char const * k, unsigned _default) const;
    double get_double(symbol const & k, double _default) const;
    double get_double(char const * k, double _default) const;
    char const * get_str(symbol const & k, char const * _default) const;
    char const * get_str(char const * k, char const * _default) const;
    rational get_rat(symbol const & k, rational const & _default) const;
    rational get_rat(char const * k, rational const & _default) const; 
    symbol get_sym(symbol const & k, symbol const & _default) const;
    symbol get_sym(char const * k, symbol const & _default) const;
    expr * get_expr(symbol const & k, expr * _default) const;
    expr * get_expr(char const * k, expr * _default) const;
    func_decl * get_func_decl(symbol const & k, func_decl * _default) const;
    func_decl * get_func_decl(char const * k, func_decl * _default) const;
    sort * get_sort(symbol const & k, sort * _default) const;
    sort * get_sort(char const * k, sort * _default) const;

    // setters
    void set_bool(symbol const & k, bool v);
    void set_bool(char const * k, bool  v);
    void set_uint(symbol const & k, unsigned v);
    void set_uint(char const * k, unsigned v);
    void set_double(symbol const & k, double v);
    void set_double(char const * k, double v);
    void set_str(symbol const & k, char const * v);
    void set_str(char const * k, char const * v);
    void set_rat(symbol const & k, rational const & v);
    void set_rat(char const * k, rational const & v); 
    void set_sym(symbol const & k, symbol const & v);
    void set_sym(char const * k, symbol const & v);
    void set_expr(symbol const & k, expr * v);
    void set_expr(char const * k, expr * v);
    void set_func_decl(symbol const & k, func_decl * v);
    void set_func_decl(char const * k, func_decl * v);
    void set_sort(symbol const & k, sort * v);
    void set_sort(char const * k, sort * v);

    void display(std::ostream & out) const {
        out << "(params";
        svector<params::entry>::const_iterator it  = m_entries.begin();  
        svector<params::entry>::const_iterator end = m_entries.end();
        for (; it != end; ++it) {                                
            out << " " << it->first;
            switch (it->second.m_kind) {
            case CPK_BOOL:
                out << " " << it->second.m_bool_value;
                break;
            case CPK_UINT:
                out << " " <<it->second.m_uint_value;
                break;
            case CPK_DOUBLE:
                out << " " << it->second.m_double_value;
                break;
            case CPK_NUMERAL:
                out << " " << *(it->second.m_rat_value);
                break;
            case CPK_SYMBOL:
                out << " " << symbol::mk_symbol_from_c_ptr(it->second.m_sym_value);
                break;
            case CPK_STRING:
                out << " " << it->second.m_str_value;
                break;
            case CPK_EXPR:
            case CPK_FUNC_DECL:
            case CPK_SORT:
                out << " #" << it->second.m_ast_value->get_id();
                break;
            default:
                UNREACHABLE();
                break;
            }
        }
        out << ")";
    }
};

params_ref::~params_ref() {
    if (m_params)
        m_params->dec_ref();
}

params_ref::params_ref(params_ref const & p):
    m_params(0) {
    operator=(p);
}

void params_ref::display(std::ostream & out) const {
    if (m_params)
        m_params->display(out);
    else
        out << "(params)";
}

void params_ref::validate(param_descrs const & p) const {
    if (m_params)
        m_params->validate(p);
}

params_ref & params_ref::operator=(params_ref const & p) {
    if (p.m_params)
        p.m_params->inc_ref();
    if (m_params)
        m_params->dec_ref();
    m_params = p.m_params;
    return *this;
}

void params_ref::copy(params_ref const & src) {
    if (m_params == 0)
        operator=(src);
    else {
        init();
        copy_core(src.m_params);
    }
}

void params_ref::copy_core(params const * src) {
    if (src == 0)
        return;
    svector<params::entry>::const_iterator it  = src->m_entries.begin();  
    svector<params::entry>::const_iterator end = src->m_entries.end();    
    for (; it != end; ++it) {                                
        switch (it->second.m_kind) {
        case CPK_BOOL:
            m_params->set_bool(it->first, it->second.m_bool_value);
            break;
        case CPK_UINT:
            m_params->set_uint(it->first, it->second.m_uint_value);
            break;
        case CPK_DOUBLE:
            m_params->set_double(it->first, it->second.m_double_value);
            break;
        case CPK_NUMERAL:
            m_params->set_rat(it->first, *(it->second.m_rat_value));
            break;
        case CPK_SYMBOL:
            m_params->set_sym(it->first, symbol::mk_symbol_from_c_ptr(it->second.m_sym_value));
            break;
        case CPK_STRING:
            m_params->set_str(it->first, it->second.m_str_value);
            break;
        case CPK_EXPR:
            m_params->set_expr(it->first, static_cast<expr*>(it->second.m_ast_value));
            break;
        case CPK_FUNC_DECL:
            m_params->set_func_decl(it->first, static_cast<func_decl*>(it->second.m_ast_value));
            break;
        case CPK_SORT:
            m_params->set_sort(it->first, static_cast<sort*>(it->second.m_ast_value));
            break;
        default:
            UNREACHABLE();
            break;
        }
    }
}

void params_ref::init() {
    if (!m_params) {
        m_params = alloc(params);
        m_params->inc_ref();
    }
    else if (m_params->m_ref_count > 1) {
        params * old = m_params;
        m_params = alloc(params);
        m_params->inc_ref();
        m_params->m_manager = old->m_manager;
        copy_core(old);
        old->dec_ref();
    }
    
    SASSERT(m_params->m_ref_count == 1);
}

bool params_ref::get_bool(symbol const & k, bool _default) const { return m_params ? m_params->get_bool(k, _default) : _default; }
bool params_ref::get_bool(char const * k, bool _default) const { return m_params ? m_params->get_bool(k, _default) : _default; }
unsigned params_ref::get_uint(symbol const & k, unsigned _default) const { return m_params ? m_params->get_uint(k, _default) : _default; }
unsigned params_ref::get_uint(char const * k, unsigned _default) const { return m_params ? m_params->get_uint(k, _default) : _default; }
double params_ref::get_double(symbol const & k, double _default) const { return m_params ? m_params->get_double(k, _default) : _default; }
double params_ref::get_double(char const * k, double _default) const { return m_params ? m_params->get_double(k, _default) : _default; }
char const * params_ref::get_str(symbol const & k, char const * _default) const { return m_params ? m_params->get_str(k, _default) : _default; }
char const * params_ref::get_str(char const * k, char const * _default) const { return m_params ? m_params->get_str(k, _default) : _default; }
expr * params_ref::get_expr(symbol const & k, expr * _default) const { return m_params ? m_params->get_expr(k, _default) : _default; }
expr * params_ref::get_expr(char const * k, expr * _default) const { return m_params ? m_params->get_expr(k, _default) : _default; }
func_decl * params_ref::get_func_decl(symbol const & k, func_decl * _default) const { return m_params ? m_params->get_func_decl(k, _default) : _default; }
func_decl * params_ref::get_func_decl(char const * k, func_decl * _default) const { return m_params ? m_params->get_func_decl(k, _default) : _default; }
sort * params_ref::get_sort(symbol const & k, sort * _default) const { return m_params ? m_params->get_sort(k, _default) : _default; }
sort * params_ref::get_sort(char const * k, sort * _default) const { return m_params ? m_params->get_sort(k, _default) : _default; }

rational params_ref::get_rat(symbol const & k, rational const & _default) const { 
    return m_params ? m_params->get_rat(k, _default) : _default; 
}

rational params_ref::get_rat(char const * k, rational const & _default) const { 
    return m_params ? m_params->get_rat(k, _default) : _default; 
}

symbol params_ref::get_sym(symbol const & k, symbol const & _default) const { 
    return m_params ? m_params->get_sym(k, _default) : _default; 
}

symbol params_ref::get_sym(char const * k, symbol const & _default) const { 
    return m_params ? m_params->get_sym(k, _default) : _default; 
}

void params_ref::set_manager(ast_manager & m) {
    init();
    m_params->set_manager(m);
}

bool params_ref::empty() const {
    if (!m_params)
        return true;
    return m_params->empty();
}

bool params_ref::contains(symbol const & k) const {
    if (!m_params)
        return false;
    return m_params->contains(k);
}

bool params_ref::contains(char const * k) const {
    if (!m_params)
        return false;
    return m_params->contains(k);
}

void params_ref::reset() {
    if (m_params)
        m_params->reset();
}

void params_ref::reset(symbol const & k) {
    if (m_params)
        m_params->reset(k);
}

void params_ref::reset(char const * k) {
    if (m_params)
        m_params->reset(k);
}

void params_ref::set_bool(symbol const & k, bool v) {
    init();
    m_params->set_bool(k, v);
}

void params_ref::set_bool(char const * k, bool  v) {
    init();
    m_params->set_bool(k, v);
}

void params_ref::set_uint(symbol const & k, unsigned v) {
    init();
    m_params->set_uint(k, v);
}

void params_ref::set_uint(char const * k, unsigned v) {
    init();
    m_params->set_uint(k, v);
}

void params_ref::set_double(symbol const & k, double v) {
    init();
    m_params->set_double(k, v);
}

void params_ref::set_double(char const * k, double v) {
    init();
    m_params->set_double(k, v);
}

void params_ref::set_str(symbol const & k, char const * v) {
    init();
    m_params->set_str(k, v);
}

void params_ref::set_str(char const * k, char const * v) {
    init();
    m_params->set_str(k, v);
}

void params_ref::set_rat(symbol const & k, rational const & v) {
    init();
    m_params->set_rat(k, v);
}

void params_ref::set_rat(char const * k, rational const & v) {
    init();
    m_params->set_rat(k, v);
}

void params_ref::set_sym(symbol const & k, symbol const & v) {
    init();
    m_params->set_sym(k, v);
}

void params_ref::set_sym(char const * k, symbol const & v) {
    init();
    m_params->set_sym(k, v);
}

void params_ref::set_expr(symbol const & k, expr * v) {
    init();
    m_params->set_expr(k, v);
}

void params_ref::set_expr(char const * k, expr * v) {
    init();
    m_params->set_expr(k, v);
}

void params_ref::set_func_decl(symbol const & k, func_decl * v) {
    init();
    m_params->set_func_decl(k, v);
}

void params_ref::set_func_decl(char const * k, func_decl * v) {
    init();
    m_params->set_func_decl(k, v);
}

void params_ref::set_sort(symbol const & k, sort * v) {
    init();
    m_params->set_sort(k, v);
}

void params_ref::set_sort(char const * k, sort * v) {
    init();
    m_params->set_sort(k, v);
}

void params::del_value(entry & e) {
    switch (e.second.m_kind) {
    case CPK_NUMERAL:
        if (e.second.m_kind == CPK_NUMERAL)
            dealloc(e.second.m_rat_value);
        break;
    case CPK_EXPR:
    case CPK_SORT:
    case CPK_FUNC_DECL:
        SASSERT(m_manager);
        m_manager->dec_ref(e.second.m_ast_value);
        return;
    default:
        return;
    }
}

void params::set_manager(ast_manager & m) {
    m_manager = &m;
}

#define TRAVERSE_ENTRIES(CODE) {                        \
    svector<entry>::iterator it  = m_entries.begin();   \
    svector<entry>::iterator end = m_entries.end();     \
    for (; it != end; ++it) {                           \
        CODE                                            \
    }                                                   \
}

#define TRAVERSE_CONST_ENTRIES(CODE) {                          \
    svector<entry>::const_iterator it  = m_entries.begin();     \
    svector<entry>::const_iterator end = m_entries.end();       \
    for (; it != end; ++it) {                                   \
        CODE                                                    \
    }                                                           \
}

void params::del_values() {
    TRAVERSE_ENTRIES(del_value(*it););
}

#define CONTAINS(k) {                                           \
    if (empty())                                                \
        return false;                                           \
    TRAVERSE_CONST_ENTRIES(if (it->first == k) return true;);   \
    return false;                                               \
}

bool params::contains(symbol const & k) const {
    CONTAINS(k);
}
 
bool params::contains(char const * k) const {
    CONTAINS(k);
}

void params::reset() {
    del_values();
    m_entries.finalize();
    SASSERT(empty());
}

#define RESET(k) {                              \
    if (empty()) return;                        \
    TRAVERSE_ENTRIES(if (it->first == k) {      \
        svector<entry>::iterator it2 = it;      \
        del_value(*it2);                        \
        ++it;                                   \
        for (; it != end; ++it, ++it2) {        \
            *it2 = *it;                         \
        }                                       \
        m_entries.pop_back();                   \
        return;                                 \
    });                                         \
}

void params::reset(symbol const & k) {
    RESET(k);
}

void params::reset(char const * k) {
    RESET(k);
}

#define GET_VALUE(MATCH_CODE, KIND) {                                           \
    if (empty()) return _default;                                                \
    TRAVERSE_CONST_ENTRIES(if (it->first == k && it->second.m_kind == KIND) {   \
        MATCH_CODE                                                              \
    });                                                                         \
    return _default;                                                             \
}
    
#define GET_SIMPLE_VALUE(FIELD_NAME, KIND) GET_VALUE(return it->second.FIELD_NAME;, KIND)

bool params::get_bool(symbol const & k, bool _default) const {
    GET_SIMPLE_VALUE(m_bool_value, CPK_BOOL);
}

bool params::get_bool(char const * k, bool _default) const {
    GET_SIMPLE_VALUE(m_bool_value, CPK_BOOL);
}

unsigned params::get_uint(symbol const & k, unsigned _default) const {
    GET_SIMPLE_VALUE(m_uint_value, CPK_UINT);
}

unsigned params::get_uint(char const * k, unsigned _default) const {
    GET_SIMPLE_VALUE(m_uint_value, CPK_UINT);
}

double params::get_double(symbol const & k, double _default) const {
    GET_SIMPLE_VALUE(m_double_value, CPK_DOUBLE);
}

double params::get_double(char const * k, double _default) const {
    GET_SIMPLE_VALUE(m_double_value, CPK_DOUBLE);
}

char const * params::get_str(symbol const & k, char const * _default) const {
    GET_SIMPLE_VALUE(m_str_value, CPK_STRING);
}

char const * params::get_str(char const * k, char const * _default) const {
    GET_SIMPLE_VALUE(m_str_value, CPK_STRING);
}

rational params::get_rat(symbol const & k, rational const & _default) const {
    if (empty()) return _default;                                               
    TRAVERSE_CONST_ENTRIES(if (it->first == k) {
            if (it->second.m_kind == CPK_NUMERAL) {   
                return *(it->second.m_rat_value);
            }
            if (it->second.m_kind == CPK_UINT) {
                return rational(static_cast<int>(it->second.m_uint_value));
            }
        });
    return _default;                                                            
}

rational params::get_rat(char const * k, rational const & _default) const {
    if (empty()) return _default;                                               
    TRAVERSE_CONST_ENTRIES(if (it->first == k) {
            if (it->second.m_kind == CPK_NUMERAL) {   
                return *(it->second.m_rat_value);
            }
            if (it->second.m_kind == CPK_UINT) {
                return rational(static_cast<int>(it->second.m_uint_value));
            }
        });
    return _default;                                                            
}

symbol params::get_sym(symbol const & k, symbol const & _default) const {
    GET_VALUE(return symbol::mk_symbol_from_c_ptr(it->second.m_sym_value);, CPK_SYMBOL);
}

symbol params::get_sym(char const * k, symbol const & _default) const {
    GET_VALUE(return symbol::mk_symbol_from_c_ptr(it->second.m_sym_value);, CPK_SYMBOL);
}

expr * params::get_expr(symbol const & k, expr * _default) const {
    GET_VALUE(return static_cast<expr*>(it->second.m_ast_value);, CPK_EXPR);
}

expr * params::get_expr(char const * k, expr * _default) const {
    GET_VALUE(return static_cast<expr*>(it->second.m_ast_value);, CPK_EXPR);
}

func_decl * params::get_func_decl(symbol const & k, func_decl * _default) const {
    GET_VALUE(return static_cast<func_decl*>(it->second.m_ast_value);, CPK_FUNC_DECL);
}

func_decl * params::get_func_decl(char const * k, func_decl * _default) const {
    GET_VALUE(return static_cast<func_decl*>(it->second.m_ast_value);, CPK_FUNC_DECL);
}

sort * params::get_sort(symbol const & k, sort * _default) const {
    GET_VALUE(return static_cast<sort*>(it->second.m_ast_value);, CPK_SORT);
}

sort * params::get_sort(char const * k, sort * _default) const {
    GET_VALUE(return static_cast<sort*>(it->second.m_ast_value);, CPK_SORT);
}

#define SET_VALUE(MATCH_CODE, ADD_CODE) {       \
    TRAVERSE_ENTRIES(if (it->first == k) {      \
        MATCH_CODE                              \
        return;                                 \
    });                                         \
    ADD_CODE                                    \
}

#define SET_SIMPLE_VALUE(FIELD_NAME, KIND) SET_VALUE({  \
    del_value(*it);                                     \
    it->second.m_kind = KIND;                           \
    it->second.FIELD_NAME = v;                          \
},                                                      \
{                                                       \
    entry new_entry;                                    \
    new_entry.first  = symbol(k);                       \
    new_entry.second.m_kind = KIND;                     \
    new_entry.second.FIELD_NAME = v;                    \
    m_entries.push_back(new_entry);                     \
})

// setters
void params::set_bool(symbol const & k, bool v) {
    SET_SIMPLE_VALUE(m_bool_value, CPK_BOOL);
}

void params::set_bool(char const * k, bool  v) {
    SET_SIMPLE_VALUE(m_bool_value, CPK_BOOL);
}
 
void params::set_uint(symbol const & k, unsigned v) {
    SET_SIMPLE_VALUE(m_uint_value, CPK_UINT);
}

void params::set_uint(char const * k, unsigned v) {
    SET_SIMPLE_VALUE(m_uint_value, CPK_UINT);
}

void params::set_double(symbol const & k, double v) {
    SET_SIMPLE_VALUE(m_double_value, CPK_DOUBLE);
}

void params::set_double(char const * k, double v) {
    SET_SIMPLE_VALUE(m_double_value, CPK_DOUBLE);
}

void params::set_str(symbol const & k, char const * v) {
    SET_SIMPLE_VALUE(m_str_value, CPK_STRING);
}

void params::set_str(char const * k, char const * v) {
    SET_SIMPLE_VALUE(m_str_value, CPK_STRING);
}

#define SET_RAT_VALUE() SET_VALUE({                             \
    if (it->second.m_kind != CPK_NUMERAL) {                     \
        del_value(*it);                                         \
        it->second.m_kind = CPK_NUMERAL;                        \
        it->second.m_rat_value = alloc(rational);               \
    }                                                           \
    *(it->second.m_rat_value) = v;                              \
},                                                              \
{                                                               \
    entry new_entry;                                            \
    new_entry.first  = symbol(k);                               \
    new_entry.second.m_kind = CPK_NUMERAL;                      \
    new_entry.second.m_rat_value = alloc(rational);             \
    *(new_entry.second.m_rat_value) = v;                        \
    m_entries.push_back(new_entry);                             \
})

void params::set_rat(symbol const & k, rational const & v) {
    SET_RAT_VALUE();
}

void params::set_rat(char const * k, rational const & v) {
    SET_RAT_VALUE();
}

#define SET_SYM_VALUE() SET_VALUE({                     \
    del_value(*it);                                     \
    it->second.m_kind = CPK_SYMBOL;                     \
    it->second.m_sym_value = v.bare_str();              \
},                                                      \
{                                                       \
    entry new_entry;                                    \
    new_entry.first  = symbol(k);                       \
    new_entry.second.m_kind = CPK_SYMBOL;               \
    new_entry.second.m_sym_value = v.bare_str();        \
    m_entries.push_back(new_entry);                     \
})

void params::set_sym(symbol const & k, symbol const & v) {
    SET_SYM_VALUE();
}

void params::set_sym(char const * k, symbol const & v) {
    SET_SYM_VALUE();
}

#define SET_AST_VALUE(KIND) {                   \
    SASSERT(m_manager);                         \
    m_manager->inc_ref(v);                      \
    SET_VALUE({                                 \
        del_value(*it);                         \
        it->second.m_kind      = KIND;          \
        it->second.m_ast_value = v;             \
    },                                          \
    {                                           \
        entry new_entry;                        \
        new_entry.first  = symbol(k);           \
        new_entry.second.m_kind = KIND;         \
        new_entry.second.m_ast_value = v;       \
        m_entries.push_back(new_entry);         \
    })}


void params::set_expr(symbol const & k, expr * v) {
    SET_AST_VALUE(CPK_EXPR);
}
 
void params::set_expr(char const * k, expr * v) {
    SET_AST_VALUE(CPK_EXPR);
}

void params::set_func_decl(symbol const & k, func_decl * v) {
    SET_AST_VALUE(CPK_FUNC_DECL);
}

void params::set_func_decl(char const * k, func_decl * v) {
    SET_AST_VALUE(CPK_FUNC_DECL);
}

void params::set_sort(symbol const & k, sort * v) {
    SET_AST_VALUE(CPK_SORT);
}

void params::set_sort(char const * k, sort * v) {
    SET_AST_VALUE(CPK_SORT);
}

