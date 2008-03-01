/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 */

// Portions of code courtesy of Clifford Click

// Optimization - Graph Style

#include "incls/_precompiled.incl"
#include "incls/_callnode.cpp.incl"

//=============================================================================
uint StartNode::size_of() const { return sizeof(*this); }
uint StartNode::cmp( const Node &n ) const
{ return _domain == ((StartNode&)n)._domain; }
const Type *StartNode::bottom_type() const { return _domain; }
const Type *StartNode::Value(PhaseTransform *phase) const { return _domain; }
#ifndef PRODUCT
void StartNode::dump_spec(outputStream *st) const { st->print(" #"); _domain->dump_on(st);}
#endif

//------------------------------Ideal------------------------------------------
Node *StartNode::Ideal(PhaseGVN *phase, bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL;
}

//------------------------------calling_convention-----------------------------
void StartNode::calling_convention( BasicType* sig_bt, VMRegPair *parm_regs, uint argcnt ) const {
  Matcher::calling_convention( sig_bt, parm_regs, argcnt, false );
}

//------------------------------Registers--------------------------------------
const RegMask &StartNode::in_RegMask(uint) const {
  return RegMask::Empty;
}

//------------------------------match------------------------------------------
// Construct projections for incoming parameters, and their RegMask info
Node *StartNode::match( const ProjNode *proj, const Matcher *match ) {
  switch (proj->_con) {
  case TypeFunc::Control:
  case TypeFunc::I_O:
  case TypeFunc::Memory:
    return new (match->C, 1) MachProjNode(this,proj->_con,RegMask::Empty,MachProjNode::unmatched_proj);
  case TypeFunc::FramePtr:
    return new (match->C, 1) MachProjNode(this,proj->_con,Matcher::c_frame_ptr_mask, Op_RegP);
  case TypeFunc::ReturnAdr:
    return new (match->C, 1) MachProjNode(this,proj->_con,match->_return_addr_mask,Op_RegP);
  case TypeFunc::Parms:
  default: {
      uint parm_num = proj->_con - TypeFunc::Parms;
      const Type *t = _domain->field_at(proj->_con);
      if (t->base() == Type::Half)  // 2nd half of Longs and Doubles
        return new (match->C, 1) ConNode(Type::TOP);
      uint ideal_reg = Matcher::base2reg[t->base()];
      RegMask &rm = match->_calling_convention_mask[parm_num];
      return new (match->C, 1) MachProjNode(this,proj->_con,rm,ideal_reg);
    }
  }
  return NULL;
}

//------------------------------StartOSRNode----------------------------------
// The method start node for an on stack replacement adapter

//------------------------------osr_domain-----------------------------
const TypeTuple *StartOSRNode::osr_domain() {
  const Type **fields = TypeTuple::fields(2);
  fields[TypeFunc::Parms+0] = TypeRawPtr::BOTTOM;  // address of osr buffer

  return TypeTuple::make(TypeFunc::Parms+1, fields);
}

//=============================================================================
const char * const ParmNode::names[TypeFunc::Parms+1] = {
  "Control", "I_O", "Memory", "FramePtr", "ReturnAdr", "Parms"
};

#ifndef PRODUCT
void ParmNode::dump_spec(outputStream *st) const {
  if( _con < TypeFunc::Parms ) {
    st->print(names[_con]);
  } else {
    st->print("Parm%d: ",_con-TypeFunc::Parms);
    // Verbose and WizardMode dump bottom_type for all nodes
    if( !Verbose && !WizardMode )   bottom_type()->dump_on(st);
  }
}
#endif

uint ParmNode::ideal_reg() const {
  switch( _con ) {
  case TypeFunc::Control  : // fall through
  case TypeFunc::I_O      : // fall through
  case TypeFunc::Memory   : return 0;
  case TypeFunc::FramePtr : // fall through
  case TypeFunc::ReturnAdr: return Op_RegP;
  default                 : assert( _con > TypeFunc::Parms, "" );
    // fall through
  case TypeFunc::Parms    : {
    // Type of argument being passed
    const Type *t = in(0)->as_Start()->_domain->field_at(_con);
    return Matcher::base2reg[t->base()];
  }
  }
  ShouldNotReachHere();
  return 0;
}

//=============================================================================
ReturnNode::ReturnNode(uint edges, Node *cntrl, Node *i_o, Node *memory, Node *frameptr, Node *retadr ) : Node(edges) {
  init_req(TypeFunc::Control,cntrl);
  init_req(TypeFunc::I_O,i_o);
  init_req(TypeFunc::Memory,memory);
  init_req(TypeFunc::FramePtr,frameptr);
  init_req(TypeFunc::ReturnAdr,retadr);
}

Node *ReturnNode::Ideal(PhaseGVN *phase, bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL;
}

const Type *ReturnNode::Value( PhaseTransform *phase ) const {
  return ( phase->type(in(TypeFunc::Control)) == Type::TOP)
    ? Type::TOP
    : Type::BOTTOM;
}

// Do we Match on this edge index or not?  No edges on return nodes
uint ReturnNode::match_edge(uint idx) const {
  return 0;
}


#ifndef PRODUCT
void ReturnNode::dump_req() const {
  // Dump the required inputs, enclosed in '(' and ')'
  uint i;                       // Exit value of loop
  for( i=0; i<req(); i++ ) {    // For all required inputs
    if( i == TypeFunc::Parms ) tty->print("returns");
    if( in(i) ) tty->print("%c%d ", Compile::current()->node_arena()->contains(in(i)) ? ' ' : 'o', in(i)->_idx);
    else tty->print("_ ");
  }
}
#endif

//=============================================================================
RethrowNode::RethrowNode(
  Node* cntrl,
  Node* i_o,
  Node* memory,
  Node* frameptr,
  Node* ret_adr,
  Node* exception
) : Node(TypeFunc::Parms + 1) {
  init_req(TypeFunc::Control  , cntrl    );
  init_req(TypeFunc::I_O      , i_o      );
  init_req(TypeFunc::Memory   , memory   );
  init_req(TypeFunc::FramePtr , frameptr );
  init_req(TypeFunc::ReturnAdr, ret_adr);
  init_req(TypeFunc::Parms    , exception);
}

Node *RethrowNode::Ideal(PhaseGVN *phase, bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL;
}

const Type *RethrowNode::Value( PhaseTransform *phase ) const {
  return (phase->type(in(TypeFunc::Control)) == Type::TOP)
    ? Type::TOP
    : Type::BOTTOM;
}

uint RethrowNode::match_edge(uint idx) const {
  return 0;
}

#ifndef PRODUCT
void RethrowNode::dump_req() const {
  // Dump the required inputs, enclosed in '(' and ')'
  uint i;                       // Exit value of loop
  for( i=0; i<req(); i++ ) {    // For all required inputs
    if( i == TypeFunc::Parms ) tty->print("exception");
    if( in(i) ) tty->print("%c%d ", Compile::current()->node_arena()->contains(in(i)) ? ' ' : 'o', in(i)->_idx);
    else tty->print("_ ");
  }
}
#endif

//=============================================================================
// Do we Match on this edge index or not?  Match only target address & method
uint TailCallNode::match_edge(uint idx) const {
  return TypeFunc::Parms <= idx  &&  idx <= TypeFunc::Parms+1;
}

//=============================================================================
// Do we Match on this edge index or not?  Match only target address & oop
uint TailJumpNode::match_edge(uint idx) const {
  return TypeFunc::Parms <= idx  &&  idx <= TypeFunc::Parms+1;
}

//=============================================================================
JVMState::JVMState(ciMethod* method, JVMState* caller) {
  assert(method != NULL, "must be valid call site");
  _method = method;
  debug_only(_bci = -99);  // random garbage value
  debug_only(_map = (SafePointNode*)-1);
  _caller = caller;
  _depth  = 1 + (caller == NULL ? 0 : caller->depth());
  _locoff = TypeFunc::Parms;
  _stkoff = _locoff + _method->max_locals();
  _monoff = _stkoff + _method->max_stack();
  _endoff = _monoff;
  _sp = 0;
}
JVMState::JVMState(int stack_size) {
  _method = NULL;
  _bci = InvocationEntryBci;
  debug_only(_map = (SafePointNode*)-1);
  _caller = NULL;
  _depth  = 1;
  _locoff = TypeFunc::Parms;
  _stkoff = _locoff;
  _monoff = _stkoff + stack_size;
  _endoff = _monoff;
  _sp = 0;
}

//--------------------------------of_depth-------------------------------------
JVMState* JVMState::of_depth(int d) const {
  const JVMState* jvmp = this;
  assert(0 < d && (uint)d <= depth(), "oob");
  for (int skip = depth() - d; skip > 0; skip--) {
    jvmp = jvmp->caller();
  }
  assert(jvmp->depth() == (uint)d, "found the right one");
  return (JVMState*)jvmp;
}

//-----------------------------same_calls_as-----------------------------------
bool JVMState::same_calls_as(const JVMState* that) const {
  if (this == that)                    return true;
  if (this->depth() != that->depth())  return false;
  const JVMState* p = this;
  const JVMState* q = that;
  for (;;) {
    if (p->_method != q->_method)    return false;
    if (p->_method == NULL)          return true;   // bci is irrelevant
    if (p->_bci    != q->_bci)       return false;
    p = p->caller();
    q = q->caller();
    if (p == q)                      return true;
    assert(p != NULL && q != NULL, "depth check ensures we don't run off end");
  }
}

//------------------------------debug_start------------------------------------
uint JVMState::debug_start()  const {
  debug_only(JVMState* jvmroot = of_depth(1));
  assert(jvmroot->locoff() <= this->locoff(), "youngest JVMState must be last");
  return of_depth(1)->locoff();
}

//-------------------------------debug_end-------------------------------------
uint JVMState::debug_end() const {
  debug_only(JVMState* jvmroot = of_depth(1));
  assert(jvmroot->endoff() <= this->endoff(), "youngest JVMState must be last");
  return endoff();
}

//------------------------------debug_depth------------------------------------
uint JVMState::debug_depth() const {
  uint total = 0;
  for (const JVMState* jvmp = this; jvmp != NULL; jvmp = jvmp->caller()) {
    total += jvmp->debug_size();
  }
  return total;
}

//------------------------------format_helper----------------------------------
// Given an allocation (a Chaitin object) and a Node decide if the Node carries
// any defined value or not.  If it does, print out the register or constant.
#ifndef PRODUCT
static void format_helper( PhaseRegAlloc *regalloc, outputStream* st, Node *n, const char *msg, uint i ) {
  if (n == NULL) { st->print(" NULL"); return; }
  if( OptoReg::is_valid(regalloc->get_reg_first(n))) { // Check for undefined
    char buf[50];
    regalloc->dump_register(n,buf);
    st->print(" %s%d]=%s",msg,i,buf);
  } else {                      // No register, but might be constant
    const Type *t = n->bottom_type();
    switch (t->base()) {
    case Type::Int:
      st->print(" %s%d]=#"INT32_FORMAT,msg,i,t->is_int()->get_con());
      break;
    case Type::AnyPtr:
      assert( t == TypePtr::NULL_PTR, "" );
      st->print(" %s%d]=#NULL",msg,i);
      break;
    case Type::AryPtr:
    case Type::KlassPtr:
    case Type::InstPtr:
      st->print(" %s%d]=#Ptr" INTPTR_FORMAT,msg,i,t->isa_oopptr()->const_oop());
      break;
    case Type::RawPtr:
      st->print(" %s%d]=#Raw" INTPTR_FORMAT,msg,i,t->is_rawptr());
      break;
    case Type::DoubleCon:
      st->print(" %s%d]=#%fD",msg,i,t->is_double_constant()->_d);
      break;
    case Type::FloatCon:
      st->print(" %s%d]=#%fF",msg,i,t->is_float_constant()->_f);
      break;
    case Type::Long:
      st->print(" %s%d]=#"INT64_FORMAT,msg,i,t->is_long()->get_con());
      break;
    case Type::Half:
    case Type::Top:
      st->print(" %s%d]=_",msg,i);
      break;
    default: ShouldNotReachHere();
    }
  }
}
#endif

//------------------------------format-----------------------------------------
#ifndef PRODUCT
void JVMState::format(PhaseRegAlloc *regalloc, const Node *n, outputStream* st) const {
  st->print("        #");
  if( _method ) {
    _method->print_short_name(st);
    st->print(" @ bci:%d ",_bci);
  } else {
    st->print_cr(" runtime stub ");
    return;
  }
  if (n->is_MachSafePoint()) {
    MachSafePointNode *mcall = n->as_MachSafePoint();
    uint i;
    // Print locals
    for( i = 0; i < (uint)loc_size(); i++ )
      format_helper( regalloc, st, mcall->local(this, i), "L[", i );
    // Print stack
    for (i = 0; i < (uint)stk_size(); i++) {
      if ((uint)(_stkoff + i) >= mcall->len())
        st->print(" oob ");
      else
       format_helper( regalloc, st, mcall->stack(this, i), "STK[", i );
    }
    for (i = 0; (int)i < nof_monitors(); i++) {
      Node *box = mcall->monitor_box(this, i);
      Node *obj = mcall->monitor_obj(this, i);
      if ( OptoReg::is_valid(regalloc->get_reg_first(box)) ) {
        while( !box->is_BoxLock() )  box = box->in(1);
        format_helper( regalloc, st, box, "MON-BOX[", i );
      } else {
        OptoReg::Name box_reg = BoxLockNode::stack_slot(box);
        st->print(" MON-BOX%d=%s+%d",
                   i,
                   OptoReg::regname(OptoReg::c_frame_pointer),
                   regalloc->reg2offset(box_reg));
      }
      format_helper( regalloc, st, obj, "MON-OBJ[", i );
    }
  }
  st->print_cr("");
  if (caller() != NULL)  caller()->format(regalloc, n, st);
}
#endif

#ifndef PRODUCT
void JVMState::dump_spec(outputStream *st) const {
  if (_method != NULL) {
    bool printed = false;
    if (!Verbose) {
      // The JVMS dumps make really, really long lines.
      // Take out the most boring parts, which are the package prefixes.
      char buf[500];
      stringStream namest(buf, sizeof(buf));
      _method->print_short_name(&namest);
      if (namest.count() < sizeof(buf)) {
        const char* name = namest.base();
        if (name[0] == ' ')  ++name;
        const char* endcn = strchr(name, ':');  // end of class name
        if (endcn == NULL)  endcn = strchr(name, '(');
        if (endcn == NULL)  endcn = name + strlen(name);
        while (endcn > name && endcn[-1] != '.' && endcn[-1] != '/')
          --endcn;
        st->print(" %s", endcn);
        printed = true;
      }
    }
    if (!printed)
      _method->print_short_name(st);
    st->print(" @ bci:%d",_bci);
  } else {
    st->print(" runtime stub");
  }
  if (caller() != NULL)  caller()->dump_spec(st);
}
#endif

#ifndef PRODUCT
void JVMState::dump_on(outputStream* st) const {
  if (_map && !((uintptr_t)_map & 1)) {
    if (_map->len() > _map->req()) {  // _map->has_exceptions()
      Node* ex = _map->in(_map->req());  // _map->next_exception()
      // skip the first one; it's already being printed
      while (ex != NULL && ex->len() > ex->req()) {
        ex = ex->in(ex->req());  // ex->next_exception()
        ex->dump(1);
      }
    }
    _map->dump(2);
  }
  st->print("JVMS depth=%d loc=%d stk=%d mon=%d end=%d mondepth=%d sp=%d bci=%d method=",
             depth(), locoff(), stkoff(), monoff(), endoff(), monitor_depth(), sp(), bci());
  if (_method == NULL) {
    st->print_cr("(none)");
  } else {
    _method->print_name(st);
    st->cr();
    if (bci() >= 0 && bci() < _method->code_size()) {
      st->print("    bc: ");
      _method->print_codes_on(bci(), bci()+1, st);
    }
  }
  if (caller() != NULL) {
    caller()->dump_on(st);
  }
}

// Extra way to dump a jvms from the debugger,
// to avoid a bug with C++ member function calls.
void dump_jvms(JVMState* jvms) {
  jvms->dump();
}
#endif

//--------------------------clone_shallow--------------------------------------
JVMState* JVMState::clone_shallow(Compile* C) const {
  JVMState* n = has_method() ? new (C) JVMState(_method, _caller) : new (C) JVMState(0);
  n->set_bci(_bci);
  n->set_locoff(_locoff);
  n->set_stkoff(_stkoff);
  n->set_monoff(_monoff);
  n->set_endoff(_endoff);
  n->set_sp(_sp);
  n->set_map(_map);
  return n;
}

//---------------------------clone_deep----------------------------------------
JVMState* JVMState::clone_deep(Compile* C) const {
  JVMState* n = clone_shallow(C);
  for (JVMState* p = n; p->_caller != NULL; p = p->_caller) {
    p->_caller = p->_caller->clone_shallow(C);
  }
  assert(n->depth() == depth(), "sanity");
  assert(n->debug_depth() == debug_depth(), "sanity");
  return n;
}

//=============================================================================
uint CallNode::cmp( const Node &n ) const
{ return _tf == ((CallNode&)n)._tf && _jvms == ((CallNode&)n)._jvms; }
#ifndef PRODUCT
void CallNode::dump_req() const {
  // Dump the required inputs, enclosed in '(' and ')'
  uint i;                       // Exit value of loop
  for( i=0; i<req(); i++ ) {    // For all required inputs
    if( i == TypeFunc::Parms ) tty->print("(");
    if( in(i) ) tty->print("%c%d ", Compile::current()->node_arena()->contains(in(i)) ? ' ' : 'o', in(i)->_idx);
    else tty->print("_ ");
  }
  tty->print(")");
}

void CallNode::dump_spec(outputStream *st) const {
  st->print(" ");
  tf()->dump_on(st);
  if (_cnt != COUNT_UNKNOWN)  st->print(" C=%f",_cnt);
  if (jvms() != NULL)  jvms()->dump_spec(st);
}
#endif

const Type *CallNode::bottom_type() const { return tf()->range(); }
const Type *CallNode::Value(PhaseTransform *phase) const {
  if (phase->type(in(0)) == Type::TOP)  return Type::TOP;
  return tf()->range();
}

//------------------------------calling_convention-----------------------------
void CallNode::calling_convention( BasicType* sig_bt, VMRegPair *parm_regs, uint argcnt ) const {
  // Use the standard compiler calling convention
  Matcher::calling_convention( sig_bt, parm_regs, argcnt, true );
}


//------------------------------match------------------------------------------
// Construct projections for control, I/O, memory-fields, ..., and
// return result(s) along with their RegMask info
Node *CallNode::match( const ProjNode *proj, const Matcher *match ) {
  switch (proj->_con) {
  case TypeFunc::Control:
  case TypeFunc::I_O:
  case TypeFunc::Memory:
    return new (match->C, 1) MachProjNode(this,proj->_con,RegMask::Empty,MachProjNode::unmatched_proj);

  case TypeFunc::Parms+1:       // For LONG & DOUBLE returns
    assert(tf()->_range->field_at(TypeFunc::Parms+1) == Type::HALF, "");
    // 2nd half of doubles and longs
    return new (match->C, 1) MachProjNode(this,proj->_con, RegMask::Empty, (uint)OptoReg::Bad);

  case TypeFunc::Parms: {       // Normal returns
    uint ideal_reg = Matcher::base2reg[tf()->range()->field_at(TypeFunc::Parms)->base()];
    OptoRegPair regs = is_CallRuntime()
      ? match->c_return_value(ideal_reg,true)  // Calls into C runtime
      : match->  return_value(ideal_reg,true); // Calls into compiled Java code
    RegMask rm = RegMask(regs.first());
    if( OptoReg::is_valid(regs.second()) )
      rm.Insert( regs.second() );
    return new (match->C, 1) MachProjNode(this,proj->_con,rm,ideal_reg);
  }

  case TypeFunc::ReturnAdr:
  case TypeFunc::FramePtr:
  default:
    ShouldNotReachHere();
  }
  return NULL;
}

// Do we Match on this edge index or not?  Match no edges
uint CallNode::match_edge(uint idx) const {
  return 0;
}

//=============================================================================
uint CallJavaNode::size_of() const { return sizeof(*this); }
uint CallJavaNode::cmp( const Node &n ) const {
  CallJavaNode &call = (CallJavaNode&)n;
  return CallNode::cmp(call) && _method == call._method;
}
#ifndef PRODUCT
void CallJavaNode::dump_spec(outputStream *st) const {
  if( _method ) _method->print_short_name(st);
  CallNode::dump_spec(st);
}
#endif

//=============================================================================
uint CallStaticJavaNode::size_of() const { return sizeof(*this); }
uint CallStaticJavaNode::cmp( const Node &n ) const {
  CallStaticJavaNode &call = (CallStaticJavaNode&)n;
  return CallJavaNode::cmp(call);
}

//----------------------------uncommon_trap_request----------------------------
// If this is an uncommon trap, return the request code, else zero.
int CallStaticJavaNode::uncommon_trap_request() const {
  if (_name != NULL && !strcmp(_name, "uncommon_trap")) {
    return extract_uncommon_trap_request(this);
  }
  return 0;
}
int CallStaticJavaNode::extract_uncommon_trap_request(const Node* call) {
#ifndef PRODUCT
  if (!(call->req() > TypeFunc::Parms &&
        call->in(TypeFunc::Parms) != NULL &&
        call->in(TypeFunc::Parms)->is_Con())) {
    assert(_in_dump_cnt != 0, "OK if dumping");
    tty->print("[bad uncommon trap]");
    return 0;
  }
#endif
  return call->in(TypeFunc::Parms)->bottom_type()->is_int()->get_con();
}

#ifndef PRODUCT
void CallStaticJavaNode::dump_spec(outputStream *st) const {
  st->print("# Static ");
  if (_name != NULL) {
    st->print("%s", _name);
    int trap_req = uncommon_trap_request();
    if (trap_req != 0) {
      char buf[100];
      st->print("(%s)",
                 Deoptimization::format_trap_request(buf, sizeof(buf),
                                                     trap_req));
    }
    st->print(" ");
  }
  CallJavaNode::dump_spec(st);
}
#endif

//=============================================================================
uint CallDynamicJavaNode::size_of() const { return sizeof(*this); }
uint CallDynamicJavaNode::cmp( const Node &n ) const {
  CallDynamicJavaNode &call = (CallDynamicJavaNode&)n;
  return CallJavaNode::cmp(call);
}
#ifndef PRODUCT
void CallDynamicJavaNode::dump_spec(outputStream *st) const {
  st->print("# Dynamic ");
  CallJavaNode::dump_spec(st);
}
#endif

//=============================================================================
uint CallRuntimeNode::size_of() const { return sizeof(*this); }
uint CallRuntimeNode::cmp( const Node &n ) const {
  CallRuntimeNode &call = (CallRuntimeNode&)n;
  return CallNode::cmp(call) && !strcmp(_name,call._name);
}
#ifndef PRODUCT
void CallRuntimeNode::dump_spec(outputStream *st) const {
  st->print("# ");
  st->print(_name);
  CallNode::dump_spec(st);
}
#endif

//------------------------------calling_convention-----------------------------
void CallRuntimeNode::calling_convention( BasicType* sig_bt, VMRegPair *parm_regs, uint argcnt ) const {
  Matcher::c_calling_convention( sig_bt, parm_regs, argcnt );
}

//=============================================================================
//------------------------------calling_convention-----------------------------


//=============================================================================
#ifndef PRODUCT
void CallLeafNode::dump_spec(outputStream *st) const {
  st->print("# ");
  st->print(_name);
  CallNode::dump_spec(st);
}
#endif

//=============================================================================

void SafePointNode::set_local(JVMState* jvms, uint idx, Node *c) {
  assert(verify_jvms(jvms), "jvms must match");
  int loc = jvms->locoff() + idx;
  if (in(loc)->is_top() && idx > 0 && !c->is_top() ) {
    // If current local idx is top then local idx - 1 could
    // be a long/double that needs to be killed since top could
    // represent the 2nd half ofthe long/double.
    uint ideal = in(loc -1)->ideal_reg();
    if (ideal == Op_RegD || ideal == Op_RegL) {
      // set other (low index) half to top
      set_req(loc - 1, in(loc));
    }
  }
  set_req(loc, c);
}

uint SafePointNode::size_of() const { return sizeof(*this); }
uint SafePointNode::cmp( const Node &n ) const {
  return (&n == this);          // Always fail except on self
}

//-------------------------set_next_exception----------------------------------
void SafePointNode::set_next_exception(SafePointNode* n) {
  assert(n == NULL || n->Opcode() == Op_SafePoint, "correct value for next_exception");
  if (len() == req()) {
    if (n != NULL)  add_prec(n);
  } else {
    set_prec(req(), n);
  }
}


//----------------------------next_exception-----------------------------------
SafePointNode* SafePointNode::next_exception() const {
  if (len() == req()) {
    return NULL;
  } else {
    Node* n = in(req());
    assert(n == NULL || n->Opcode() == Op_SafePoint, "no other uses of prec edges");
    return (SafePointNode*) n;
  }
}


//------------------------------Ideal------------------------------------------
// Skip over any collapsed Regions
Node *SafePointNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  if (remove_dead_region(phase, can_reshape))  return this;

  return NULL;
}

//------------------------------Identity---------------------------------------
// Remove obviously duplicate safepoints
Node *SafePointNode::Identity( PhaseTransform *phase ) {

  // If you have back to back safepoints, remove one
  if( in(TypeFunc::Control)->is_SafePoint() )
    return in(TypeFunc::Control);

  if( in(0)->is_Proj() ) {
    Node *n0 = in(0)->in(0);
    // Check if he is a call projection (except Leaf Call)
    if( n0->is_Catch() ) {
      n0 = n0->in(0)->in(0);
      assert( n0->is_Call(), "expect a call here" );
    }
    if( n0->is_Call() && n0->as_Call()->guaranteed_safepoint() ) {
      // Useless Safepoint, so remove it
      return in(TypeFunc::Control);
    }
  }

  return this;
}

//------------------------------Value------------------------------------------
const Type *SafePointNode::Value( PhaseTransform *phase ) const {
  if( phase->type(in(0)) == Type::TOP ) return Type::TOP;
  if( phase->eqv( in(0), this ) ) return Type::TOP; // Dead infinite loop
  return Type::CONTROL;
}

#ifndef PRODUCT
void SafePointNode::dump_spec(outputStream *st) const {
  st->print(" SafePoint ");
}
#endif

const RegMask &SafePointNode::in_RegMask(uint idx) const {
  if( idx < TypeFunc::Parms ) return RegMask::Empty;
  // Values outside the domain represent debug info
  return *(Compile::current()->matcher()->idealreg2debugmask[in(idx)->ideal_reg()]);
}
const RegMask &SafePointNode::out_RegMask() const {
  return RegMask::Empty;
}


void SafePointNode::grow_stack(JVMState* jvms, uint grow_by) {
  assert((int)grow_by > 0, "sanity");
  int monoff = jvms->monoff();
  int endoff = jvms->endoff();
  assert(endoff == (int)req(), "no other states or debug info after me");
  Node* top = Compile::current()->top();
  for (uint i = 0; i < grow_by; i++) {
    ins_req(monoff, top);
  }
  jvms->set_monoff(monoff + grow_by);
  jvms->set_endoff(endoff + grow_by);
}

void SafePointNode::push_monitor(const FastLockNode *lock) {
  // Add a LockNode, which points to both the original BoxLockNode (the
  // stack space for the monitor) and the Object being locked.
  const int MonitorEdges = 2;
  assert(JVMState::logMonitorEdges == exact_log2(MonitorEdges), "correct MonitorEdges");
  assert(req() == jvms()->endoff(), "correct sizing");
  if (GenerateSynchronizationCode) {
    add_req(lock->box_node());
    add_req(lock->obj_node());
  } else {
    add_req(NULL);
    add_req(NULL);
  }
  jvms()->set_endoff(req());
}

void SafePointNode::pop_monitor() {
  // Delete last monitor from debug info
  debug_only(int num_before_pop = jvms()->nof_monitors());
  const int MonitorEdges = (1<<JVMState::logMonitorEdges);
  int endoff = jvms()->endoff();
  int new_endoff = endoff - MonitorEdges;
  jvms()->set_endoff(new_endoff);
  while (endoff > new_endoff)  del_req(--endoff);
  assert(jvms()->nof_monitors() == num_before_pop-1, "");
}

Node *SafePointNode::peek_monitor_box() const {
  int mon = jvms()->nof_monitors() - 1;
  assert(mon >= 0, "most have a monitor");
  return monitor_box(jvms(), mon);
}

Node *SafePointNode::peek_monitor_obj() const {
  int mon = jvms()->nof_monitors() - 1;
  assert(mon >= 0, "most have a monitor");
  return monitor_obj(jvms(), mon);
}

// Do we Match on this edge index or not?  Match no edges
uint SafePointNode::match_edge(uint idx) const {
  if( !needs_polling_address_input() )
    return 0;

  return (TypeFunc::Parms == idx);
}

//=============================================================================
uint AllocateNode::size_of() const { return sizeof(*this); }

AllocateNode::AllocateNode(Compile* C, const TypeFunc *atype,
                           Node *ctrl, Node *mem, Node *abio,
                           Node *size, Node *klass_node, Node *initial_test)
  : CallNode(atype, NULL, TypeRawPtr::BOTTOM)
{
  init_class_id(Class_Allocate);
  init_flags(Flag_is_macro);
  Node *topnode = C->top();

  init_req( TypeFunc::Control  , ctrl );
  init_req( TypeFunc::I_O      , abio );
  init_req( TypeFunc::Memory   , mem );
  init_req( TypeFunc::ReturnAdr, topnode );
  init_req( TypeFunc::FramePtr , topnode );
  init_req( AllocSize          , size);
  init_req( KlassNode          , klass_node);
  init_req( InitialTest        , initial_test);
  init_req( ALength            , topnode);
  C->add_macro_node(this);
}

//=============================================================================
uint AllocateArrayNode::size_of() const { return sizeof(*this); }

//=============================================================================
uint LockNode::size_of() const { return sizeof(*this); }

// Redundant lock elimination
//
// There are various patterns of locking where we release and
// immediately reacquire a lock in a piece of code where no operations
// occur in between that would be observable.  In those cases we can
// skip releasing and reacquiring the lock without violating any
// fairness requirements.  Doing this around a loop could cause a lock
// to be held for a very long time so we concentrate on non-looping
// control flow.  We also require that the operations are fully
// redundant meaning that we don't introduce new lock operations on
// some paths so to be able to eliminate it on others ala PRE.  This
// would probably require some more extensive graph manipulation to
// guarantee that the memory edges were all handled correctly.
//
// Assuming p is a simple predicate which can't trap in any way and s
// is a synchronized method consider this code:
//
//   s();
//   if (p)
//     s();
//   else
//     s();
//   s();
//
// 1. The unlocks of the first call to s can be eliminated if the
// locks inside the then and else branches are eliminated.
//
// 2. The unlocks of the then and else branches can be eliminated if
// the lock of the final call to s is eliminated.
//
// Either of these cases subsumes the simple case of sequential control flow
//
// Addtionally we can eliminate versions without the else case:
//
//   s();
//   if (p)
//     s();
//   s();
//
// 3. In this case we eliminate the unlock of the first s, the lock
// and unlock in the then case and the lock in the final s.
//
// Note also that in all these cases the then/else pieces don't have
// to be trivial as long as they begin and end with synchronization
// operations.
//
//   s();
//   if (p)
//     s();
//     f();
//     s();
//   s();
//
// The code will work properly for this case, leaving in the unlock
// before the call to f and the relock after it.
//
// A potentially interesting case which isn't handled here is when the
// locking is partially redundant.
//
//   s();
//   if (p)
//     s();
//
// This could be eliminated putting unlocking on the else case and
// eliminating the first unlock and the lock in the then side.
// Alternatively the unlock could be moved out of the then side so it
// was after the merge and the first unlock and second lock
// eliminated.  This might require less manipulation of the memory
// state to get correct.
//
// Additionally we might allow work between a unlock and lock before
// giving up eliminating the locks.  The current code disallows any
// conditional control flow between these operations.  A formulation
// similar to partial redundancy elimination computing the
// availability of unlocking and the anticipatability of locking at a
// program point would allow detection of fully redundant locking with
// some amount of work in between.  I'm not sure how often I really
// think that would occur though.  Most of the cases I've seen
// indicate it's likely non-trivial work would occur in between.
// There may be other more complicated constructs where we could
// eliminate locking but I haven't seen any others appear as hot or
// interesting.
//
// Locking and unlocking have a canonical form in ideal that looks
// roughly like this:
//
//              <obj>
//                | \\------+
//                |  \       \
//                | BoxLock   \
//                |  |   |     \
//                |  |    \     \
//                |  |   FastLock
//                |  |   /
//                |  |  /
//                |  |  |
//
//               Lock
//                |
//            Proj #0
//                |
//            MembarAcquire
//                |
//            Proj #0
//
//            MembarRelease
//                |
//            Proj #0
//                |
//              Unlock
//                |
//            Proj #0
//
//
// This code proceeds by processing Lock nodes during PhaseIterGVN
// and searching back through its control for the proper code
// patterns.  Once it finds a set of lock and unlock operations to
// eliminate they are marked as eliminatable which causes the
// expansion of the Lock and Unlock macro nodes to make the operation a NOP
//
//=============================================================================

//
// Utility function to skip over uninteresting control nodes.  Nodes skipped are:
//   - copy regions.  (These may not have been optimized away yet.)
//   - eliminated locking nodes
//
static Node *next_control(Node *ctrl) {
  if (ctrl == NULL)
    return NULL;
  while (1) {
    if (ctrl->is_Region()) {
      RegionNode *r = ctrl->as_Region();
      Node *n = r->is_copy();
      if (n == NULL)
        break;  // hit a region, return it
      else
        ctrl = n;
    } else if (ctrl->is_Proj()) {
      Node *in0 = ctrl->in(0);
      if (in0->is_AbstractLock() && in0->as_AbstractLock()->is_eliminated()) {
        ctrl = in0->in(0);
      } else {
        break;
      }
    } else {
      break; // found an interesting control
    }
  }
  return ctrl;
}
//
// Given a control, see if it's the control projection of an Unlock which
// operating on the same object as lock.
//
bool AbstractLockNode::find_matching_unlock(const Node* ctrl, LockNode* lock,
                                            GrowableArray<AbstractLockNode*> &lock_ops) {
  ProjNode *ctrl_proj = (ctrl->is_Proj()) ? ctrl->as_Proj() : NULL;
  if (ctrl_proj != NULL && ctrl_proj->_con == TypeFunc::Control) {
    Node *n = ctrl_proj->in(0);
    if (n != NULL && n->is_Unlock()) {
      UnlockNode *unlock = n->as_Unlock();
      if ((lock->obj_node() == unlock->obj_node()) &&
          (lock->box_node() == unlock->box_node()) && !unlock->is_eliminated()) {
        lock_ops.append(unlock);
        return true;
      }
    }
  }
  return false;
}

//
// Find the lock matching an unlock.  Returns null if a safepoint
// or complicated control is encountered first.
LockNode *AbstractLockNode::find_matching_lock(UnlockNode* unlock) {
  LockNode *lock_result = NULL;
  // find the matching lock, or an intervening safepoint
  Node *ctrl = next_control(unlock->in(0));
  while (1) {
    assert(ctrl != NULL, "invalid control graph");
    assert(!ctrl->is_Start(), "missing lock for unlock");
    if (ctrl->is_top()) break;  // dead control path
    if (ctrl->is_Proj()) ctrl = ctrl->in(0);
    if (ctrl->is_SafePoint()) {
        break;  // found a safepoint (may be the lock we are searching for)
    } else if (ctrl->is_Region()) {
      // Check for a simple diamond pattern.  Punt on anything more complicated
      if (ctrl->req() == 3 && ctrl->in(1) != NULL && ctrl->in(2) != NULL) {
        Node *in1 = next_control(ctrl->in(1));
        Node *in2 = next_control(ctrl->in(2));
        if (((in1->is_IfTrue() && in2->is_IfFalse()) ||
             (in2->is_IfTrue() && in1->is_IfFalse())) && (in1->in(0) == in2->in(0))) {
          ctrl = next_control(in1->in(0)->in(0));
        } else {
          break;
        }
      } else {
        break;
      }
    } else {
      ctrl = next_control(ctrl->in(0));  // keep searching
    }
  }
  if (ctrl->is_Lock()) {
    LockNode *lock = ctrl->as_Lock();
    if ((lock->obj_node() == unlock->obj_node()) &&
            (lock->box_node() == unlock->box_node())) {
      lock_result = lock;
    }
  }
  return lock_result;
}

// This code corresponds to case 3 above.

bool AbstractLockNode::find_lock_and_unlock_through_if(Node* node, LockNode* lock,
                                                       GrowableArray<AbstractLockNode*> &lock_ops) {
  Node* if_node = node->in(0);
  bool  if_true = node->is_IfTrue();

  if (if_node->is_If() && if_node->outcnt() == 2 && (if_true || node->is_IfFalse())) {
    Node *lock_ctrl = next_control(if_node->in(0));
    if (find_matching_unlock(lock_ctrl, lock, lock_ops)) {
      Node* lock1_node = NULL;
      ProjNode* proj = if_node->as_If()->proj_out(!if_true);
      if (if_true) {
        if (proj->is_IfFalse() && proj->outcnt() == 1) {
          lock1_node = proj->unique_out();
        }
      } else {
        if (proj->is_IfTrue() && proj->outcnt() == 1) {
          lock1_node = proj->unique_out();
        }
      }
      if (lock1_node != NULL && lock1_node->is_Lock()) {
        LockNode *lock1 = lock1_node->as_Lock();
        if ((lock->obj_node() == lock1->obj_node()) &&
            (lock->box_node() == lock1->box_node()) && !lock1->is_eliminated()) {
          lock_ops.append(lock1);
          return true;
        }
      }
    }
  }

  lock_ops.trunc_to(0);
  return false;
}

bool AbstractLockNode::find_unlocks_for_region(const RegionNode* region, LockNode* lock,
                               GrowableArray<AbstractLockNode*> &lock_ops) {
  // check each control merging at this point for a matching unlock.
  // in(0) should be self edge so skip it.
  for (int i = 1; i < (int)region->req(); i++) {
    Node *in_node = next_control(region->in(i));
    if (in_node != NULL) {
      if (find_matching_unlock(in_node, lock, lock_ops)) {
        // found a match so keep on checking.
        continue;
      } else if (find_lock_and_unlock_through_if(in_node, lock, lock_ops)) {
        continue;
      }

      // If we fall through to here then it was some kind of node we
      // don't understand or there wasn't a matching unlock, so give
      // up trying to merge locks.
      lock_ops.trunc_to(0);
      return false;
    }
  }
  return true;

}

#ifndef PRODUCT
//
// Create a counter which counts the number of times this lock is acquired
//
void AbstractLockNode::create_lock_counter(JVMState* state) {
  _counter = OptoRuntime::new_named_counter(state, NamedCounter::LockCounter);
}
#endif

void AbstractLockNode::set_eliminated() {
  _eliminate = true;
#ifndef PRODUCT
  if (_counter) {
    // Update the counter to indicate that this lock was eliminated.
    // The counter update code will stay around even though the
    // optimizer will eliminate the lock operation itself.
    _counter->set_tag(NamedCounter::EliminatedLockCounter);
  }
#endif
}

//=============================================================================
Node *LockNode::Ideal(PhaseGVN *phase, bool can_reshape) {

  // perform any generic optimizations first
  Node *result = SafePointNode::Ideal(phase, can_reshape);

  // Now see if we can optimize away this lock.  We don't actually
  // remove the locking here, we simply set the _eliminate flag which
  // prevents macro expansion from expanding the lock.  Since we don't
  // modify the graph, the value returned from this function is the
  // one computed above.
  if (EliminateLocks && !is_eliminated()) {
    //
    // Try lock coarsening
    //
    PhaseIterGVN* iter = phase->is_IterGVN();
    if (iter != NULL) {

      GrowableArray<AbstractLockNode*>   lock_ops;

      Node *ctrl = next_control(in(0));

      // now search back for a matching Unlock
      if (find_matching_unlock(ctrl, this, lock_ops)) {
        // found an unlock directly preceding this lock.  This is the
        // case of single unlock directly control dependent on a
        // single lock which is the trivial version of case 1 or 2.
      } else if (ctrl->is_Region() ) {
        if (find_unlocks_for_region(ctrl->as_Region(), this, lock_ops)) {
        // found lock preceded by multiple unlocks along all paths
        // joining at this point which is case 3 in description above.
        }
      } else {
        // see if this lock comes from either half of an if and the
        // predecessors merges unlocks and the other half of the if
        // performs a lock.
        if (find_lock_and_unlock_through_if(ctrl, this, lock_ops)) {
          // found unlock splitting to an if with locks on both branches.
        }
      }

      if (lock_ops.length() > 0) {
        // add ourselves to the list of locks to be eliminated.
        lock_ops.append(this);

  #ifndef PRODUCT
        if (PrintEliminateLocks) {
          int locks = 0;
          int unlocks = 0;
          for (int i = 0; i < lock_ops.length(); i++) {
            AbstractLockNode* lock = lock_ops.at(i);
            if (lock->Opcode() == Op_Lock) locks++;
            else                               unlocks++;
            if (Verbose) {
              lock->dump(1);
            }
          }
          tty->print_cr("***Eliminated %d unlocks and %d locks", unlocks, locks);
        }
  #endif

        // for each of the identified locks, mark them
        // as eliminatable
        for (int i = 0; i < lock_ops.length(); i++) {
          AbstractLockNode* lock = lock_ops.at(i);

          // Mark it eliminated to update any counters
          lock->set_eliminated();
        }
      } else if (result != NULL && ctrl->is_Region() &&
                 iter->_worklist.member(ctrl)) {
        // We weren't able to find any opportunities but the region this
        // lock is control dependent on hasn't been processed yet so put
        // this lock back on the worklist so we can check again once any
        // region simplification has occurred.
        iter->_worklist.push(this);
      }
    }
  }

  return result;
}

//=============================================================================
uint UnlockNode::size_of() const { return sizeof(*this); }

//=============================================================================
Node *UnlockNode::Ideal(PhaseGVN *phase, bool can_reshape) {

  // perform any generic optimizations first
  Node * result = SafePointNode::Ideal(phase, can_reshape);

  // Now see if we can optimize away this unlock.  We don't actually
  // remove the unlocking here, we simply set the _eliminate flag which
  // prevents macro expansion from expanding the unlock.  Since we don't
  // modify the graph, the value returned from this function is the
  // one computed above.
  if (EliminateLocks && !is_eliminated()) {
    //
    // If we are unlocking an unescaped object, the lock/unlock is unnecessary
    // We can eliminate them if there are no safepoints in the locked region.
    //
    ConnectionGraph *cgr = Compile::current()->congraph();
    if (cgr != NULL && cgr->escape_state(obj_node(), phase) == PointsToNode::NoEscape) {
      GrowableArray<AbstractLockNode*>   lock_ops;
      LockNode *lock = find_matching_lock(this);
      if (lock != NULL) {
        lock_ops.append(this);
        lock_ops.append(lock);
        // find other unlocks which pair with the lock we found and add them
        // to the list
        Node * box = box_node();

        for (DUIterator_Fast imax, i = box->fast_outs(imax); i < imax; i++) {
          Node *use = box->fast_out(i);
          if (use->is_Unlock() && use != this) {
            UnlockNode *unlock1 = use->as_Unlock();
            if (!unlock1->is_eliminated()) {
              LockNode *lock1 = find_matching_lock(unlock1);
              if (lock == lock1)
                lock_ops.append(unlock1);
              else if (lock1 == NULL) {
               // we can't find a matching lock, we must assume the worst
                lock_ops.trunc_to(0);
                break;
              }
            }
          }
        }
        if (lock_ops.length() > 0) {

  #ifndef PRODUCT
          if (PrintEliminateLocks) {
            int locks = 0;
            int unlocks = 0;
            for (int i = 0; i < lock_ops.length(); i++) {
              AbstractLockNode* lock = lock_ops.at(i);
              if (lock->Opcode() == Op_Lock) locks++;
              else                               unlocks++;
              if (Verbose) {
                lock->dump(1);
              }
            }
            tty->print_cr("***Eliminated %d unescaped unlocks and %d unescaped locks", unlocks, locks);
          }
  #endif

          // for each of the identified locks, mark them
          // as eliminatable
          for (int i = 0; i < lock_ops.length(); i++) {
            AbstractLockNode* lock = lock_ops.at(i);

            // Mark it eliminated to update any counters
            lock->set_eliminated();
          }
        }
      }
    }
  }
  return result;
}