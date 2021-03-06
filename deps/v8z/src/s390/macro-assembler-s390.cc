// Copyright 2012 the V8 project authors. All rights reserved.
//
// Copyright IBM Corp. 2012-2014. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <limits.h>  // For LONG_MIN, LONG_MAX.
#include <assert.h>  // For assert

#include "v8.h"

#if defined(V8_TARGET_ARCH_S390)

#include "bootstrapper.h"
#include "codegen.h"
#include "debug.h"
#include "runtime.h"

namespace v8 {
namespace internal {


MacroAssembler::MacroAssembler(Isolate* arg_isolate, void* buffer, int size)
    : Assembler(arg_isolate, buffer, size),
      generating_stub_(false),
      allow_stub_calls_(true),
      has_frame_(false) {
  if (isolate() != NULL) {
    code_object_ = Handle<Object>(isolate()->heap()->undefined_value(),
                                  isolate());
  }
}


void MacroAssembler::Jump(Register target, Condition cond) {
  ASSERT(cond == al);
  b(cond, target);
}


void MacroAssembler::Jump(intptr_t target, RelocInfo::Mode rmode,
                          Condition cond, CRegister) {
  Label skip;

  if (cond != al)
    b(NegateCondition(cond), &skip);

  ASSERT(rmode == RelocInfo::CODE_TARGET || rmode == RelocInfo::RUNTIME_ENTRY);

  mov(ip, Operand(target, rmode));
  b(ip);

  bind(&skip);
}


void MacroAssembler::Jump(Address target, RelocInfo::Mode rmode,
                          Condition cond, CRegister cr) {
  ASSERT(!RelocInfo::IsCodeTarget(rmode));
  Jump(reinterpret_cast<intptr_t>(target), rmode, cond, cr);
}


void MacroAssembler::Jump(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond) {
  ASSERT(RelocInfo::IsCodeTarget(rmode));
  // 'code' is always generated ppc code, never THUMB code
  Jump(reinterpret_cast<intptr_t>(code.location()), rmode, cond);
}


int MacroAssembler::CallSize(Register target, Condition cond) {
  // 2-byte BASR is used to dispatch.
  return 2;
}


void MacroAssembler::Call(Register target, Condition cond) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Label start;
  bind(&start);
  ASSERT(cond == al);  // in prep of removal of condition

  // Statement positions are expected to be recorded when the target
  // address is loaded.
  positions_recorder()->WriteRecordedPositions();

  // Branch to target via indirect branch
  basr(r14, target);

  ASSERT_EQ(CallSize(target, cond), SizeOfCodeGeneratedSince(&start));
}


int MacroAssembler::CallSize(
    Address target, RelocInfo::Mode rmode, Condition cond) {
  // S390 Assembler::move sequence are IILF / IIHF
  int size;
#if V8_TARGET_ARCH_S390X
  size = 2 + 12;  // IILF + IIHF + BASR
#else
  size = 2 + 6;   // IILF + BASR
#endif
  return size;
}


int MacroAssembler::CallSizeNotPredictableCodeSize(
    Address target, RelocInfo::Mode rmode, Condition cond) {
  // S390 Assembler::move sequence are IILF / IIHF
  int size;
#if V8_TARGET_ARCH_S390X
  size = 2 + 12;  // IILF + IIHF + BASR
#else
  size = 2 + 6;  // IILF + BASR
#endif
  return size;
}


void MacroAssembler::Call(Address target,
                          RelocInfo::Mode rmode,
                          Condition cond) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  ASSERT(cond == al);
  Label start;
  bind(&start);

  // Statement positions are expected to be recorded when the target
  // address is loaded.
  positions_recorder()->WriteRecordedPositions();

  mov(ip, Operand(reinterpret_cast<intptr_t>(target), rmode));
  basr(r14, ip);

#if V8_TARGET_ARCH_S390X
  ASSERT(kCallTargetAddressOffset == 14);
#else
  ASSERT(kCallTargetAddressOffset == 8);
#endif
  ASSERT_EQ(CallSize(target, rmode, cond), SizeOfCodeGeneratedSince(&start));
}


int MacroAssembler::CallSize(Handle<Code> code,
                             RelocInfo::Mode rmode,
                             TypeFeedbackId ast_id,
                             Condition cond) {
  return CallSize(reinterpret_cast<Address>(code.location()), rmode, cond);
}


void MacroAssembler::Call(Handle<Code> code,
                          RelocInfo::Mode rmode,
                          TypeFeedbackId ast_id,
                          Condition cond) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Label start;
  bind(&start);
  ASSERT(RelocInfo::IsCodeTarget(rmode));
  if (rmode == RelocInfo::CODE_TARGET && !ast_id.IsNone()) {
    SetRecordedAstId(ast_id);
    rmode = RelocInfo::CODE_TARGET_WITH_ID;
  }
  Call(reinterpret_cast<Address>(code.location()), rmode, cond);
  ASSERT_EQ(CallSize(code, rmode, ast_id, cond),
            SizeOfCodeGeneratedSince(&start));
}


void MacroAssembler::Ret() {
  b(r14);
}

void MacroAssembler::Drop(int count) {
  if (count > 0) {
    la(sp, MemOperand(sp, count * kPointerSize));
  }
}

void MacroAssembler::Ret(int drop) {
  Drop(drop);
  Ret();
}

void MacroAssembler::Call(Label* target) {
  b(r14, target);
}

void MacroAssembler::Push(Handle<Object> handle) {
  mov(ip, Operand(handle));
  push(ip);
}

void MacroAssembler::Move(Register dst, Handle<Object> value) {
  mov(dst, Operand(value));
}

void MacroAssembler::Move(Register dst, Register src, Condition cond) {
  if (!dst.is(src)) {
    LoadRR(dst, src);
  }
}

void MacroAssembler::Move(DoubleRegister dst, DoubleRegister src) {
  if (!dst.is(src)) {
    ldr(dst, src);
  }
}

void MacroAssembler::MultiPush(RegList regs) {
  int16_t num_to_push = NumberOfBitsSet(regs);
  int16_t stack_offset = num_to_push * kPointerSize;

  lay(sp, MemOperand(sp, -stack_offset));
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs & (1 << i)) != 0) {
      stack_offset -= kPointerSize;
      StoreP(ToRegister(i), MemOperand(sp, stack_offset));
    }
  }
}

void MacroAssembler::MultiPop(RegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs & (1 << i)) != 0) {
      LoadP(ToRegister(i), MemOperand(sp, stack_offset));
      stack_offset += kPointerSize;
    }
  }
  la(sp, MemOperand(sp, stack_offset));
}


void MacroAssembler::LoadRoot(Register destination,
                              Heap::RootListIndex index,
                              Condition cond) {
  ASSERT(cond == al);
  LoadP(destination, MemOperand(kRootRegister,
                                index << kPointerSizeLog2), r0);
}


void MacroAssembler::StoreRoot(Register source,
                               Heap::RootListIndex index,
                               Condition cond) {
  ASSERT(cond == al);
  StoreP(source, MemOperand(kRootRegister, index << kPointerSizeLog2));
}


void MacroAssembler::LoadHeapObject(Register result,
                                    Handle<HeapObject> object) {
  if (isolate()->heap()->InNewSpace(*object)) {
    Handle<JSGlobalPropertyCell> cell =
        isolate()->factory()->NewJSGlobalPropertyCell(object);
    mov(result, Operand(cell));
    LoadP(result, FieldMemOperand(result, JSGlobalPropertyCell::kValueOffset));
  } else {
    mov(result, Operand(object));
  }
}


void MacroAssembler::InNewSpace(Register object,
                                Register scratch,
                                Condition cond,
                                Label* branch) {
  // N.B. scratch may be same register as object
  ASSERT(cond == eq || cond == ne);
  mov(r0, Operand(ExternalReference::new_space_mask(isolate())));

  AndP(scratch, object, r0);
  mov(r0, Operand(ExternalReference::new_space_start(isolate())));
  CmpP(scratch, r0);
  b(cond, branch);
}


void MacroAssembler::RecordWriteField(
    Register object,
    int offset,
    Register value,
    Register dst,
    LinkRegisterStatus lr_status,
    SaveFPRegsMode save_fp,
    RememberedSetAction remembered_set_action,
    SmiCheck smi_check) {
  // First, check if a write barrier is even needed. The tests below
  // catch stores of Smis.
  Label done;

  // Skip barrier if writing a smi.
  if (smi_check == INLINE_SMI_CHECK) {
    JumpIfSmi(value, &done);
  }

  // Although the object register is tagged, the offset is relative to the start
  // of the object, so so offset must be a multiple of kPointerSize.
  ASSERT(IsAligned(offset, kPointerSize));

  AddP(dst, object, Operand(offset - kHeapObjectTag));
  if (emit_debug_code()) {
    Label ok;
    AndP(r0, dst, Operand((1 << kPointerSizeLog2) - 1));
    beq(&ok, Label::kNear);
    stop("Unaligned cell in write barrier");
    bind(&ok);
  }

  RecordWrite(object,
              dst,
              value,
              lr_status,
              save_fp,
              remembered_set_action,
              OMIT_SMI_CHECK);

  bind(&done);

  // Clobber clobbered input registers when running with the debug-code flag
  // turned on to provoke errors.
  if (emit_debug_code()) {
    mov(value, Operand(BitCast<intptr_t>(kZapValue + 4)));
    mov(dst, Operand(BitCast<intptr_t>(kZapValue + 8)));
  }
}


// Will clobber 4 registers: object, address, scratch, ip.  The
// register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWrite(Register object,
                                 Register address,
                                 Register value,
                                 LinkRegisterStatus lr_status,
                                 SaveFPRegsMode fp_mode,
                                 RememberedSetAction remembered_set_action,
                                 SmiCheck smi_check) {
  // The compiled code assumes that record write doesn't change the
  // context register, so we check that none of the clobbered
  // registers are cp.
  ASSERT(!address.is(cp) && !value.is(cp));

  if (emit_debug_code()) {
    CmpP(value, MemOperand(address));
    Check(eq, "Wrong address or value passed to RecordWrite");
  }

  Label done;

  if (smi_check == INLINE_SMI_CHECK) {
    JumpIfSmi(value, &done);
  }

  CheckPageFlag(value,
                value,  // Used as scratch.
                MemoryChunk::kPointersToHereAreInterestingMask,
                eq,
                &done);
  CheckPageFlag(object,
                value,  // Used as scratch.
                MemoryChunk::kPointersFromHereAreInterestingMask,
                eq,
                &done);

  // Record the actual write.
  if (lr_status == kLRHasNotBeenSaved) {
    push(r14);
  }
  RecordWriteStub stub(object, value, address, remembered_set_action, fp_mode);
  CallStub(&stub);
  if (lr_status == kLRHasNotBeenSaved) {
    pop(r14);
  }

  bind(&done);

  // Clobber clobbered registers when running with the debug-code flag
  // turned on to provoke errors.
  if (emit_debug_code()) {
    mov(address, Operand(BitCast<intptr_t>(kZapValue + 12)));
    mov(value, Operand(BitCast<intptr_t>(kZapValue + 16)));
  }
}


void MacroAssembler::RememberedSetHelper(Register object,  // For debug tests.
                                         Register address,
                                         Register scratch,
                                         SaveFPRegsMode fp_mode,
                                         RememberedSetFinalAction and_then) {
  Label done;
  if (emit_debug_code()) {
    Label ok;
    JumpIfNotInNewSpace(object, scratch, &ok);
    stop("Remembered set pointer is in new space");
    bind(&ok);
  }
  // Load store buffer top.
  ExternalReference store_buffer =
      ExternalReference::store_buffer_top(isolate());
  mov(ip, Operand(store_buffer));
  LoadP(scratch, MemOperand(ip));
  // Store pointer to buffer and increment buffer top.
  StoreP(address, MemOperand(scratch));
  AddP(scratch, Operand(kPointerSize));
  // Write back new top of buffer.
  StoreP(scratch, MemOperand(ip));
  // Call stub on end of buffer.
  // Check for end of buffer.
  AndP(r0, scratch, Operand(StoreBuffer::kStoreBufferOverflowBit));

  if (and_then == kFallThroughAtEnd) {
    beq(&done, Label::kNear);
  } else {
    ASSERT(and_then == kReturnAtEnd);
    beq(&done, Label::kNear);
  }
  push(r14);
  StoreBufferOverflowStub store_buffer_overflow =
      StoreBufferOverflowStub(fp_mode);
  CallStub(&store_buffer_overflow);
  pop(r14);
  bind(&done);
  if (and_then == kReturnAtEnd) {
    Ret();
  }
}


// Push and pop all registers that can hold pointers.
void MacroAssembler::PushSafepointRegisters() {
  // Safepoints expect a block of kNumSafepointRegisters values on the
  // stack, so adjust the stack for unsaved registers.
  const int num_unsaved = kNumSafepointRegisters - kNumSafepointSavedRegisters;
  ASSERT(num_unsaved >= 0);
  if (num_unsaved > 0) {
    lay(sp, MemOperand(sp, -(num_unsaved * kPointerSize)));
  }
  MultiPush(kSafepointSavedRegisters);
}


void MacroAssembler::PopSafepointRegisters() {
  const int num_unsaved = kNumSafepointRegisters - kNumSafepointSavedRegisters;
  MultiPop(kSafepointSavedRegisters);
  if (num_unsaved > 0) {
    la(sp, MemOperand(sp, num_unsaved * kPointerSize));
  }
}


void MacroAssembler::StoreToSafepointRegisterSlot(Register src, Register dst) {
  StoreP(src, SafepointRegisterSlot(dst));
}


void MacroAssembler::LoadFromSafepointRegisterSlot(Register dst, Register src) {
  LoadP(dst, SafepointRegisterSlot(src));
}


int MacroAssembler::SafepointRegisterStackIndex(int reg_code) {
  // The registers are pushed starting with the highest encoding,
  // which means that lowest encodings are closest to the stack pointer.
  RegList regs = kSafepointSavedRegisters;
  int index = 0;

  ASSERT(reg_code >= 0 && reg_code < kNumRegisters);

  for (int16_t i = 0; i < reg_code; i++) {
    if ((regs & (1 << i)) != 0) {
      index++;
    }
  }

  return index;
}


MemOperand MacroAssembler::SafepointRegisterSlot(Register reg) {
  return MemOperand(sp, SafepointRegisterStackIndex(reg.code()) * kPointerSize);
}


MemOperand MacroAssembler::SafepointRegistersAndDoublesSlot(Register reg) {
  // General purpose registers are pushed last on the stack.
  int doubles_size = DoubleRegister::kNumAllocatableRegisters * kDoubleSize;
  int register_offset = SafepointRegisterStackIndex(reg.code()) * kPointerSize;
  return MemOperand(sp, doubles_size + register_offset);
}

// Used by FrameScope constructor to enter frame.
void MacroAssembler::EnterFrame(StackFrame::Type type) {
  // We create a stack frame with:
  //    Return Addr <-- old sp
  //    Old FP      <-- new fp
  //    CP
  //    type
  //    CodeObject  <-- new sp
  lay(sp, MemOperand(sp, -5 * kPointerSize));
  CleanseP(r14);
  StoreP(r14, MemOperand(sp, 4 * kPointerSize));
  StoreP(fp, MemOperand(sp, 3 * kPointerSize));
  StoreP(cp, MemOperand(sp, 2 * kPointerSize));
  LoadSmiLiteral(r0, Smi::FromInt(type));
  StoreP(r0, MemOperand(sp, 1 * kPointerSize));
  mov(r0, Operand(CodeObject()));
  StoreP(r0, MemOperand(sp, 0 * kPointerSize));
  la(fp, MemOperand(sp, 3 * kPointerSize));  // Adjust FP to point to saved FP
}

void MacroAssembler::LeaveFrame(StackFrame::Type type) {
  // Drop the execution stack down to the frame pointer and restore
  // the caller frame pointer and return address.
  LoadRR(sp, fp);
  LoadP(fp, MemOperand(sp));
  LoadP(r14, MemOperand(sp, kPointerSize));
  la(sp, MemOperand(sp, 2 * kPointerSize));
}

// ExitFrame layout (probably wrongish.. needs updating)
//
//  SP -> previousSP
//        LK reserved
//        code
//        sp_on_exit (for debug?)
// oldSP->prev SP
//        LK
//        <parameters on stack>

// Prior to calling EnterExitFrame, we've got a bunch of parameters
// on the stack that we need to wrap a real frame around.. so first
// we reserve a slot for LK and push the previous SP which is captured
// in the fp register (r11)
// Then - we buy a new frame

void MacroAssembler::EnterExitFrame(bool save_doubles, int stack_space) {
  // Set up the frame structure on the stack.
  ASSERT_EQ(2 * kPointerSize, ExitFrameConstants::kCallerSPDisplacement);
  ASSERT_EQ(1 * kPointerSize, ExitFrameConstants::kCallerPCOffset);
  ASSERT_EQ(0 * kPointerSize, ExitFrameConstants::kCallerFPOffset);
  ASSERT(stack_space > 0);

  // This is an opportunity to build a frame to wrap
  // all of the pushes that have happened inside of V8
  // since we were called from C code

  // replicate ARM frame - TODO make this more closely follow PPC ABI

  // @TODO This is a temporary workaround until we figure out where to
  // appropriately cleanse the top nibble of 31-bit pointers.
  CleanseP(r14);

  Push(r14, fp);
  LoadRR(fp, sp);
  // Reserve room for saved entry sp and code object.
  lay(sp, MemOperand(sp, -2 * kPointerSize));

  if (emit_debug_code()) {
    LoadImmP(r1, Operand::Zero());
    StoreP(r1, MemOperand(fp, ExitFrameConstants::kSPOffset));
  }
  mov(r1, Operand(CodeObject()));
  StoreP(r1, MemOperand(fp, ExitFrameConstants::kCodeOffset));

  // Save the frame pointer and the context in top.
  mov(r1, Operand(ExternalReference(Isolate::kCEntryFPAddress, isolate())));
  StoreP(fp, MemOperand(r1));
  mov(r1, Operand(ExternalReference(Isolate::kContextAddress, isolate())));
  StoreP(cp, MemOperand(r1));

  // Optionally save all volatile double registers.
  if (save_doubles) {
    const int kNumRegs = DoubleRegister::kNumVolatileRegisters;
    lay(sp, MemOperand(sp, -kNumRegs * kDoubleSize));
#define StoreFloatingPointRegisterToStack(reg, offset) \
    StoreF(DoubleRegister::from_code(reg), \
      MemOperand(sp, (offset) * kDoubleSize));
#ifdef V8_TARGET_ARCH_S390X
    for (int i = 0; i < 7; i++) {
      StoreFloatingPointRegisterToStack(i, i);
    }
#else
    StoreFloatingPointRegisterToStack(0, 0);
    StoreFloatingPointRegisterToStack(1, 1);
    StoreFloatingPointRegisterToStack(2, 2);
    StoreFloatingPointRegisterToStack(3, 3);
    StoreFloatingPointRegisterToStack(5, 4);
    int offset = 5;
    for (int i = 7; i < DoubleRegister::kNumRegisters; i++, offset++) {
      StoreFloatingPointRegisterToStack(i, offset);
    }
#endif
#undef StoreFloatingPointRegisterToStack
  }

  // Allocate and align the frame preparing for calling the runtime
  // function.
  stack_space += kNumRequiredStackFrameSlots;
  lay(sp, MemOperand(sp, -stack_space * kPointerSize));
  const int frame_alignment = MacroAssembler::ActivationFrameAlignment();
  if (frame_alignment > 0) {
    ASSERT(frame_alignment == 8);
    ClearRightImm(sp, sp, Operand(3));  // equivalent to &= -8
  }

  // Set the exit frame sp value to point just before the return address
  // location.
  lay(r1, MemOperand(sp, kStackFrameSPSlot * kPointerSize));
  StoreP(r1, MemOperand(fp, ExitFrameConstants::kSPOffset));
}


void MacroAssembler::InitializeNewString(Register string,
                                         Register length,
                                         Heap::RootListIndex map_index,
                                         Register scratch1,
                                         Register scratch2) {
  SmiTag(scratch1, length);
  LoadRoot(scratch2, map_index);
  StoreP(scratch1, FieldMemOperand(string, String::kLengthOffset));
  LoadImmP(scratch1, Operand(String::kEmptyHashField));
  StoreP(scratch2, FieldMemOperand(string, HeapObject::kMapOffset));
  StoreP(scratch1, FieldMemOperand(string, String::kHashFieldSlot));
}


int MacroAssembler::ActivationFrameAlignment() {
#if !defined(USE_SIMULATOR)
  // Running on the real platform. Use the alignment as mandated by the local
  // environment.
  // Note: This will break if we ever start generating snapshots on one S390
  // platform for another S390 platform with a different alignment.
  return OS::ActivationFrameAlignment();
#else  // Simulated
  // If we are using the simulator then we should always align to the expected
  // alignment. As the simulator is used to generate snapshots we do not know
  // if the target platform will need alignment, so this is controlled from a
  // flag.
  return FLAG_sim_stack_alignment;
#endif
}


void MacroAssembler::LeaveExitFrame(bool save_doubles,
                                    Register argument_count) {
  // Optionally restore all double registers.
  if (save_doubles) {
    // Calculate the stack location of the saved doubles and restore them.
    const int kNumRegs = DoubleRegister::kNumVolatileRegisters;
    lay(sp, MemOperand(fp, -(2 * kPointerSize + kNumRegs * kDoubleSize)));
#define LoadFloatingPointRegisterToStack(reg, offset) \
    LoadF(DoubleRegister::from_code(reg), \
      MemOperand(sp, (offset) * kDoubleSize));
#ifdef V8_TARGET_ARCH_S390X
    for (int i = 0; i < 7; i++) {
      LoadFloatingPointRegisterToStack(i, i);
    }
#else
    LoadFloatingPointRegisterToStack(0, 0);
    LoadFloatingPointRegisterToStack(1, 1);
    LoadFloatingPointRegisterToStack(2, 2);
    LoadFloatingPointRegisterToStack(3, 3);
    LoadFloatingPointRegisterToStack(5, 4);
    int offset = 5;
    for (int i = 7; i < DoubleRegister::kNumRegisters; i++, offset++) {
      LoadFloatingPointRegisterToStack(i, offset);
    }
#endif
#undef LoadFloatingPointRegisterToStack
  }

  // Clear top frame.
  LoadImmP(r0, Operand(0, RelocInfo::NONE));
  mov(ip, Operand(ExternalReference(Isolate::kCEntryFPAddress, isolate())));
  StoreP(r0, MemOperand(ip));

  // Restore current context from top and clear it in debug mode.
  mov(ip, Operand(ExternalReference(Isolate::kContextAddress, isolate())));
  LoadP(cp, MemOperand(ip));
#ifdef DEBUG
  StoreP(r0, MemOperand(ip));
#endif

  // Tear down the exit frame, pop the arguments, and return.
  LoadRR(sp, fp);
  pop(fp);
  pop(r14);

  if (argument_count.is_valid()) {
    ShiftLeftP(argument_count, argument_count, Operand(kPointerSizeLog2));
    la(sp, MemOperand(sp, argument_count));
  }
}

void MacroAssembler::GetCFunctionDoubleResult(const DoubleRegister dst) {
  ldr(dst, d0);
}


void MacroAssembler::SetCallKind(Register dst, CallKind call_kind) {
  // This macro takes the dst register to make the code more readable
  // at the call sites. However, the dst register has to be r7 to
  // follow the calling convention which requires the call type to be
  // in r7.
  ASSERT(dst.is(r7));
  if (call_kind == CALL_AS_FUNCTION) {
    LoadSmiLiteral(dst, Smi::FromInt(1));
  } else {
    LoadSmiLiteral(dst, Smi::FromInt(0));
  }
}


void MacroAssembler::InvokePrologue(const ParameterCount& expected,
                                    const ParameterCount& actual,
                                    Handle<Code> code_constant,
                                    Register code_reg,
                                    Label* done,
                                    bool* definitely_mismatches,
                                    InvokeFlag flag,
                                    const CallWrapper& call_wrapper,
                                    CallKind call_kind) {
  bool definitely_matches = false;
  *definitely_mismatches = false;
  Label regular_invoke;

  // Check whether the expected and actual arguments count match. If not,
  // setup registers according to contract with ArgumentsAdaptorTrampoline:
  //  r2: actual arguments count
  //  r3: function (passed through to callee)
  //  r4: expected arguments count
  //  r5: callee code entry

  // The code below is made a lot easier because the calling code already sets
  // up actual and expected registers according to the contract if values are
  // passed in registers.

  // roohack - remove these 3 checks temporarily
  //  ASSERT(actual.is_immediate() || actual.reg().is(r2));
  //  ASSERT(expected.is_immediate() || expected.reg().is(r4));
  //  ASSERT((!code_constant.is_null() && code_reg.is(no_reg))
  //          || code_reg.is(r5));

  if (expected.is_immediate()) {
    ASSERT(actual.is_immediate());
    if (expected.immediate() == actual.immediate()) {
      definitely_matches = true;
    } else {
      mov(r2, Operand(actual.immediate()));
      const int sentinel = SharedFunctionInfo::kDontAdaptArgumentsSentinel;
      if (expected.immediate() == sentinel) {
        // Don't worry about adapting arguments for builtins that
        // don't want that done. Skip adaption code by making it look
        // like we have a match between expected and actual number of
        // arguments.
        definitely_matches = true;
      } else {
        *definitely_mismatches = true;
        mov(r4, Operand(expected.immediate()));
      }
    }
  } else {
    if (actual.is_immediate()) {
      CmpPH(expected.reg(), Operand(actual.immediate()));
      beq(&regular_invoke);
      mov(r2, Operand(actual.immediate()));
    } else {
      CmpP(expected.reg(), actual.reg());
      beq(&regular_invoke);
    }
  }

  if (!definitely_matches) {
    if (!code_constant.is_null()) {
      mov(r5, Operand(code_constant));
      AddP(r5, Operand(Code::kHeaderSize - kHeapObjectTag));
    }

    Handle<Code> adaptor =
        isolate()->builtins()->ArgumentsAdaptorTrampoline();
    if (flag == CALL_FUNCTION) {
      call_wrapper.BeforeCall(CallSize(adaptor));
      SetCallKind(r7, call_kind);
      Call(adaptor);
      call_wrapper.AfterCall();
      if (!*definitely_mismatches) {
        b(done);
      }
    } else {
      SetCallKind(r7, call_kind);
      Jump(adaptor, RelocInfo::CODE_TARGET);
    }
    bind(&regular_invoke);
  }
}


void MacroAssembler::InvokeCode(Register code,
                                const ParameterCount& expected,
                                const ParameterCount& actual,
                                InvokeFlag flag,
                                const CallWrapper& call_wrapper,
                                CallKind call_kind) {
  // You can't call a function without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  Label done;
  bool definitely_mismatches = false;
  InvokePrologue(expected, actual, Handle<Code>::null(), code,
                 &done, &definitely_mismatches, flag,
                 call_wrapper, call_kind);
  if (!definitely_mismatches) {
    if (flag == CALL_FUNCTION) {
      call_wrapper.BeforeCall(CallSize(code));
      SetCallKind(r7, call_kind);
      Call(code);
      call_wrapper.AfterCall();
    } else {
      ASSERT(flag == JUMP_FUNCTION);
      SetCallKind(r7, call_kind);
      Jump(code);
    }

    // Continue here if InvokePrologue does handle the invocation due to
    // mismatched parameter counts.
    bind(&done);
  }
}


void MacroAssembler::InvokeCode(Handle<Code> code,
                                const ParameterCount& expected,
                                const ParameterCount& actual,
                                RelocInfo::Mode rmode,
                                InvokeFlag flag,
                                CallKind call_kind) {
  // You can't call a function without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  Label done;
  bool definitely_mismatches = false;
  InvokePrologue(expected, actual, code, no_reg,
                 &done, &definitely_mismatches, flag,
                 NullCallWrapper(), call_kind);
  if (!definitely_mismatches) {
    if (flag == CALL_FUNCTION) {
      SetCallKind(r7, call_kind);
      Call(code, rmode);
    } else {
      SetCallKind(r7, call_kind);
      Jump(code, rmode);
    }

    // Continue here if InvokePrologue does handle the invocation due to
    // mismatched parameter counts.
    bind(&done);
  }
}


void MacroAssembler::InvokeFunction(Register fun,
                                    const ParameterCount& actual,
                                    InvokeFlag flag,
                                    const CallWrapper& call_wrapper,
                                    CallKind call_kind) {
  // You can't call a function without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  // Contract with called JS functions requires that function is passed in r3.
  ASSERT(fun.is(r3));

  Register expected_reg = r4;
  Register code_reg = r5;

  LoadP(code_reg, FieldMemOperand(r3, JSFunction::kSharedFunctionInfoOffset));
  LoadP(cp, FieldMemOperand(r3, JSFunction::kContextOffset));
  LoadW(expected_reg, FieldMemOperand(code_reg,
                      SharedFunctionInfo::kFormalParameterCountOffset));
#if !defined(V8_TARGET_ARCH_S390X)
  SmiUntag(expected_reg);
#endif
  LoadP(code_reg,
        FieldMemOperand(r3, JSFunction::kCodeEntryOffset));

  ParameterCount expected(expected_reg);
  InvokeCode(code_reg, expected, actual, flag, call_wrapper, call_kind);
}


void MacroAssembler::InvokeFunction(Handle<JSFunction> function,
                                    const ParameterCount& actual,
                                    InvokeFlag flag,
                                    const CallWrapper& call_wrapper,
                                    CallKind call_kind) {
  // You can't call a function without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  // Get the function and setup the context.
  LoadHeapObject(r3, function);
  LoadP(cp, FieldMemOperand(r3, JSFunction::kContextOffset));

  ParameterCount expected(function->shared()->formal_parameter_count());
  // We call indirectly through the code field in the function to
  // allow recompilation to take effect without changing any of the
  // call sites.
  LoadP(r5, FieldMemOperand(r3, JSFunction::kCodeEntryOffset));
  InvokeCode(r5, expected, actual, flag, call_wrapper, call_kind);
}


void MacroAssembler::IsObjectJSObjectType(Register heap_object,
                                          Register map,
                                          Register scratch,
                                          Label* fail) {
  LoadP(map, FieldMemOperand(heap_object, HeapObject::kMapOffset));
  IsInstanceJSObjectType(map, scratch, fail);
}


void MacroAssembler::IsInstanceJSObjectType(Register map,
                                            Register scratch,
                                            Label* fail) {
  LoadlB(scratch, FieldMemOperand(map, Map::kInstanceTypeOffset));
  CmpP(scratch, Operand(FIRST_NONCALLABLE_SPEC_OBJECT_TYPE));
  blt(fail);
  CmpP(scratch, Operand(LAST_NONCALLABLE_SPEC_OBJECT_TYPE));
  bgt(fail);
}


void MacroAssembler::IsObjectJSStringType(Register object,
                                          Register scratch,
                                          Label* fail) {
  ASSERT(kNotStringTag != 0);

  LoadP(scratch, FieldMemOperand(object, HeapObject::kMapOffset));
  LoadlB(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  mov(r0, Operand(kIsNotStringMask));
  AndP(r0, scratch);
  bne(fail /*, cr0*/);
}


#ifdef ENABLE_DEBUGGER_SUPPORT
void MacroAssembler::DebugBreak() {
  LoadImmP(r2, Operand(0, RelocInfo::NONE));
  mov(r3, Operand(ExternalReference(Runtime::kDebugBreak, isolate())));
  CEntryStub ces(1);
  ASSERT(AllowThisStubCall(&ces));
  Call(ces.GetCode(), RelocInfo::DEBUG_BREAK);
}
#endif


void MacroAssembler::PushTryHandler(StackHandler::Kind kind,
                                    int handler_index) {
  // Adjust this code if not the case.
  STATIC_ASSERT(StackHandlerConstants::kSize == 5 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kCodeOffset == 1 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kStateSlot == 2 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kContextOffset == 3 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kFPOffset == 4 * kPointerSize);

  // For the JSEntry handler, we must preserve r2-r6,
  //   r0,r1,r7-r9 are available.
  //
  // We want the stack to look like
  // sp -> NextOffset
  //       CodeObject
  //       state
  //       context
  //       frame pointer

  // Link the current handler as the next handler.
  mov(r7, Operand(ExternalReference(Isolate::kHandlerAddress, isolate())));

  // Buy the full stack frame for 5 slots.
  lay(sp, MemOperand(sp,  -StackHandlerConstants::kSize));

  // Copy the old handler into the next handler slot.
  mvc(MemOperand(sp, StackHandlerConstants::kNextOffset),
      MemOperand(r7), kPointerSize);
  // Set this new handler as the current one.
  StoreP(sp, MemOperand(r7));

  unsigned state =
      StackHandler::IndexField::encode(handler_index) |
      StackHandler::KindField::encode(kind);

  if (kind == StackHandler::JS_ENTRY) {
    // R7: state, R8: Context, R9: FP Offset
    LoadIntLiteral(r7, state);
    LoadImmP(r8, Operand(0, RelocInfo::NONE));  // NULL frame pointer.
    // @TODO Potential Bug here as r10 is roots register.
    LoadSmiLiteral(r9, Smi::FromInt(0));    // Indicates no context.
    StoreMultipleP(r7, r9, MemOperand(sp, StackHandlerConstants::kStateSlot));
  } else {
    // still not sure if fp is right
    StoreP(fp, MemOperand(sp, StackHandlerConstants::kFPOffset));
    StoreP(cp, MemOperand(sp, StackHandlerConstants::kContextOffset));
    LoadIntLiteral(r7, state);
    StoreP(r7, MemOperand(sp, StackHandlerConstants::kStateSlot));
  }
  mov(r7, Operand(CodeObject()));
  StoreP(r7, MemOperand(sp, StackHandlerConstants::kCodeOffset));
}


void MacroAssembler::PopTryHandler() {
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0);
  // Pop the Next Handler into r3 and store it into Handler Address reference.
  LoadP(r3, MemOperand(sp, StackHandlerConstants::kNextOffset));
  mov(ip, Operand(ExternalReference(Isolate::kHandlerAddress, isolate())));
  // Restore previous stack frame.
  lay(sp, MemOperand(sp, StackHandlerConstants::kSize));
  StoreP(r3, MemOperand(ip));
}

// Make use of ip as a temporary register
void MacroAssembler::JumpToHandlerEntry() {
  // Compute the handler entry address and jump to it.  The handler table is
  // a fixed array of (smi-tagged) code offsets.
  // r2 = exception, r3 = code object, r4 = state.
  LoadP(r5,
        FieldMemOperand(r3, Code::kHandlerTableOffset));  // Handler table.
  AddP(r5, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  srl(r4, Operand(StackHandler::kKindWidth));  // Handler index.
  LoadRR(ip, r4);
  sll(ip, Operand(kPointerSizeLog2));
  AddP(ip, r5);
  LoadP(r4, MemOperand(ip));  // Smi-tagged offset.
  AddP(r3, Operand(Code::kHeaderSize - kHeapObjectTag));  // Code start.
  SmiUntag(ip, r4);
  AddP(r0, r3, ip);
  LoadRR(r14, r0);
  Ret();
}


void MacroAssembler::Throw(Register value) {
  // Adjust this code if not the case.
  STATIC_ASSERT(StackHandlerConstants::kSize == 5 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0);
  STATIC_ASSERT(StackHandlerConstants::kCodeOffset == 1 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kStateSlot == 2 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kContextOffset == 3 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kFPOffset == 4 * kPointerSize);
  Label skip;

  // The exception is expected in r2.
  if (!value.is(r2)) {
    LoadRR(r2, value);
  }
  // Drop the stack pointer to the top of the top handler.
  mov(r5, Operand(ExternalReference(Isolate::kHandlerAddress, isolate())));
  LoadP(sp, MemOperand(r5));
  // Restore the next handler.
  pop(r4);
  StoreP(r4, MemOperand(r5));

  // Get the code object (r3) and state (r4).  Restore the context and frame
  // pointer.
  pop(r3);
  pop(r4);
  pop(cp);
  pop(fp);

  // If the handler is a JS frame, restore the context to the frame.
  // (kind == ENTRY) == (fp == 0) == (cp == 0), so we could test either fp
  // or cp.
  CmpP(cp, Operand::Zero());
  beq(&skip);
  StoreP(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  bind(&skip);

  JumpToHandlerEntry();
}


void MacroAssembler::ThrowUncatchable(Register value) {
  // Adjust this code if not the case.
  STATIC_ASSERT(StackHandlerConstants::kSize == 5 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kCodeOffset == 1 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kStateSlot == 2 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kContextOffset == 3 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kFPOffset == 4 * kPointerSize);

  // The exception is expected in r2.
  if (!value.is(r2)) {
    LoadRR(r2, value);
  }
  // Drop the stack pointer to the top of the top stack handler.
  mov(r5, Operand(ExternalReference(Isolate::kHandlerAddress, isolate())));
  LoadP(sp, MemOperand(r5));

  // Unwind the handlers until the ENTRY handler is found.
  Label fetch_next, check_kind;
  b(&check_kind);
  bind(&fetch_next);
  LoadP(sp, MemOperand(sp, StackHandlerConstants::kNextOffset));

  bind(&check_kind);
  STATIC_ASSERT(StackHandler::JS_ENTRY == 0);
  LoadP(r4, MemOperand(sp, StackHandlerConstants::kStateSlot));
  mov(r0, Operand(StackHandler::KindField::kMask));
  AndP(r0, r4);
  bne(&fetch_next /*, cr0*/);

  // Set the top handler address to next handler past the top ENTRY handler.
  pop(r4);
  StoreP(r4, MemOperand(r5));
  // Get the code object (r3) and state (r4).  Clear the context and frame
  // pointer (0 was saved in the handler).
  pop(r3);
  pop(r4);
  pop(cp);
  pop(fp);

  JumpToHandlerEntry();
}


void MacroAssembler::CheckAccessGlobalProxy(Register holder_reg,
                                            Register scratch,
                                            Label* miss) {
  Label same_contexts;

  ASSERT(!holder_reg.is(scratch));
  ASSERT(!holder_reg.is(ip));
  ASSERT(!scratch.is(ip));

  // Load current lexical context from the stack frame.
  LoadP(scratch, MemOperand(fp, StandardFrameConstants::kContextOffset));
  // In debug mode, make sure the lexical context is set.
#ifdef DEBUG
  CmpP(scratch, Operand(0, RelocInfo::NONE));
  Check(ne, "we should not have an empty lexical context");
#endif

  // Load the native context of the current context.
  int offset =
      Context::kHeaderSize + Context::GLOBAL_OBJECT_INDEX * kPointerSize;
  LoadP(scratch, FieldMemOperand(scratch, offset));
  LoadP(scratch, FieldMemOperand(scratch, GlobalObject::kNativeContextOffset));

  // Check the context is a native context.
  if (emit_debug_code()) {
    // TODO(119): avoid push(holder_reg)/pop(holder_reg)
    // Cannot use ip as a temporary in this verification code. Due to the fact
    // that ip is clobbered as part of cmp with an object Operand.
    push(holder_reg);  // Temporarily save holder on the stack.
    // Read the first word and compare to the native_context_map.
    LoadP(holder_reg, FieldMemOperand(scratch, HeapObject::kMapOffset));
    CompareRoot(holder_reg, Heap::kNativeContextMapRootIndex);
    Check(eq, "JSGlobalObject::native_context should be a native context.");
    pop(holder_reg);  // Restore holder.
  }

  // Check if both contexts are the same.
  LoadP(ip, FieldMemOperand(holder_reg, JSGlobalProxy::kNativeContextOffset));
  CmpP(scratch, ip);
  beq(&same_contexts);

  // Check the context is a native context.
  if (emit_debug_code()) {
    // TODO(119): avoid push(holder_reg)/pop(holder_reg)
    // Cannot use ip as a temporary in this verification code. Due to the fact
    // that ip is clobbered as part of cmp with an object Operand.
    push(holder_reg);  // Temporarily save holder on the stack.
    LoadRR(holder_reg, ip);  // Move ip to its holding place.
    CompareRoot(holder_reg, Heap::kNullValueRootIndex);
    Check(ne, "JSGlobalProxy::context() should not be null.");

    LoadP(holder_reg, FieldMemOperand(holder_reg, HeapObject::kMapOffset));
    CompareRoot(holder_reg, Heap::kNativeContextMapRootIndex);
    Check(eq, "JSGlobalObject::native_context should be a native context.");
    // Restore ip is not needed. ip is reloaded below.
    pop(holder_reg);  // Restore holder.
    // Restore ip to holder's context.
    LoadP(ip, FieldMemOperand(holder_reg, JSGlobalProxy::kNativeContextOffset));
  }

  // Check that the security token in the calling global object is
  // compatible with the security token in the receiving global
  // object.
  int token_offset = Context::kHeaderSize +
                     Context::SECURITY_TOKEN_INDEX * kPointerSize;

  LoadP(scratch, FieldMemOperand(scratch, token_offset));
  LoadP(ip, FieldMemOperand(ip, token_offset));
  CmpP(scratch, ip);
  bne(miss);

  bind(&same_contexts);
}


void MacroAssembler::GetNumberHash(Register t0, Register scratch) {
  // First of all we assign the hash seed to scratch.
  LoadRoot(scratch, Heap::kHashSeedRootIndex);
  SmiUntag(scratch);

  // Xor original key with a seed.
  XorP(t0, scratch);

  // Compute the hash code from the untagged key.  This must be kept in sync
  // with ComputeIntegerHash in utils.h.
  //
  // hash = ~hash + (hash << 15);
  LoadRR(scratch, t0);
  NotP(scratch);
  sll(t0, Operand(15));
  AddP(t0, scratch, t0);
  // hash = hash ^ (hash >> 12);
  ShiftRight(scratch, t0, Operand(12));
  XorP(t0, scratch);
  // hash = hash + (hash << 2);
  ShiftLeft(scratch, t0, Operand(2));
  AddP(t0, t0, scratch);
  // hash = hash ^ (hash >> 4);
  ShiftRight(scratch, t0, Operand(4));
  XorP(t0, scratch);
  // hash = hash * 2057;
  LoadRR(r0, t0);
  ShiftLeft(scratch, t0, Operand(3));
  AddP(t0, t0, scratch);
  ShiftLeft(scratch, r0, Operand(11));
  AddP(t0, t0, scratch);
  // hash = hash ^ (hash >> 16);
  ShiftRight(scratch, t0, Operand(16));
  XorP(t0, scratch);
}


void MacroAssembler::LoadFromNumberDictionary(Label* miss,
                                              Register elements,
                                              Register key,
                                              Register result,
                                              Register t0,
                                              Register t1,
                                              Register t2) {
  // Register use:
  //
  // elements - holds the slow-case elements of the receiver on entry.
  //            Unchanged unless 'result' is the same register.
  //
  // key      - holds the smi key on entry.
  //            Unchanged unless 'result' is the same register.
  //
  // result   - holds the result on exit if the load succeeded.
  //            Allowed to be the same as 'key' or 'result'.
  //            Unchanged on bailout so 'key' or 'result' can be used
  //            in further computation.
  //
  // Scratch registers:
  //
  // t0 - holds the untagged key on entry and holds the hash once computed.
  //
  // t1 - used to hold the capacity mask of the dictionary
  //
  // t2 - used for the index into the dictionary.
  Label done;

  GetNumberHash(t0, t1);

  // Compute the capacity mask.
  LoadP(t1, FieldMemOperand(elements, SeededNumberDictionary::kCapacityOffset));
  SmiUntag(t1);
  SubP(t1, Operand(1));

  // Generate an unrolled loop that performs a few probes before giving up.
  static const int kProbes = 4;
  for (int i = 0; i < kProbes; i++) {
    // Use t2 for index calculations and keep the hash intact in t0.
    LoadRR(t2, t0);
    // Compute the masked index: (hash + i + i * i) & mask.
    if (i > 0) {
      AddP(t2, Operand(SeededNumberDictionary::GetProbeOffset(i)));
    }
    AndP(t2, t1);

    // Scale the index by multiplying by the element size.
    ASSERT(SeededNumberDictionary::kEntrySize == 3);
    LoadRR(ip, t2);
    sll(ip, Operand(1));
    AddP(t2, ip);  // t2 = t2 * 3

    // Check if the key is identical to the name.
    sll(t2, Operand(kPointerSizeLog2));
    AddP(t2, elements);
    LoadP(ip,
          FieldMemOperand(t2, SeededNumberDictionary::kElementsStartOffset));
    CmpP(key, ip);
    if (i != kProbes - 1) {
      beq(&done);
    } else {
      bne(miss);
    }
  }

  bind(&done);
  // Check that the value is a normal property.
  // t2: elements + (index * kPointerSize)
  const int kDetailsOffset =
      SeededNumberDictionary::kElementsStartOffset + 2 * kPointerSize;
  LoadP(t1, FieldMemOperand(t2, kDetailsOffset));
  LoadSmiLiteral(ip, Smi::FromInt(PropertyDetails::TypeField::kMask));
  LoadRR(r0, ip);
  AndP(r0, t1/*, SetRC*/);  // Should be okay to remove rc
  bne(miss /*, cr0*/);

  // Get the value at the masked, scaled index and return.
  const int kValueOffset =
      SeededNumberDictionary::kElementsStartOffset + kPointerSize;
  LoadP(result, FieldMemOperand(t2, kValueOffset));
}


void MacroAssembler::AllocateInNewSpace(int object_size,
                                        Register result,
                                        Register scratch1,
                                        Register scratch2,
                                        Label* gc_required,
                                        AllocationFlags flags) {
  if (!FLAG_inline_new) {
    if (emit_debug_code()) {
      // Trash the registers to simulate an allocation failure.
      LoadImmP(result, Operand(0x7091));
      LoadImmP(scratch1, Operand(0x7191));
      LoadImmP(scratch2, Operand(0x7291));
    }
    b(gc_required);
    return;
  }

  ASSERT(!result.is(scratch1));
  ASSERT(!result.is(scratch2));
  ASSERT(!scratch1.is(scratch2));
  ASSERT(!scratch1.is(ip));
  ASSERT(!scratch2.is(ip));

  // Make object size into bytes.
  if ((flags & SIZE_IN_WORDS) != 0) {
    object_size *= kPointerSize;
  }
  ASSERT_EQ(0, static_cast<int>(object_size & kObjectAlignmentMask));

  // Check relative positions of allocation top and limit addresses.
  // The values must be adjacent in memory to allow the use of LDM.
  // Also, assert that the registers are numbered such that the values
  // are loaded in the correct order.
  ExternalReference new_space_allocation_top =
      ExternalReference::new_space_allocation_top_address(isolate());
  ExternalReference new_space_allocation_limit =
      ExternalReference::new_space_allocation_limit_address(isolate());
  intptr_t top   =
      reinterpret_cast<intptr_t>(new_space_allocation_top.address());
  intptr_t limit =
      reinterpret_cast<intptr_t>(new_space_allocation_limit.address());
  ASSERT((limit - top) == kPointerSize);
  ASSERT(result.code() < ip.code());

  // Set up allocation top address and object size registers.
  Register topaddr = scratch1;
  Register obj_size_reg = scratch2;
  mov(topaddr, Operand(new_space_allocation_top));
  // this won't work for very large object on PowerPC
  LoadImmP(obj_size_reg, Operand(object_size));

  // This code stores a temporary value in ip. This is OK, as the code below
  // does not need ip for implicit literal generation.
  if ((flags & RESULT_CONTAINS_TOP) == 0) {
    // Load allocation top into result and allocation limit into ip.
    LoadP(result, MemOperand(topaddr));
    LoadP(ip, MemOperand(topaddr, kPointerSize));
  } else {
    if (emit_debug_code()) {
      // Assert that result actually contains top on entry. ip is used
      // immediately below so this use of ip does not cause difference with
      // respect to register content between debug and release mode.
      LoadP(ip, MemOperand(topaddr));
      CmpP(result, ip);
      Check(eq, "Unexpected allocation top");
    }
    // Load allocation limit into ip. Result already contains allocation top.
    LoadP(ip, MemOperand(topaddr, limit - top), r0);
  }

  // Calculate new top and bail out if new space is exhausted. Use result
  // to calculate the new top.
  ASSERT(obj_size_reg.is(scratch2));
  AddP(scratch2, result);   // Add result + obj_size_reg (scratch2)
  b(Condition(CC_OF), gc_required);   // Detect overflow
  CmpLogicalP(scratch2, ip);
  bgt(gc_required);
  StoreP(scratch2, MemOperand(topaddr));

  // Tag object if requested.
  if ((flags & TAG_OBJECT) != 0) {
    AddP(result, Operand(kHeapObjectTag));
  }
}


void MacroAssembler::AllocateInNewSpace(Register object_size,
                                        Register result,
                                        Register scratch1,
                                        Register scratch2,
                                        Label* gc_required,
                                        AllocationFlags flags) {
  if (!FLAG_inline_new) {
    if (emit_debug_code()) {
      // Trash the registers to simulate an allocation failure.
      LoadImmP(result, Operand(0x7091));
      LoadImmP(scratch1, Operand(0x7191));
      LoadImmP(scratch2, Operand(0x7291));
    }
    b(gc_required);
    return;
  }

  // Assert that the register arguments are different and that none of
  // them are ip. ip is used explicitly in the code generated below.
  ASSERT(!result.is(scratch1));
  ASSERT(!result.is(scratch2));
  ASSERT(!scratch1.is(scratch2));
  ASSERT(!object_size.is(ip));
  ASSERT(!result.is(ip));
  ASSERT(!scratch1.is(ip));
  ASSERT(!scratch2.is(ip));

  // Check relative positions of allocation top and limit addresses.
  // The values must be adjacent in memory to allow the use of LDM.
  // Also, assert that the registers are numbered such that the values
  // are loaded in the correct order.
  ExternalReference new_space_allocation_top =
      ExternalReference::new_space_allocation_top_address(isolate());
  ExternalReference new_space_allocation_limit =
      ExternalReference::new_space_allocation_limit_address(isolate());
  intptr_t top =
      reinterpret_cast<intptr_t>(new_space_allocation_top.address());
  intptr_t limit =
      reinterpret_cast<intptr_t>(new_space_allocation_limit.address());
  ASSERT((limit - top) == kPointerSize);
  ASSERT(result.code() < ip.code());

  // Set up allocation top address.
  Register topaddr = scratch1;
  mov(topaddr, Operand(new_space_allocation_top));

  // This code stores a temporary value in ip. This is OK, as the code below
  // does not need ip for implicit literal generation.
  if ((flags & RESULT_CONTAINS_TOP) == 0) {
    // Load allocation top into result and allocation limit into ip.
    LoadP(result, MemOperand(topaddr));
    LoadP(ip, MemOperand(topaddr, kPointerSize));
  } else {
    if (emit_debug_code()) {
      // Assert that result actually contains top on entry. ip is used
      // immediately below so this use of ip does not cause difference with
      // respect to register content between debug and release mode.
      LoadP(ip, MemOperand(topaddr));
      CmpP(result, ip);
      Check(eq, "Unexpected allocation top");
    }
    // Load allocation limit into ip. Result already contains allocation top.
    LoadP(ip, MemOperand(topaddr, limit - top));
  }

  // Calculate new top and bail out if new space is exhausted. Use result
  // to calculate the new top. Object size may be in words so a shift is
  // required to get the number of bytes.
  if ((flags & SIZE_IN_WORDS) != 0) {
    ShiftLeftP(scratch2, object_size, Operand(kPointerSizeLog2));
    AddP(scratch2, result);
  } else {
    AddP(scratch2, result, object_size);
  }
  b(Condition(CC_OF), gc_required);
  CmpLogicalP(scratch2, ip);
  bgt(gc_required);

  // Update allocation top. result temporarily holds the new top.
  if (emit_debug_code()) {
    mov(r0, Operand(kObjectAlignmentMask));
    AndP(r0, scratch2);
    Check(eq, "Unaligned allocation in new space", cr0);
  }
  StoreP(scratch2, MemOperand(topaddr));

  // Tag object if requested.
  if ((flags & TAG_OBJECT) != 0) {
    AddP(result, Operand(kHeapObjectTag));
  }
}


void MacroAssembler::UndoAllocationInNewSpace(Register object,
                                              Register scratch) {
  ExternalReference new_space_allocation_top =
      ExternalReference::new_space_allocation_top_address(isolate());

  // Make sure the object has no tag before resetting top.
  mov(r0, Operand(~kHeapObjectTagMask));
  AndP(object, r0);
  // was.. and_(object, object, Operand(~kHeapObjectTagMask));
#ifdef DEBUG
  // Check that the object un-allocated is below the current top.
  mov(scratch, Operand(new_space_allocation_top));
  LoadP(scratch, MemOperand(scratch));
  CmpP(object, scratch);
  Check(lt, "Undo allocation of non allocated memory");
#endif
  // Write the address of the object to un-allocate as the current top.
  mov(scratch, Operand(new_space_allocation_top));
  StoreP(object, MemOperand(scratch));
}


void MacroAssembler::AllocateTwoByteString(Register result,
                                           Register length,
                                           Register scratch1,
                                           Register scratch2,
                                           Register scratch3,
                                           Label* gc_required) {
  // Calculate the number of bytes needed for the characters in the string while
  // observing object alignment.
  ASSERT((SeqTwoByteString::kHeaderSize & kObjectAlignmentMask) == 0);
  LoadRR(scratch1, length);
  sll(scratch1, Operand(1));  // Length in bytes, not chars.
  AddP(scratch1,
       Operand(kObjectAlignmentMask + SeqTwoByteString::kHeaderSize));
  mov(r0, Operand(~kObjectAlignmentMask));
  AndP(scratch1, r0);

  // Allocate two-byte string in new space.
  AllocateInNewSpace(scratch1,
                     result,
                     scratch2,
                     scratch3,
                     gc_required,
                     TAG_OBJECT);

  // Set the map, length and hash field.
  InitializeNewString(result,
                      length,
                      Heap::kStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::AllocateAsciiString(Register result,
                                         Register length,
                                         Register scratch1,
                                         Register scratch2,
                                         Register scratch3,
                                         Label* gc_required) {
  // Calculate the number of bytes needed for the characters in the string while
  // observing object alignment.
  ASSERT((SeqAsciiString::kHeaderSize & kObjectAlignmentMask) == 0);
  ASSERT(kCharSize == 1);
  AddP(scratch1, length,
                 Operand(kObjectAlignmentMask + SeqAsciiString::kHeaderSize));
  LoadImmP(r0, Operand(~kObjectAlignmentMask));
  AndP(scratch1, r0);

  // Allocate ASCII string in new space.
  AllocateInNewSpace(scratch1,
                     result,
                     scratch2,
                     scratch3,
                     gc_required,
                     TAG_OBJECT);

  // Set the map, length and hash field.
  InitializeNewString(result,
                      length,
                      Heap::kAsciiStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::AllocateTwoByteConsString(Register result,
                                               Register length,
                                               Register scratch1,
                                               Register scratch2,
                                               Label* gc_required) {
  AllocateInNewSpace(ConsString::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     TAG_OBJECT);

  InitializeNewString(result,
                      length,
                      Heap::kConsStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::AllocateAsciiConsString(Register result,
                                             Register length,
                                             Register scratch1,
                                             Register scratch2,
                                             Label* gc_required) {
  AllocateInNewSpace(ConsString::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     TAG_OBJECT);

  InitializeNewString(result,
                      length,
                      Heap::kConsAsciiStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::AllocateTwoByteSlicedString(Register result,
                                                 Register length,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Label* gc_required) {
  AllocateInNewSpace(SlicedString::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     TAG_OBJECT);

  InitializeNewString(result,
                      length,
                      Heap::kSlicedStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::AllocateAsciiSlicedString(Register result,
                                               Register length,
                                               Register scratch1,
                                               Register scratch2,
                                               Label* gc_required) {
  AllocateInNewSpace(SlicedString::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     TAG_OBJECT);

  InitializeNewString(result,
                      length,
                      Heap::kSlicedAsciiStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::CompareObjectType(Register object,
                                       Register map,
                                       Register type_reg,
                                       InstanceType type) {
  LoadP(map, FieldMemOperand(object, HeapObject::kMapOffset));
  CompareInstanceType(map, type_reg, type);
}


void MacroAssembler::CompareInstanceType(Register map,
                                         Register type_reg,
                                         InstanceType type) {
  LoadlB(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
  CmpP(type_reg, Operand(type));
}


void MacroAssembler::CompareRoot(Register obj,
                                 Heap::RootListIndex index) {
  CmpP(obj, RootMemOperand(index));
}


void MacroAssembler::CheckFastElements(Register map,
                                       Register scratch,
                                       Label* fail) {
  STATIC_ASSERT(FAST_SMI_ELEMENTS == 0);
  STATIC_ASSERT(FAST_HOLEY_SMI_ELEMENTS == 1);
  STATIC_ASSERT(FAST_ELEMENTS == 2);
  STATIC_ASSERT(FAST_HOLEY_ELEMENTS == 3);
  LoadlB(scratch, FieldMemOperand(map, Map::kBitField2Offset));
  STATIC_ASSERT(Map::kMaximumBitField2FastHoleyElementValue < 0x8000);
  CmpLogicalP(scratch, Operand(Map::kMaximumBitField2FastHoleyElementValue));
  bgt(fail);
}


void MacroAssembler::CheckFastObjectElements(Register map,
                                             Register scratch,
                                             Label* fail) {
  STATIC_ASSERT(FAST_SMI_ELEMENTS == 0);
  STATIC_ASSERT(FAST_HOLEY_SMI_ELEMENTS == 1);
  STATIC_ASSERT(FAST_ELEMENTS == 2);
  STATIC_ASSERT(FAST_HOLEY_ELEMENTS == 3);
  LoadlB(scratch, FieldMemOperand(map, Map::kBitField2Offset));
  CmpLogicalP(scratch, Operand(Map::kMaximumBitField2FastHoleySmiElementValue));
  ble(fail);
  CmpLogicalP(scratch, Operand(Map::kMaximumBitField2FastHoleyElementValue));
  bgt(fail);
}


void MacroAssembler::CheckFastSmiElements(Register map,
                                          Register scratch,
                                          Label* fail) {
  STATIC_ASSERT(FAST_SMI_ELEMENTS == 0);
  STATIC_ASSERT(FAST_HOLEY_SMI_ELEMENTS == 1);
  LoadlB(scratch, FieldMemOperand(map, Map::kBitField2Offset));
  CmpLogicalP(scratch, Operand(Map::kMaximumBitField2FastHoleySmiElementValue));
  bgt(fail);
}


void MacroAssembler::StoreNumberToDoubleElements(Register value_reg,
                                                 Register key_reg,
                                                 Register receiver_reg,
                                                 Register elements_reg,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Register scratch3,
                                                 Register scratch4,
                                                 Label* fail) {
  Label smi_value, maybe_nan, have_double_value, is_nan, done;
#if V8_TARGET_ARCH_S390X
  Register double_reg = scratch2;
#else
  Register mantissa_reg = scratch2;
  Register exponent_reg = scratch3;
#endif

  // Handle smi values specially.
  JumpIfSmi(value_reg, &smi_value);

  // Ensure that the object is a heap number
  CheckMap(value_reg,
           scratch1,
           isolate()->factory()->heap_number_map(),
           fail,
           DONT_DO_SMI_CHECK);

  // Check for nan: all NaN values have a value greater (signed) than 0x7ff00000
  // in the exponent.
#if V8_TARGET_ARCH_S390X
  mov(scratch1, Operand(kLastNonNaNInt64));
  AddP(scratch3, value_reg, Operand(-kHeapObjectTag));
  lg(double_reg, MemOperand(scratch3, HeapNumber::kValueOffset));
  CmpP(double_reg, scratch1);
#else
  mov(scratch1, Operand(kNaNOrInfinityLowerBoundUpper32));
  LoadlW(exponent_reg, FieldMemOperand(value_reg, HeapNumber::kExponentOffset));
  CmpP(exponent_reg, scratch1);
#endif
  bge(&maybe_nan);

#if !V8_TARGET_ARCH_S390X
  LoadlW(mantissa_reg, FieldMemOperand(value_reg, HeapNumber::kMantissaOffset));
#endif

  bind(&have_double_value);
  SmiToDoubleArrayOffset(scratch1, key_reg);
  AddP(scratch1, elements_reg);
#if V8_TARGET_ARCH_S390X
  AddP(scratch1, Operand(-kHeapObjectTag));
  stg(double_reg, MemOperand(scratch1, FixedDoubleArray::kHeaderSize));
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
  StoreW(mantissa_reg,
         FieldMemOperand(scratch1, FixedDoubleArray::kHeaderSize));
  uint32_t offset = FixedDoubleArray::kHeaderSize + sizeof(kHoleNanLower32);
  StoreW(exponent_reg, FieldMemOperand(scratch1, offset));
#elif __BYTE_ORDER == __BIG_ENDIAN
  StoreW(exponent_reg,
         FieldMemOperand(scratch1, FixedDoubleArray::kHeaderSize));
  uint32_t offset = FixedDoubleArray::kHeaderSize + sizeof(kHoleNanLower32);
  StoreW(mantissa_reg, FieldMemOperand(scratch1, offset));
#endif
#endif
  b(&done);

  bind(&maybe_nan);
  // Could be NaN or Infinity. If fraction is not zero, it's NaN, otherwise
  // it's an Infinity, and the non-NaN code path applies.
  bgt(&is_nan);
#if V8_TARGET_ARCH_S390X
  LoadRR(r0, double_reg);
  nihf(r0, Operand::Zero());
  CmpP(r0, Operand::Zero());
  beq(&have_double_value);
  // clrldi(r0, double_reg, Operand(32), SetRC);
  // beq(&have_double_value /*, cr0*/);
#else
  LoadlW(mantissa_reg, FieldMemOperand(value_reg, HeapNumber::kMantissaOffset));
  CmpP(mantissa_reg, Operand::Zero());
  beq(&have_double_value);
#endif
  bind(&is_nan);
  // Load canonical NaN for storing into the double array.
  uint64_t nan_int64 = BitCast<uint64_t>(
      FixedDoubleArray::canonical_not_the_hole_nan_as_double());
#if V8_TARGET_ARCH_S390X
  mov(double_reg, Operand(nan_int64));
#else
  mov(mantissa_reg, Operand(static_cast<intptr_t>(nan_int64)));
  mov(exponent_reg, Operand(static_cast<intptr_t>(nan_int64 >> 32)));
#endif
  b(&have_double_value);

  bind(&smi_value);
  AddP(scratch1, elements_reg,
                 Operand(FixedDoubleArray::kHeaderSize - kHeapObjectTag));
  SmiToDoubleArrayOffset(scratch4, key_reg);
  AddP(scratch1, scratch4);
  // scratch1 is now effective address of the double element

  Register untagged_value = elements_reg;
  SmiUntag(untagged_value, value_reg);
  FloatingPointHelper::ConvertIntToDouble(this,
                                          untagged_value,
                                          d0);
  StoreF(d0, MemOperand(scratch1, 0));

  bind(&done);
}


void MacroAssembler::AddAndCheckForOverflow(Register dst,
                                            Register left,
                                            Register right,
                                            Register overflow_dst,
                                            Register scratch) {
  ASSERT(!dst.is(overflow_dst));
  ASSERT(!dst.is(scratch));
  ASSERT(!overflow_dst.is(scratch));
  ASSERT(!overflow_dst.is(left));
  ASSERT(!overflow_dst.is(right));

  // C = A+B; C overflows if A/B have same sign and C has diff sign than A
  if (dst.is(left)) {
    LoadRR(scratch, left);            // Preserve left.
    AddP(dst, left, right);            // Left is overwritten.
    XorP(scratch, dst);               // Original left.
    XorP(overflow_dst, dst, right);
    AndP(overflow_dst, scratch/*, SetRC*/);
    LoadAndTestRR(overflow_dst, overflow_dst);
    // Should be okay to remove rc
  } else if (dst.is(right)) {
    LoadRR(scratch, right);           // Preserve right.
    AddP(dst, left, right);            // Right is overwritten.
    XorP(scratch, dst);               // Original right.
    XorP(overflow_dst, dst, left);
    AndP(overflow_dst, scratch/*, SetRC*/);
    LoadAndTestRR(overflow_dst, overflow_dst);
    // Should be okay to remove rc
  } else {
    AddP(dst, left, right);
    XorP(overflow_dst, dst, left);
    XorP(scratch, dst, right);
    AndP(overflow_dst, scratch/*, SetRC*/);
    LoadAndTestRR(overflow_dst, overflow_dst);
    // Should be okay to remove rc
  }
}

void MacroAssembler::SubAndCheckForOverflow(Register dst,
                                            Register left,
                                            Register right,
                                            Register overflow_dst,
                                            Register scratch) {
  ASSERT(!dst.is(overflow_dst));
  ASSERT(!dst.is(scratch));
  ASSERT(!overflow_dst.is(scratch));
  ASSERT(!overflow_dst.is(left));
  ASSERT(!overflow_dst.is(right));

  // C = A-B; C overflows if A/B have diff signs and C has diff sign than A
  if (dst.is(left)) {
    LoadRR(scratch, left);            // Preserve left.
    SubP(dst, left, right);           // Left is overwritten.
    XorP(overflow_dst, dst, scratch);
    XorP(scratch, right);
    AndP(overflow_dst, scratch/*, SetRC*/);
    LoadAndTestRR(overflow_dst, overflow_dst);
    // Should be okay to remove rc
  } else if (dst.is(right)) {
    LoadRR(scratch, right);           // Preserve right.
    SubP(dst, left, right);           // Right is overwritten.
    XorP(overflow_dst, dst, left);
    XorP(scratch, left);
    AndP(overflow_dst, scratch/*, SetRC*/);
    LoadAndTestRR(overflow_dst, overflow_dst);
    // Should be okay to remove rc
  } else {
    SubP(dst, left, right);
    XorP(overflow_dst, dst, left);
    XorP(scratch, left, right);
    AndP(overflow_dst, scratch/*, SetRC*/);
    LoadAndTestRR(overflow_dst, overflow_dst);
    // Should be okay to remove rc
  }
}


void MacroAssembler::CompareMap(Register obj,
                                Register scratch,
                                Handle<Map> map,
                                Label* early_success,
                                CompareMapMode mode) {
  LoadP(scratch, FieldMemOperand(obj, HeapObject::kMapOffset));
  CompareMap(obj, map, early_success, mode);
}


void MacroAssembler::CompareMap(Register obj,
                                Handle<Map> map,
                                Label* early_success,
                                CompareMapMode mode) {
  mov(r0, Operand(map));
  CmpP(r0, FieldMemOperand(obj, HeapObject::kMapOffset));
  if (mode == ALLOW_ELEMENT_TRANSITION_MAPS) {
    ElementsKind kind = map->elements_kind();
    if (IsFastElementsKind(kind)) {
      bool packed = IsFastPackedElementsKind(kind);
      Map* current_map = *map;
      while (CanTransitionToMoreGeneralFastElementsKind(kind, packed)) {
        kind = GetNextMoreGeneralFastElementsKind(kind, packed);
        current_map = current_map->LookupElementsTransitionMap(kind);
        if (!current_map) break;
        beq(early_success);
        mov(r0, Operand(Handle<Map>(current_map)));
        CmpP(r0, FieldMemOperand(obj, HeapObject::kMapOffset));
      }
    }
  }
}


void MacroAssembler::CheckMap(Register obj,
                              Register scratch,
                              Handle<Map> map,
                              Label* fail,
                              SmiCheckType smi_check_type,
                              CompareMapMode mode) {
  if (smi_check_type == DO_SMI_CHECK) {
    JumpIfSmi(obj, fail);
  }

  Label success;
  CompareMap(obj, scratch, map, &success, mode);
  bne(fail);
  bind(&success);
}


void MacroAssembler::CheckMap(Register obj,
                              Register scratch,
                              Heap::RootListIndex index,
                              Label* fail,
                              SmiCheckType smi_check_type) {
  if (smi_check_type == DO_SMI_CHECK) {
    JumpIfSmi(obj, fail);
  }
  LoadP(scratch, FieldMemOperand(obj, HeapObject::kMapOffset));
  CompareRoot(scratch, index);
  bne(fail);
}


void MacroAssembler::DispatchMap(Register obj,
                                 Register scratch,
                                 Handle<Map> map,
                                 Handle<Code> success,
                                 SmiCheckType smi_check_type) {
  Label fail;
  if (smi_check_type == DO_SMI_CHECK) {
    JumpIfSmi(obj, &fail);
  }
  LoadP(scratch, FieldMemOperand(obj, HeapObject::kMapOffset));
  mov(ip, Operand(map));
  CmpP(scratch, ip);
  bne(&fail);
  Jump(success, RelocInfo::CODE_TARGET, al);
  bind(&fail);
}


void MacroAssembler::TryGetFunctionPrototype(Register function,
                                             Register result,
                                             Register scratch,
                                             Label* miss,
                                             bool miss_on_bound_function) {
  // Check that the receiver isn't a smi.
  JumpIfSmi(function, miss);

  // Check that the function really is a function.  Load map into result reg.
  CompareObjectType(function, result, scratch, JS_FUNCTION_TYPE);
  bne(miss);

  if (miss_on_bound_function) {
    LoadP(scratch,
          FieldMemOperand(function, JSFunction::kSharedFunctionInfoOffset));
    LoadlW(scratch,
           FieldMemOperand(scratch, SharedFunctionInfo::kCompilerHintsOffset));
    TestBit(scratch,
#if V8_TARGET_ARCH_S390X
            SharedFunctionInfo::kBoundFunction,
#else
            SharedFunctionInfo::kBoundFunction + kSmiTagSize,
#endif
            r0);
    bne(miss /*, cr0*/);
  }

  // Make sure that the function has an instance prototype.
  Label non_instance;
  LoadlB(scratch, FieldMemOperand(result, Map::kBitFieldOffset));
  mov(r0, Operand(1 << Map::kHasNonInstancePrototype));
  AndP(r0, scratch);
  bne(&non_instance /*, cr0*/);

  // Get the prototype or initial map from the function.
  LoadP(result,
        FieldMemOperand(function, JSFunction::kPrototypeOrInitialMapOffset));

  // If the prototype or initial map is the hole, don't return it and
  // simply miss the cache instead. This will allow us to allocate a
  // prototype object on-demand in the runtime system.
  CompareRoot(result, Heap::kTheHoleValueRootIndex);
  beq(miss);

  // If the function does not have an initial map, we're done.
  Label done;
  CompareObjectType(result, scratch, scratch, MAP_TYPE);
  bne(&done);

  // Get the prototype from the initial map.
  LoadP(result, FieldMemOperand(result, Map::kPrototypeOffset));
  b(&done);

  // Non-instance prototype: Fetch prototype from constructor field
  // in initial map.
  bind(&non_instance);
  LoadP(result, FieldMemOperand(result, Map::kConstructorOffset));

  // All done.
  bind(&done);
}


void MacroAssembler::CallStub(CodeStub* stub, Condition cond) {
  ASSERT(AllowThisStubCall(stub));  // Stub calls are not allowed in some stubs.
  Call(stub->GetCode(), RelocInfo::CODE_TARGET, TypeFeedbackId::None(), cond);
}


void MacroAssembler::TailCallStub(CodeStub* stub, Condition cond) {
  ASSERT(allow_stub_calls_ || stub->CompilingCallsToThisStubIsGCSafe());
  Jump(stub->GetCode(), RelocInfo::CODE_TARGET, cond);
}


static int AddressOffset(ExternalReference ref0, ExternalReference ref1) {
  return ref0.address() - ref1.address();
}


void MacroAssembler::CallApiFunctionAndReturn(ExternalReference function,
                                              int stack_space) {
  ExternalReference next_address =
      ExternalReference::handle_scope_next_address();
  const int kNextOffset = 0;
  const int kLimitOffset = AddressOffset(
      ExternalReference::handle_scope_limit_address(),
      next_address);
  const int kLevelOffset = AddressOffset(
      ExternalReference::handle_scope_level_address(),
      next_address);

  // Allocate HandleScope in callee-save registers.
  // r9 - next_address
  // r6 - next_address->kNextOffset
  // r7 - next_address->kLimitOffset
  // r8 - next_address->kLevelOffset
  mov(r9, Operand(next_address));
  LoadP(r6, MemOperand(r9, kNextOffset));
  LoadP(r7, MemOperand(r9, kLimitOffset));
  LoadlW(r8, MemOperand(r9, kLevelOffset));
  AddP(r8, Operand(1));
  StoreW(r8, MemOperand(r9, kLevelOffset));

#if !ABI_RETURNS_HANDLES_IN_REGS
  // PPC LINUX ABI
  // The return value is pointer-sized non-scalar value.
  // Space has already been allocated on the stack which will pass as an
  // implicity first argument.
  la(r2, MemOperand(sp, (kStackFrameExtraParamSlot + 1) * kPointerSize));
#endif

  // Native call returns to the DirectCEntry stub which redirects to the
  // return address pushed on stack (could have moved after GC).
  // DirectCEntry stub itself is generated early and never moves.
  DirectCEntryStub stub;
  stub.GenerateCall(this, function);

#if !ABI_RETURNS_HANDLES_IN_REGS
  // Retrieve return value from stack buffer
  LoadP(r2, MemOperand(r2));
#endif

  Label promote_scheduled_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;
  Label skip1, skip2;

  // If result is non-zero, dereference to get the result value
  // otherwise set it to undefined.
  CmpP(r2, Operand::Zero());
  bne(&skip1, Label::kNear);
  LoadRoot(r2, Heap::kUndefinedValueRootIndex);
  b(&skip2, Label::kNear);
  bind(&skip1);
  LoadP(r2, MemOperand(r2));
  bind(&skip2);

  // No more valid handles (the result handle was the last one). Restore
  // previous handle scope.
  StoreP(r6, MemOperand(r9, kNextOffset));
  if (emit_debug_code()) {
    LoadlW(r3, MemOperand(r9, kLevelOffset));
    CmpP(r3, r8);
    Check(eq, "Unexpected level after return from api call");
  }
  SubP(r8, Operand(1));
  StoreW(r8, MemOperand(r9, kLevelOffset));
  LoadP(ip, MemOperand(r9, kLimitOffset));
  CmpP(r7, ip);
  bne(&delete_allocated_handles);

  // Check if the function scheduled an exception.
  bind(&leave_exit_frame);
  LoadRoot(r6, Heap::kTheHoleValueRootIndex);
  mov(ip, Operand(ExternalReference::scheduled_exception_address(isolate())));
  LoadP(r7, MemOperand(ip));
  CmpP(r6, r7);
  bne(&promote_scheduled_exception);

  // LeaveExitFrame expects unwind space to be in a register.
  mov(r6, Operand(stack_space));
  LeaveExitFrame(false, r6);
  Ret();

  bind(&promote_scheduled_exception);
  TailCallExternalReference(
      ExternalReference(Runtime::kPromoteScheduledException, isolate()),
      0,
      1);

  // HandleScope limit has changed. Delete allocated extensions.
  bind(&delete_allocated_handles);
  StoreP(r7, MemOperand(r9, kLimitOffset));
  LoadRR(r6, r2);
  PrepareCallCFunction(1, r7);
  mov(r2, Operand(ExternalReference::isolate_address()));
  CallCFunction(
      ExternalReference::delete_handle_scope_extensions(isolate()), 1);
  LoadRR(r2, r6);
  b(&leave_exit_frame);
}


bool MacroAssembler::AllowThisStubCall(CodeStub* stub) {
  if (!has_frame_ && stub->SometimesSetsUpAFrame()) return false;
  return allow_stub_calls_ || stub->CompilingCallsToThisStubIsGCSafe();
}


void MacroAssembler::IllegalOperation(int num_arguments) {
  if (num_arguments > 0) {
    la(sp, MemOperand(sp, num_arguments * kPointerSize));
  }
  LoadRoot(r0, Heap::kUndefinedValueRootIndex);
}


void MacroAssembler::IndexFromHash(Register hash, Register index) {
  // If the hash field contains an array index pick it out. The assert checks
  // that the constants for the maximum number of digits for an array index
  // cached in the hash field and the number of bits reserved for it does not
  // conflict.
  ASSERT(TenToThe(String::kMaxCachedArrayIndexLength) <
         (1 << String::kArrayIndexValueBits));
  // We want the smi-tagged index in key.  kArrayIndexValueMask has zeros in
  // the low kHashShift bits.
  STATIC_ASSERT(String::kHashShift == 2);
  STATIC_ASSERT(String::kArrayIndexValueBits == 24);
  // index = SmiTag((hash >> 2) & 0x00FFFFFF);
  ExtractBitRange(index, hash, 25, 2);
  SmiTag(index);
}

void MacroAssembler::SmiToDoubleFPRegister(Register smi,
                                            DoubleRegister value,
                                            Register scratch1) {
  SmiUntag(scratch1, smi);
  FloatingPointHelper::ConvertIntToDouble(this, scratch1, value);
}


// Tries to get a signed int32 out of a double precision floating point heap
// number. Rounds towards 0. Branch to 'not_int32' if the double is out of the
// 32bits signed integer range.
void MacroAssembler::ConvertToInt32(Register source,
                                    Register dest,
                                    Register scratch,
                                    Register scratch2,
                                    DoubleRegister double_scratch,
                                    Label *not_int32) {
  // Retrieve double from heap
  LoadF(double_scratch, FieldMemOperand(source, HeapNumber::kValueOffset));
  // convert
  cfdbr(Condition(5), dest, double_scratch);
  // jump if overflows
  b(Condition(0x1), not_int32);
}

void MacroAssembler::EmitVFPTruncate(VFPRoundingMode rounding_mode,
                                     Register result,
                                     DoubleRegister double_input,
                                     Register scratch,
                                     DoubleRegister double_scratch,
                                     CheckForInexactConversion check_inexact) {
  Condition m = Condition(0);
  switch (rounding_mode) {
    case kRoundToZero:
      m = Condition(5);
      break;
    case kRoundToNearest:
      // TODO(AlanLi): 1 or 3??
      UNIMPLEMENTED();
      break;
    case kRoundToPlusInf:
      m = Condition(6);
      break;
    case kRoundToMinusInf:
      m = Condition(7);
      break;
    default:
      UNIMPLEMENTED();
      break;
  }
  Label done;
  cfdbr(m, result, double_input);
  // Jump to done if overflows to preserve CC
  b(Condition(0x1), &done);

  // Save registers values used by TestIfInt32
  if (r1.is(result)) {
    Push(r0, r1);
  } else {
    Push(r0, r1, result);
  }

#if V8_TARGET_ARCH_S390X
  // The result is a 32-bit integer when the high 33 bits of the
  // result are identical.
  TestIfInt32(result, r0, r1);
#else
  // The result is a 32-bit integer when the high 33 bits of the
  // result are identical.
  LoadRR(r0, result);
  srda(r0, Operand(32));
  TestIfInt32(r0, r1, result);
#endif

  // Restore reg values.
  if (r1.is(result)) {
    Pop(r0, r1);
  } else {
    Pop(r0, r1, result);
  }

  if (check_inexact == kCheckForInexactConversion) {
    bne(&done);
    // convert back and compare
    cdfbr(double_scratch, result);
    cdbr(double_scratch, double_input);
  }
  bind(&done);

  // according to POPS Figure 19-18, condition code 3 is set if the integer
  // overflows or underflows.
}


void MacroAssembler::EmitOutOfInt32RangeTruncate(Register result,
                                                 Register input_high,
                                                 Register input_low,
                                                 Register scratch) {
  Label done, high_shift_needed, pos_shift, neg_shift, shift_done;

  LoadImmP(result, Operand::Zero());

  // check for NaN or +/-Infinity
  // by extracting exponent (mask: 0x7ff00000)
  STATIC_ASSERT(HeapNumber::kExponentMask == 0x7ff00000u);
  ExtractBitMask(scratch, input_high, HeapNumber::kExponentMask);
  CmpLogicalP(scratch, Operand(0x7ff));
  beq(&done);

  // Express exponent as delta to (number of mantissa bits + 31).
  AddP(scratch, Operand(-(HeapNumber::kExponentBias
                                  + HeapNumber::kMantissaBits + 31)));

  // If the delta is strictly positive, all bits would be shifted away,
  // which means that we can return 0.
  CmpP(scratch, Operand::Zero());
  bgt(&done);

  const int kShiftBase = HeapNumber::kNonMantissaBitsInTopWord - 1;
  // Calculate shift.
  AddP(scratch, Operand(kShiftBase + HeapNumber::kMantissaBits));

  // Save the sign.
  STATIC_ASSERT(HeapNumber::kSignMask == 0x80000000u);
  Register sign = result;
  result = no_reg;
  lr(sign, input_high);
  nilf(sign, Operand(HeapNumber::kSignMask));

  // Shifts >= 32 bits should result in zero.
  // Result will comprise of only shifted input_low bits
  CmpP(scratch, Operand(32));
  blt(&high_shift_needed);
  LoadImmP(input_high, Operand::Zero());  // Zero out high for or'ing later
  LoadComplementRR(scratch, scratch);       // scratch = 32 - scratch
  AddP(scratch, Operand(32));
  b(&neg_shift);

  // Set the implicit 1 before the mantissa part in input_high.
  bind(&high_shift_needed);
  oilf(input_high, Operand(1 << HeapNumber::kMantissaBitsInTopWord));

  // Shift the mantissa bits to the correct position.
  // We don't need to clear non-mantissa bits as they will be shifted away.
  // If they weren't, it would mean that the answer is in the 32bit range.
  sll(input_high, scratch);
  LoadComplementRR(scratch, scratch);       // scratch = 32 - scratch
  AddP(scratch, Operand(32));
  b(&pos_shift);

  // Replace the shifted bits with bits from the lower mantissa word.

  bind(&neg_shift);
  LoadComplementRR(scratch, scratch);
  sll(input_low, scratch);
  b(&shift_done);

  bind(&pos_shift);
  srl(input_low, scratch);

  bind(&shift_done);
  OrP(input_high, input_low);

  // Restore sign if necessary.
  CmpP(sign, Operand::Zero());
  result = sign;
  sign = no_reg;
  LoadRR(result, input_high);
  beq(&done);
  LoadComplementRR(result, result);

  bind(&done);
}


void MacroAssembler::EmitECMATruncate(Register result,
                                      DoubleRegister double_input,
                                      DoubleRegister double_scratch,
                                      Register scratch,
                                      Register input_high,
                                      Register input_low) {
  ASSERT(!input_high.is(result));
  ASSERT(!input_low.is(result));
  ASSERT(!input_low.is(input_high));
  ASSERT(!scratch.is(result) &&
         !scratch.is(input_high) &&
         !scratch.is(input_low));
  ASSERT(!double_scratch.is(double_input));

  Label done;

  cfdbr(ROUND_TOWARD_0, result, double_input);
  // if condition code 3 is not set, this can be fit into
  // an Int32
  // branch either cc == 0, 1 or 2
  b(Condition(0xe), &done);

  // otherwise, do the manual truncation.

  // Load the FPR bits into a GPR
  lgdr(input_high, double_input);

  // Need to massage the 64-bit reg into high and low regs
  // @TODO Implement EmitOutOfInt32RangeTruncate to use a 64-bit reg!
  lr(input_low, input_high);
  srlg(input_high, input_high, Operand(32));

  EmitOutOfInt32RangeTruncate(result,
                              input_high,
                              input_low,
                              scratch);

  bind(&done);
}


void MacroAssembler::GetLeastBitsFromSmi(Register dst,
                                         Register src,
                                         int num_least_bits) {
  if (CpuFeatures::IsSupported(GENERAL_INSTR_EXT)) {
    // We rotate by kSmiShift amount, and extract the num_least_bits
    risbg(dst, src, Operand(64 - num_least_bits), Operand(63),
                    Operand(64 - kSmiShift), true);
  } else {
    SmiUntag(dst, src);
    AndP(dst, Operand((1 << num_least_bits) - 1));
  }
}


void MacroAssembler::GetLeastBitsFromInt32(Register dst,
                                           Register src,
                                           int num_least_bits) {
  AndP(dst, src, Operand((1 << num_least_bits) - 1));
}


void MacroAssembler::CallRuntime(const Runtime::Function* f,
                                 int num_arguments) {
  // All parameters are on the stack.  r2 has the return value after call.

  // If the expected number of arguments of the runtime function is
  // constant, we check that the actual number of arguments match the
  // expectation.
  if (f->nargs >= 0 && f->nargs != num_arguments) {
    IllegalOperation(num_arguments);
    return;
  }

  // TODO(1236192): Most runtime routines don't need the number of
  // arguments passed in because it is constant. At some point we
  // should remove this need and make the runtime routine entry code
  // smarter.
  mov(r2, Operand(num_arguments));
  mov(r3, Operand(ExternalReference(f, isolate())));
#if V8_TARGET_ARCH_S390X
  CEntryStub stub(f->result_size);
#else
  CEntryStub stub(1);
#endif
  CallStub(&stub);
}


void MacroAssembler::CallRuntime(Runtime::FunctionId fid, int num_arguments) {
  CallRuntime(Runtime::FunctionForId(fid), num_arguments);
}


void MacroAssembler::CallRuntimeSaveDoubles(Runtime::FunctionId id) {
  const Runtime::Function* function = Runtime::FunctionForId(id);
  LoadImmP(r2, Operand(function->nargs));
  mov(r3, Operand(ExternalReference(function, isolate())));
  CEntryStub stub(1, kSaveFPRegs);
  CallStub(&stub);
}


void MacroAssembler::CallExternalReference(const ExternalReference& ext,
                                           int num_arguments) {
  mov(r2, Operand(num_arguments));
  mov(r3, Operand(ext));

  CEntryStub stub(1);
  CallStub(&stub);
}


void MacroAssembler::TailCallExternalReference(const ExternalReference& ext,
                                               int num_arguments,
                                               int result_size) {
  // TODO(1236192): Most runtime routines don't need the number of
  // arguments passed in because it is constant. At some point we
  // should remove this need and make the runtime routine entry code
  // smarter.
  mov(r2, Operand(num_arguments));
  JumpToExternalReference(ext);
}


void MacroAssembler::TailCallRuntime(Runtime::FunctionId fid,
                                     int num_arguments,
                                     int result_size) {
  TailCallExternalReference(ExternalReference(fid, isolate()),
                            num_arguments,
                            result_size);
}


void MacroAssembler::JumpToExternalReference(const ExternalReference& builtin) {
  mov(r3, Operand(builtin));
  CEntryStub stub(1);
  Jump(stub.GetCode(), RelocInfo::CODE_TARGET);
}


void MacroAssembler::InvokeBuiltin(Builtins::JavaScript id,
                                   InvokeFlag flag,
                                   const CallWrapper& call_wrapper) {
  // You can't call a builtin without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  GetBuiltinEntry(r4, id);
  if (flag == CALL_FUNCTION) {
    call_wrapper.BeforeCall(CallSize(r1));
    SetCallKind(r7, CALL_AS_METHOD);
    Call(r4);
    call_wrapper.AfterCall();
  } else {
    ASSERT(flag == JUMP_FUNCTION);
    SetCallKind(r7, CALL_AS_METHOD);
    Jump(r4);
  }
}


void MacroAssembler::GetBuiltinFunction(Register target,
                                        Builtins::JavaScript id) {
  // Load the builtins object into target register.
  LoadP(target,
        MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  LoadP(target, FieldMemOperand(target, GlobalObject::kBuiltinsOffset));
  // Load the JavaScript builtin function from the builtins object.
  LoadP(target,
        FieldMemOperand(target,
                        JSBuiltinsObject::OffsetOfFunctionWithId(id)), r0);
}


void MacroAssembler::GetBuiltinEntry(Register target, Builtins::JavaScript id) {
  ASSERT(!target.is(r3));
  GetBuiltinFunction(r3, id);
  // Load the code entry point from the builtins object.
  LoadP(target, FieldMemOperand(r3, JSFunction::kCodeEntryOffset));
}


void MacroAssembler::SetCounter(StatsCounter* counter, int value,
                                Register scratch1, Register scratch2) {
  if (FLAG_native_code_counters && counter->Enabled()) {
    mov(scratch1, Operand(value));
    mov(scratch2, Operand(ExternalReference(counter)));
    StoreW(scratch1, MemOperand(scratch2));
  }
}


void MacroAssembler::IncrementCounter(StatsCounter* counter, int value,
                                      Register scratch1, Register scratch2) {
  ASSERT(value > 0 && is_int8(value));
  if (FLAG_native_code_counters && counter->Enabled()) {
    mov(scratch1, Operand(ExternalReference(counter)));
    // @TODO(JOHN): can be optimized by asi()
    LoadW(scratch2, MemOperand(scratch1));
    AddP(scratch2, Operand(value));
    StoreW(scratch2, MemOperand(scratch1));
  }
}


void MacroAssembler::DecrementCounter(StatsCounter* counter, int value,
                                      Register scratch1, Register scratch2) {
  ASSERT(value > 0 && is_int8(value));
  if (FLAG_native_code_counters && counter->Enabled()) {
    mov(scratch1, Operand(ExternalReference(counter)));
    // @TODO(JOHN): can be optimized by asi()
    LoadW(scratch2, MemOperand(scratch1));
    AddP(scratch2, Operand(-value));
    StoreW(scratch2, MemOperand(scratch1));
  }
}


void MacroAssembler::Assert(Condition cond, const char* msg, CRegister cr) {
  if (emit_debug_code())
    Check(cond, msg, cr);
}


void MacroAssembler::AssertRegisterIsRoot(Register reg,
                                          Heap::RootListIndex index) {
  if (emit_debug_code()) {
    CompareRoot(reg, index);
    Check(eq, "Register did not match expected root");
  }
}


void MacroAssembler::AssertFastElements(Register elements) {
  if (emit_debug_code()) {
    ASSERT(!elements.is(ip));
    Label ok;
    push(elements);
    LoadP(elements, FieldMemOperand(elements, HeapObject::kMapOffset));
    CompareRoot(elements, Heap::kFixedArrayMapRootIndex);
    beq(&ok, Label::kNear);
    CompareRoot(elements, Heap::kFixedDoubleArrayMapRootIndex);
    beq(&ok, Label::kNear);
    CompareRoot(elements, Heap::kFixedCOWArrayMapRootIndex);
    beq(&ok, Label::kNear);
    Abort("JSObject with fast elements map has slow elements");
    bind(&ok);
    pop(elements);
  }
}


void MacroAssembler::Check(Condition cond, const char* msg, CRegister cr) {
  Label L;
  b(cond, &L);
  Abort(msg);
  // will not return here
  bind(&L);
}


void MacroAssembler::Abort(const char* msg) {
  Label abort_start;
  bind(&abort_start);
  // We want to pass the msg string like a smi to avoid GC
  // problems, however msg is not guaranteed to be aligned
  // properly. Instead, we pass an aligned pointer that is
  // a proper v8 smi, but also pass the alignment difference
  // from the real pointer as a smi.
  intptr_t p1 = reinterpret_cast<intptr_t>(msg);
  intptr_t p0 = (p1 & ~kSmiTagMask) + kSmiTag;
  ASSERT(reinterpret_cast<Object*>(p0)->IsSmi());
#ifdef DEBUG
  if (msg != NULL) {
    RecordComment("Abort message: ");
    RecordComment(msg);
  }
#endif

  mov(r0, Operand(p0));
  push(r0);
  LoadSmiLiteral(r0, Smi::FromInt(p1 - p0));
  push(r0);
  // Disable stub call restrictions to always allow calls to abort.
  if (!has_frame_) {
    // We don't actually want to generate a pile of code for this, so just
    // claim there is a stack frame, without generating one.
    FrameScope scope(this, StackFrame::NONE);
    CallRuntime(Runtime::kAbort, 2);
  } else {
    CallRuntime(Runtime::kAbort, 2);
  }
  // will not return here
}


void MacroAssembler::LoadContext(Register dst, int context_chain_length) {
  if (context_chain_length > 0) {
    // Move up the chain of contexts to the context containing the slot.
    LoadP(dst, MemOperand(cp, Context::SlotOffset(Context::PREVIOUS_INDEX)));
    for (int i = 1; i < context_chain_length; i++) {
      LoadP(dst, MemOperand(dst, Context::SlotOffset(Context::PREVIOUS_INDEX)));
    }
  } else {
    // Slot is in the current function context.  Move it into the
    // destination register in case we store into it (the write barrier
    // cannot be allowed to destroy the context in esi).
    LoadRR(dst, cp);
  }
}


void MacroAssembler::LoadTransitionedArrayMapConditional(
    ElementsKind expected_kind,
    ElementsKind transitioned_kind,
    Register map_in_out,
    Register scratch,
    Label* no_map_match) {
  // Load the global or builtins object from the current context.
  LoadP(scratch,
      MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  LoadP(scratch, FieldMemOperand(scratch, GlobalObject::kNativeContextOffset));

  // Check that the function's map is the same as the expected cached map.
  LoadP(scratch,
      MemOperand(scratch,
                 Context::SlotOffset(Context::JS_ARRAY_MAPS_INDEX)));
  size_t offset = expected_kind * kPointerSize +
      FixedArrayBase::kHeaderSize;
  LoadP(ip, FieldMemOperand(scratch, offset));
  CmpP(map_in_out, ip);
  bne(no_map_match);

  // Use the transitioned cached map.
  offset = transitioned_kind * kPointerSize +
      FixedArrayBase::kHeaderSize;
  LoadP(map_in_out, FieldMemOperand(scratch, offset));
}


void MacroAssembler::LoadInitialArrayMap(
    Register function_in, Register scratch,
    Register map_out, bool can_have_holes) {
  ASSERT(!function_in.is(map_out));
  Label done;
  LoadP(map_out, FieldMemOperand(function_in,
                                 JSFunction::kPrototypeOrInitialMapOffset));
  if (!FLAG_smi_only_arrays) {
    ElementsKind kind = can_have_holes ? FAST_HOLEY_ELEMENTS : FAST_ELEMENTS;
    LoadTransitionedArrayMapConditional(FAST_SMI_ELEMENTS,
                                        kind,
                                        map_out,
                                        scratch,
                                        &done);
  } else if (can_have_holes) {
    LoadTransitionedArrayMapConditional(FAST_SMI_ELEMENTS,
                                        FAST_HOLEY_SMI_ELEMENTS,
                                        map_out,
                                        scratch,
                                        &done);
  }
  bind(&done);
}


void MacroAssembler::LoadGlobalFunction(int index, Register function) {
  // Load the global or builtins object from the current context.
  LoadP(function,
        MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  // Load the native context from the global or builtins object.
  LoadP(function, FieldMemOperand(function,
                                  GlobalObject::kNativeContextOffset));
  // Load the function from the native context.
  LoadP(function, MemOperand(function, Context::SlotOffset(index)), r0);
}


void MacroAssembler::LoadGlobalFunctionInitialMap(Register function,
                                                  Register map,
                                                  Register scratch) {
  // Load the initial map. The global functions all have initial maps.
  LoadP(map,
        FieldMemOperand(function, JSFunction::kPrototypeOrInitialMapOffset));
  if (emit_debug_code()) {
    Label ok, fail;
    CheckMap(map, scratch, Heap::kMetaMapRootIndex, &fail, DO_SMI_CHECK);
    b(&ok);
    bind(&fail);
    Abort("Global functions must have initial map");
    bind(&ok);
  }
}


void MacroAssembler::JumpIfNotPowerOfTwoOrZero(
    Register reg,
    Register scratch,
    Label* not_power_of_two_or_zero) {
  SubP(scratch, reg, Operand(1));
  CmpP(scratch, Operand::Zero());
  blt(not_power_of_two_or_zero);
  AndP(r0, reg, scratch/*, SetRC*/);  // Should be okay to remove rc
  bne(not_power_of_two_or_zero /*, cr0*/);
}


void MacroAssembler::JumpIfNotPowerOfTwoOrZeroAndNeg(
    Register reg,
    Register scratch,
    Label* zero_and_neg,
    Label* not_power_of_two) {
  SubP(scratch, reg, Operand(1));
  CmpP(scratch, Operand::Zero());
  blt(zero_and_neg);
  AndP(r0, reg, scratch/*, SetRC*/);  // Should be okay to remove rc
  bne(not_power_of_two /*, cr0*/);
}

void MacroAssembler::JumpIfNotBothSmi(Register reg1,
                                      Register reg2,
                                      Label* on_not_both_smi) {
  STATIC_ASSERT(kSmiTag == 0);
  ASSERT_EQ(1, static_cast<int>(kSmiTagMask));
  LoadRR(r0, reg2);
  OrP(r0, reg1/*, LeaveRC*/);  // should be okay to remove LeaveRC
  JumpIfNotSmi(r0, on_not_both_smi);
}


void MacroAssembler::UntagAndJumpIfSmi(
    Register dst, Register src, Label* smi_case) {
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagSize == 1);
  // this won't work if src == dst
  ASSERT(src.code() != dst.code());
  SmiUntag(dst, src);
  TestIfSmi(src);
  beq(smi_case);
}


void MacroAssembler::UntagAndJumpIfNotSmi(
    Register dst, Register src, Label* non_smi_case) {
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagSize == 1);

  // We can more optimally use TestIfSmi if dst != src
  // otherwise, the UnTag operation will kill the CC and we cannot
  // test the Tag bit.
  if (src.code() != dst.code()) {
    SmiUntag(dst, src);
    TestIfSmi(src);
  } else {
    TestBit(src, 0, r0);
    SmiUntag(dst, src);
    LoadAndTestRR(r0, r0);
  }
  bne(non_smi_case);
}


void MacroAssembler::JumpIfEitherSmi(Register reg1,
                                     Register reg2,
                                     Label* on_either_smi) {
  STATIC_ASSERT(kSmiTag == 0);
  JumpIfSmi(reg1, on_either_smi);
  JumpIfSmi(reg2, on_either_smi);
}


void MacroAssembler::AssertNotSmi(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    mov(r0, Operand(kSmiTagMask));
    AndP(r0, object);
    Check(ne, "Operand is a smi", cr0);
  }
}


void MacroAssembler::AssertSmi(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    mov(r0, Operand(kSmiTagMask));
    AndP(r0, object);
    Check(eq, "Operand is not smi", cr0);
  }
}


void MacroAssembler::AssertString(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    mov(r0, Operand(kSmiTagMask));
    AndP(r0, object);
    Check(ne, "Operand is not a string", cr0);
    push(object);
    LoadP(object, FieldMemOperand(object, HeapObject::kMapOffset));
    CompareInstanceType(object, object, FIRST_NONSTRING_TYPE);
    pop(object);
    Check(lt, "Operand is not a string");
  }
}



void MacroAssembler::AssertRootValue(Register src,
                                     Heap::RootListIndex root_value_index,
                                     const char* message) {
  if (emit_debug_code()) {
    CompareRoot(src, root_value_index);
    Check(eq, message);
  }
}


void MacroAssembler::JumpIfNotHeapNumber(Register object,
                                         Register heap_number_map,
                                         Register scratch,
                                         Label* on_not_heap_number) {
  LoadP(scratch, FieldMemOperand(object, HeapObject::kMapOffset));
  AssertRegisterIsRoot(heap_number_map, Heap::kHeapNumberMapRootIndex);
  CmpP(scratch, heap_number_map);
  bne(on_not_heap_number);
}


void MacroAssembler::JumpIfNonSmisNotBothSequentialAsciiStrings(
    Register first,
    Register second,
    Register scratch1,
    Register scratch2,
    Label* failure) {
  // Test that both first and second are sequential ASCII strings.
  // Assume that they are non-smis.
  LoadP(scratch1, FieldMemOperand(first, HeapObject::kMapOffset));
  LoadP(scratch2, FieldMemOperand(second, HeapObject::kMapOffset));
  LoadlB(scratch1, FieldMemOperand(scratch1, Map::kInstanceTypeOffset));
  LoadlB(scratch2, FieldMemOperand(scratch2, Map::kInstanceTypeOffset));

  JumpIfBothInstanceTypesAreNotSequentialAscii(scratch1,
                                               scratch2,
                                               scratch1,
                                               scratch2,
                                               failure);
}

void MacroAssembler::JumpIfNotBothSequentialAsciiStrings(Register first,
                                                         Register second,
                                                         Register scratch1,
                                                         Register scratch2,
                                                         Label* failure) {
  // Check that neither is a smi.
  STATIC_ASSERT(kSmiTag == 0);
  AndP(scratch1, first, second);
  JumpIfSmi(scratch1, failure);
  JumpIfNonSmisNotBothSequentialAsciiStrings(first,
                                             second,
                                             scratch1,
                                             scratch2,
                                             failure);
}


// Allocates a heap number or jumps to the need_gc label if the young space
// is full and a scavenge is needed.
void MacroAssembler::AllocateHeapNumber(Register result,
                                        Register scratch1,
                                        Register scratch2,
                                        Register heap_number_map,
                                        Label* gc_required,
                                        TaggingMode tagging_mode) {
  // Allocate an object in the heap for the heap number and tag it as a heap
  // object.
  AllocateInNewSpace(HeapNumber::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     tagging_mode == TAG_RESULT ? TAG_OBJECT :
                                                  NO_ALLOCATION_FLAGS);

  // Store heap number map in the allocated object.
  AssertRegisterIsRoot(heap_number_map, Heap::kHeapNumberMapRootIndex);
  if (tagging_mode == TAG_RESULT) {
    StoreP(heap_number_map, FieldMemOperand(result, HeapObject::kMapOffset));
  } else {
    StoreP(heap_number_map, MemOperand(result, HeapObject::kMapOffset));
  }
}


void MacroAssembler::AllocateHeapNumberWithValue(Register result,
                                                 DoubleRegister value,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Register heap_number_map,
                                                 Label* gc_required) {
  AllocateHeapNumber(result, scratch1, scratch2, heap_number_map, gc_required);
  StoreF(value, FieldMemOperand(result, HeapNumber::kValueOffset));
}


// Copies a fixed number of fields of heap objects from src to dst.
void MacroAssembler::CopyFields(Register dst,
                                Register src,
                                RegList temps,
                                int field_count) {
  // At least one bit set in the first 15 registers.
  ASSERT((temps & ((1 << 15) - 1)) != 0);
  ASSERT((temps & dst.bit()) == 0);
  ASSERT((temps & src.bit()) == 0);
  // Primitive implementation using only one temporary register.

  Register tmp = no_reg;
  // Find a temp register in temps list.
  for (int i = 0; i < 15; i++) {
    if ((temps & (1 << i)) != 0) {
      tmp.set_code(i);
      break;
    }
  }
  ASSERT(!tmp.is(no_reg));

  for (int i = 0; i < field_count; i++) {
    LoadP(tmp, FieldMemOperand(src, i * kPointerSize), r0);
    StoreP(tmp, FieldMemOperand(dst, i * kPointerSize));
  }
}


void MacroAssembler::CopyBytes(Register src,
                               Register dst,
                               Register length,
                               Register scratch) {
  Label big_loop, left_bytes, done, fake_call;

  ASSERT(!scratch.is(r0));

  // big loop moves 256 bytes at a time
  bind(&big_loop);
  CmpP(length, Operand(static_cast<intptr_t>(0x100)));
  blt(&left_bytes);

  mvc(MemOperand(dst), MemOperand(src), 0x100);

  AddP(src, Operand(static_cast<intptr_t>(0x100)));
  AddP(dst, Operand(static_cast<intptr_t>(0x100)));
  SubP(length, Operand(static_cast<intptr_t>(0x100)));
  b(&big_loop);

  bind(&left_bytes);
  CmpP(length, Operand::Zero());
  beq(&done);

  // TODO(JOHN): The full optimized version with unknown problem.
  /*
  b(scratch, &fake_call);  // use brasl to Save mvc addr to scratch
  mvc(MemOperand(dst), MemOperand(src), 1);
  bind(&fake_call);
  SubP(length, Operand(static_cast<intptr_t>(-1)));
  ex(length, MemOperand(scratch));  // execute mvc instr above
  AddP(src, length);
  AddP(dst, length);
  AddP(src, Operand(static_cast<intptr_t>(0x1)));
  AddP(dst, Operand(static_cast<intptr_t>(0x1)));
  */

  mvc(MemOperand(dst), MemOperand(src), 1);
  AddP(src, Operand(static_cast<intptr_t>(0x1)));
  AddP(dst, Operand(static_cast<intptr_t>(0x1)));
  SubP(length, Operand(static_cast<intptr_t>(0x1)));

  b(&left_bytes);
  bind(&done);
}

void MacroAssembler::InitializeFieldsWithFiller(Register start_offset,
                                                Register end_offset,
                                                Register filler) {
  Label loop, entry;
  b(&entry);
  bind(&loop);
  StoreP(filler, MemOperand(start_offset));
  AddP(start_offset, Operand(kPointerSize));
  bind(&entry);
  CmpP(start_offset, end_offset);
  blt(&loop);
}


void MacroAssembler::JumpIfBothInstanceTypesAreNotSequentialAscii(
    Register first,
    Register second,
    Register scratch1,
    Register scratch2,
    Label* failure) {
  int kFlatAsciiStringMask =
      kIsNotStringMask | kStringEncodingMask | kStringRepresentationMask;
  int kFlatAsciiStringTag = ASCII_STRING_TYPE;
  if (!scratch1.is(first)) LoadRR(scratch1, first);
  if (!scratch2.is(second)) LoadRR(scratch2, second);
  nilf(scratch1, Operand(kFlatAsciiStringMask));
  CmpP(scratch1, Operand(kFlatAsciiStringTag));
  bne(failure);
  nilf(scratch2, Operand(kFlatAsciiStringMask));
  CmpP(scratch2, Operand(kFlatAsciiStringTag));
  bne(failure);

  /*
  CmpP(scratch1, Operand(kFlatAsciiStringTag));
  nilf(scratch2, Operand(kFlatAsciiStringMask));
  mov(scratch1, Operand(kFlatAsciiStringMask));
  AndP(scratch1, first);
  mov(scratch2, Operand(kFlatAsciiStringMask));
  AndP(scratch2, second);
  CmpP(scratch1, Operand(kFlatAsciiStringTag));
  bne(failure);
  CmpP(scratch2, Operand(kFlatAsciiStringTag));
  bne(failure);
  */
}


void MacroAssembler::JumpIfInstanceTypeIsNotSequentialAscii(Register type,
                                                            Register scratch,
                                                            Label* failure) {
  int kFlatAsciiStringMask =
      kIsNotStringMask | kStringEncodingMask | kStringRepresentationMask;
  int kFlatAsciiStringTag = ASCII_STRING_TYPE;

  if (!scratch.is(type)) LoadRR(scratch, type);
  nilf(scratch, Operand(kFlatAsciiStringMask));
  CmpP(scratch, Operand(kFlatAsciiStringTag));
  bne(failure);
  /*
  mov(scratch, Operand(kFlatAsciiStringMask));
  AndP(scratch, type);
  CmpP(scratch, Operand(kFlatAsciiStringTag));
  bne(failure);
  */
}

static const int kRegisterPassedArguments = 5;


int MacroAssembler::CalculateStackPassedWords(int num_reg_arguments,
                                              int num_double_arguments) {
  int stack_passed_words = 0;
  if (num_double_arguments > DoubleRegister::kNumRegisters) {
      stack_passed_words +=
          2 * (num_double_arguments - DoubleRegister::kNumRegisters);
  }
  // Up to five simple arguments are passed in registers r2..r6
  if (num_reg_arguments > kRegisterPassedArguments) {
    stack_passed_words += num_reg_arguments - kRegisterPassedArguments;
  }
  return stack_passed_words;
}


void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          int num_double_arguments,
                                          Register scratch) {
  int frame_alignment = ActivationFrameAlignment();
  int stack_passed_arguments = CalculateStackPassedWords(
      num_reg_arguments, num_double_arguments);
  if (frame_alignment > kPointerSize) {
    // Make stack end at alignment and make room for stack arguments,
    // the original value of sp and, on native, the required slots to
    // make ABI work.
    LoadRR(scratch, sp);
    lay(sp, MemOperand(sp, -(1 + stack_passed_arguments +
                          kNumRequiredStackFrameSlots) * kPointerSize));
    ASSERT(IsPowerOf2(frame_alignment));
    nill(sp, Operand(-frame_alignment));

    // Save the original stack pointer (pre-alignment) onto the stack
    StoreP(scratch, MemOperand(sp, (stack_passed_arguments +
                        kNumRequiredStackFrameSlots) * kPointerSize));
  } else {
    lay(sp, MemOperand(sp, -(stack_passed_arguments +
                          kNumRequiredStackFrameSlots) * kPointerSize));
  }
}


void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          Register scratch) {
  PrepareCallCFunction(num_reg_arguments, 0, scratch);
}


void MacroAssembler::SetCallCDoubleArguments(DoubleRegister dreg) {
  Move(d0, dreg);
}


void MacroAssembler::SetCallCDoubleArguments(DoubleRegister dreg1,
                                             DoubleRegister dreg2) {
  if (dreg2.is(d0)) {
    ASSERT(!dreg1.is(d2));
    Move(d2, dreg2);
    Move(d0, dreg1);
  } else {
    Move(d0, dreg1);
    Move(d2, dreg2);
  }
}


void MacroAssembler::SetCallCDoubleArguments(DoubleRegister dreg,
                                             Register reg) {
  Move(d0, dreg);
  Move(r2, reg);
}


void MacroAssembler::CallCFunction(ExternalReference function,
                                   int num_reg_arguments,
                                   int num_double_arguments) {
  mov(ip, Operand(function));
  CallCFunctionHelper(ip, num_reg_arguments, num_double_arguments);
}


void MacroAssembler::CallCFunction(Register function,
                                   int num_reg_arguments,
                                   int num_double_arguments) {
  CallCFunctionHelper(function, num_reg_arguments, num_double_arguments);
}


void MacroAssembler::CallCFunction(ExternalReference function,
                                   int num_arguments) {
  CallCFunction(function, num_arguments, 0);
}


void MacroAssembler::CallCFunction(Register function,
                                   int num_arguments) {
  CallCFunction(function, num_arguments, 0);
}


void MacroAssembler::CallCFunctionHelper(Register function,
                                         int num_reg_arguments,
                                         int num_double_arguments) {
  ASSERT(has_frame());
  // Make sure that the stack is aligned before calling a C function unless
  // running in the simulator. The simulator has its own alignment check which
  // provides more information.

  // Just call directly. The function called cannot cause a GC, or
  // allow preemption, so the return address in the link register
  // stays correct.
#if ABI_USES_FUNCTION_DESCRIPTORS && !defined(USE_SIMULATOR)
  // AIX uses a function descriptor. When calling C code be aware
  // of this descriptor and pick up values from it
  LoadP(ToRegister(2), MemOperand(function, kPointerSize));
  LoadP(function, MemOperand(function, 0));
#endif

  // zLinux ABI requires caller's frame to have sufficient space for callee
  // preserved regsiter save area.
  // @TODO Make sure this is in the right place and we need to guard it
  // with appropriate #ifdefs
  // lay(sp, MemOperand(sp, -kCalleeRegisterSaveAreaSize));
  Call(function);
  // la(sp, MemOperand(sp, +kCalleeRegisterSaveAreaSize));

  int stack_passed_arguments = CalculateStackPassedWords(
      num_reg_arguments, num_double_arguments);
  if (ActivationFrameAlignment() > kPointerSize) {
    // Load the original stack pointer (pre-alignment) from the stack
    LoadP(sp, MemOperand(sp, (stack_passed_arguments +
                    kNumRequiredStackFrameSlots) * kPointerSize), r0);
  } else {
    la(sp, MemOperand(sp, (stack_passed_arguments +
                          kNumRequiredStackFrameSlots) * kPointerSize));
  }
}


void MacroAssembler::FlushICache(Register address, size_t size,
                                 Register scratch) {
  // S390 memory model does not require us to flush icache
  return;
}

// This code assumes a FIXED_SEQUENCE for iilf on 31-bit
// and iihf/iilf on 64-bit
void MacroAssembler::PatchRelocatedValue(Register patch_location,
                                         Register scratch,
                                         Register new_value) {
  int32_t offset = 0;

#if V8_TARGET_ARCH_S390X
  // On 64-bit, we expect a IIHF instruction here.
  if (emit_debug_code()) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    // Instructions are stored in Big Endian format
    lrvh(scratch, MemOperand(patch_location));
#else
    llh(scratch, MemOperand(patch_location));
#endif
    nilf(scratch, Operand(0xFF0F));
    // IIHF Opcode with extra zero in 3rd nibble
    cfi(scratch, Operand(0xC008));
    Check(eq, "The instruction to patch should be a iihf.");
  }

  srlg(scratch, new_value, Operand(32));
  // insert new high word into iihf instruction
#if __BYTE_ORDER == __LITTLE_ENDIAN
  // Instructions are stored in Big Endian format
  strv(scratch, MemOperand(patch_location, 2));
#else
  st(scratch, MemOperand(patch_location, 2));
#endif
  offset += 6;
#endif  // V8_TARGET_ARCH_S390X


  // At this point scratch is a iilf instruction.
  if (emit_debug_code()) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    // Instructions are stored in Big Endian format
    lrvh(scratch, MemOperand(patch_location, offset));
#else
    llh(scratch, MemOperand(patch_location, offset));
#endif
    nilf(scratch, Operand(0xFF0F));
    // IILF Opcode with extra zero in 3rd nibble
    cfi(scratch, Operand(0xC009));
    Check(eq, "The instruction to patch should be a iilf.");
  }

  // insert low word into iilf instruction
#if __BYTE_ORDER == __LITTLE_ENDIAN
  // Instructions are stored in Big Endian format
  strv(new_value, MemOperand(patch_location, 2 + offset));
#else
  st(new_value, MemOperand(patch_location, 2 + offset));
#endif

  // Update the I-cache so the new lis and addic can be executed.
#if V8_TARGET_ARCH_S390X
  FlushICache(patch_location, 12, scratch);
#else
  FlushICache(patch_location, 6, scratch);
#endif
}

// This code assumes a FIXED_SEQUENCE for iilf on 31-bit
// and iihf/iilf on 64-bit
void MacroAssembler::GetRelocatedValueLocation(Register patch_location,
                                               Register result,
                                               Register scratch) {
  int32_t offset = 0;

#if V8_TARGET_ARCH_S390X
  // On 64-bit, we expect a IIHF instruction here.
  if (emit_debug_code()) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    // Instructions are stored in Big Endian format
    lrvh(scratch, MemOperand(patch_location));
#else
    llh(scratch, MemOperand(patch_location));
#endif
    nilf(scratch, Operand(0xFF0F));
    // IIHF Opcode with extra zero in 3rd nibble
    cfi(scratch, Operand(0xC008));
    Check(eq, "The instruction to patch should be a iihf.");
  }

  // load high word from iihf instruction
#if __BYTE_ORDER == __LITTLE_ENDIAN
  // Instructions are stored in Big Endian format
  lrv(result, MemOperand(patch_location, 2));
#else
  l(result, MemOperand(patch_location, 2));
#endif
  sllg(result, result, Operand(32));

  offset += 6;
#endif  // V8_TARGET_ARCH_S390X


  // At this point scratch is a iilf instruction.
  if (emit_debug_code()) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    // Instructions are stored in Big Endian format
    lrvh(scratch, MemOperand(patch_location, offset));
#else
    llh(scratch, MemOperand(patch_location, offset));
#endif
    nilf(scratch, Operand(0xFF0F));
    // IILF Opcode with extra zero in 3rd nibble
    cfi(scratch, Operand(0xC009));
    Check(eq, "The instruction to patch should be a iilf.");
  }

  // load low word from iilf instruction
#if __BYTE_ORDER == __LITTLE_ENDIAN
  // Instructions are stored in Big Endian format
  lrv(result, MemOperand(patch_location, 2 + offset));
#else
  l(result, MemOperand(patch_location, 2 + offset));
#endif
}


void MacroAssembler::CheckPageFlag(
    Register object,
    Register scratch,  // scratch may be same register as object
    int mask,
    Condition cc,
    Label* condition_met) {
  ASSERT(cc == ne || cc == eq);
  ClearRightImm(scratch, object, Operand(kPageSizeBits));

  if (IsPowerOf2(mask)) {
    // If it's a power of two, we can use Test-Under-Mask Memory-Imm form
    // which allows testing of a single byte in memory.
    int32_t byte_offset = 4;
    uint32_t shifted_mask = mask;
    // Determine the byte offset to be tested
    if (mask <= 0x80) {
      byte_offset = kPointerSize - 1;
    } else if (mask < 0x8000) {
      byte_offset = kPointerSize - 2;
      shifted_mask = mask >> 8;
    } else if (mask < 0x800000) {
      byte_offset = kPointerSize - 3;
      shifted_mask = mask >> 16;
    } else {
      byte_offset = kPointerSize - 4;
      shifted_mask = mask >> 24;
    }
#if __BYTE_ORDER == __LITTLE_ENDIAN
    // Reverse the byte_offset if emulating on little endian platform
    byte_offset = kPointerSize - byte_offset - 1;
#endif
    tm(MemOperand(scratch, MemoryChunk::kFlagsOffset + byte_offset),
       Operand(shifted_mask));
  } else {
    LoadP(scratch, MemOperand(scratch, MemoryChunk::kFlagsOffset));
    AndP(r0, scratch, Operand(mask));
  }
  // Should be okay to remove rc

  if (cc == ne) {
    bne(condition_met, Label::kNear);
  }
  if (cc == eq) {
    beq(condition_met, Label::kNear);
  }
}


void MacroAssembler::JumpIfBlack(Register object,
                                 Register scratch0,
                                 Register scratch1,
                                 Label* on_black) {
  HasColor(object, scratch0, scratch1, on_black, 1, 0);  // kBlackBitPattern.
  ASSERT(strcmp(Marking::kBlackBitPattern, "10") == 0);
}


void MacroAssembler::HasColor(Register object,
                              Register bitmap_scratch,
                              Register mask_scratch,
                              Label* has_color,
                              int first_bit,
                              int second_bit) {
  ASSERT(!AreAliased(object, bitmap_scratch, mask_scratch, no_reg));

  GetMarkBits(object, bitmap_scratch, mask_scratch);

  Label other_color, word_boundary;
  LoadlW(ip, MemOperand(bitmap_scratch, MemoryChunk::kHeaderSize));
  // Test the first bit
  AndP(r0, ip, mask_scratch/*, SetRC*/);  // Should be okay to remove rc
  b(first_bit == 1 ? eq : ne, &other_color, Label::kNear);
  // Shift left 1
  // May need to load the next cell
  sll(mask_scratch, Operand(1)/*, SetRC*/);
  beq(&word_boundary /*, cr0*/);
  // Test the second bit
  AndP(r0, ip, mask_scratch/*, SetRC*/);  // Should be okay to remove rc
  b(second_bit == 1 ? ne : eq, has_color);
  b(&other_color, Label::kNear);

  bind(&word_boundary);
  LoadlW(ip, MemOperand(bitmap_scratch,
                     MemoryChunk::kHeaderSize + kIntSize));
  AndP(r0, ip, Operand(1));
  b(second_bit == 1 ? ne : eq, has_color);
  bind(&other_color);
}


// Detect some, but not all, common pointer-free objects.  This is used by the
// incremental write barrier which doesn't care about oddballs (they are always
// marked black immediately so this code is not hit).
void MacroAssembler::JumpIfDataObject(Register value,
                                      Register scratch,
                                      Label* not_data_object) {
  Label is_data_object;
  LoadP(scratch, FieldMemOperand(value, HeapObject::kMapOffset));
  CompareRoot(scratch, Heap::kHeapNumberMapRootIndex);
  beq(&is_data_object);
  ASSERT(kIsIndirectStringTag == 1 && kIsIndirectStringMask == 1);
  ASSERT(kNotStringTag == 0x80 && kIsNotStringMask == 0x80);
  // If it's a string and it's not a cons string then it's an object containing
  // no GC pointers.
  LoadlB(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  STATIC_ASSERT((kIsIndirectStringMask | kIsNotStringMask) == 0x81);
  nilf(scratch, Operand(kIsIndirectStringMask | kIsNotStringMask));
  bne(not_data_object /*, cr0*/);
  bind(&is_data_object);
}


void MacroAssembler::GetMarkBits(Register addr_reg,
                                 Register bitmap_reg,
                                 Register mask_reg) {
  ASSERT(!AreAliased(addr_reg, bitmap_reg, mask_reg, no_reg));
  LoadRR(bitmap_reg, addr_reg);
  nilf(bitmap_reg, Operand(~Page::kPageAlignmentMask));
  const int kLowBits = kPointerSizeLog2 + Bitmap::kBitsPerCellLog2;
  ExtractBitRange(mask_reg, addr_reg,
                  kLowBits - 1,
                  kPointerSizeLog2);
  ExtractBitRange(ip, addr_reg,
                  kPageSizeBits - 1,
                  kLowBits);
  ShiftLeftP(ip, ip, Operand(Bitmap::kBytesPerCellLog2));
  AddP(bitmap_reg, ip);
  LoadRR(ip, mask_reg);   // Have to do some funky reg shuffling as
                          // 31-bit shift left clobbers on s390.
  LoadImmP(mask_reg, Operand(1));
  ShiftLeftP(mask_reg, mask_reg, ip);
}


void MacroAssembler::EnsureNotWhite(
    Register value,
    Register bitmap_scratch,
    Register mask_scratch,
    Register load_scratch,
    Label* value_is_white_and_not_data) {
  ASSERT(!AreAliased(value, bitmap_scratch, mask_scratch, ip));
  GetMarkBits(value, bitmap_scratch, mask_scratch);

  // If the value is black or grey we don't need to do anything.
  ASSERT(strcmp(Marking::kWhiteBitPattern, "00") == 0);
  ASSERT(strcmp(Marking::kBlackBitPattern, "10") == 0);
  ASSERT(strcmp(Marking::kGreyBitPattern, "11") == 0);
  ASSERT(strcmp(Marking::kImpossibleBitPattern, "01") == 0);

  Label done;

  // Since both black and grey have a 1 in the first position and white does
  // not have a 1 there we only need to check one bit.
  LoadlW(load_scratch, MemOperand(bitmap_scratch, MemoryChunk::kHeaderSize));
  LoadRR(r0, load_scratch);
  AndP(r0, mask_scratch/*, SetRC*/);
  // Should be okay to remove rc
  bne(&done /*, cr0*/);

  if (emit_debug_code()) {
    // Check for impossible bit pattern.
    Label ok;
    // LSL may overflow, making the check conservative.
    LoadRR(r0, mask_scratch);
    sll(r0, Operand(1));
    AndP(r0, load_scratch/*, SetRC*/);  // Should be okay to remove rc
    beq(&ok /*, cr0*/);
    stop("Impossible marking bit pattern");
    bind(&ok);
  }

  // Value is white.  We check whether it is data that doesn't need scanning.
  // Currently only checks for HeapNumber and non-cons strings.
  Register map = load_scratch;  // Holds map while checking type.
  Register length = load_scratch;  // Holds length of object after testing type.
  Label is_data_object, maybe_string_object, is_string_object, is_encoded;
#if V8_TARGET_ARCH_S390X
  Label length_computed;
#endif


  // Check for heap-number
  LoadP(map, FieldMemOperand(value, HeapObject::kMapOffset));
  CompareRoot(map, Heap::kHeapNumberMapRootIndex);
  bne(&maybe_string_object);
  LoadImmP(length, Operand(HeapNumber::kSize));
  b(&is_data_object);
  bind(&maybe_string_object);

  // Check for strings.
  ASSERT(kIsIndirectStringTag == 1 && kIsIndirectStringMask == 1);
  ASSERT(kNotStringTag == 0x80 && kIsNotStringMask == 0x80);
  // If it's a string and it's not a cons string then it's an object containing
  // no GC pointers.
  Register instance_type = load_scratch;
  LoadlB(instance_type, FieldMemOperand(map, Map::kInstanceTypeOffset));
  mov(r0, Operand(kIsIndirectStringMask | kIsNotStringMask));
  AndP(r0, instance_type);
  bne(value_is_white_and_not_data /*, cr0*/);
  // It's a non-indirect (non-cons and non-slice) string.
  // If it's external, the length is just ExternalString::kSize.
  // Otherwise it's String::kHeaderSize + string->length() * (1 or 2).
  // External strings are the only ones with the kExternalStringTag bit
  // set.
  ASSERT_EQ(0, kSeqStringTag & kExternalStringTag);
  ASSERT_EQ(0, kConsStringTag & kExternalStringTag);
  mov(r0, Operand(kExternalStringTag));
  AndP(r0, instance_type);
  beq(&is_string_object /*, cr0*/);
  LoadImmP(length, Operand(ExternalString::kSize));
  b(&is_data_object);
  bind(&is_string_object);

  // Sequential string, either ASCII or UC16.
  // For ASCII (char-size of 1) we untag the smi to get the length.
  // For UC16 (char-size of 2):
  //   - (32-bit) we just leave the smi tag in place, thereby getting
  //              the length multiplied by 2.
  //   - (64-bit) we compute the offset in the 2-byte array
  ASSERT(kAsciiStringTag == 4 && kStringEncodingMask == 4);
  LoadP(ip, FieldMemOperand(value, String::kLengthOffset));
  mov(r0, Operand(kStringEncodingMask));
  AndP(r0, instance_type);
  beq(&is_encoded /*, cr0*/);
  SmiUntag(ip);
#if V8_TARGET_ARCH_S390X
  b(&length_computed);
#endif
  bind(&is_encoded);
#if V8_TARGET_ARCH_S390X
  SmiToShortArrayOffset(ip, ip);
  bind(&length_computed);
#else
  ASSERT(kSmiShift == 1);
#endif
  AddP(length, ip, Operand(SeqString::kHeaderSize + kObjectAlignmentMask));
  LoadImmP(r0, Operand(~kObjectAlignmentMask));
  AndP(length, r0);

  bind(&is_data_object);
  // Value is a data object, and it is white.  Mark it black.  Since we know
  // that the object is white we can make it black by flipping one bit.
  LoadlW(ip, MemOperand(bitmap_scratch, MemoryChunk::kHeaderSize));
  OrP(ip, mask_scratch);
  StoreW(ip, MemOperand(bitmap_scratch, MemoryChunk::kHeaderSize));

  mov(ip, Operand(~Page::kPageAlignmentMask));
  AndP(bitmap_scratch, ip);
  LoadlW(ip, MemOperand(bitmap_scratch, MemoryChunk::kLiveBytesOffset));
  AddP(ip, length);
  StoreW(ip, MemOperand(bitmap_scratch, MemoryChunk::kLiveBytesOffset));

  bind(&done);
}

// Saturate a value into 8-bit unsigned integer
//   if input_value < 0, output_value is 0
//   if input_value > 255, output_value is 255
//   otherwise output_value is the input_value
void MacroAssembler::ClampUint8(Register output_reg, Register input_reg) {
  Label done, negative_label, overflow_label;
  int satval = (1 << 8) - 1;

  CmpP(input_reg, Operand::Zero());
  blt(&negative_label);

  CmpP(input_reg, Operand(satval));
  bgt(&overflow_label);
  if (!output_reg.is(input_reg)) {
    LoadRR(output_reg, input_reg);
  }
  b(&done);

  bind(&negative_label);
  LoadImmP(output_reg, Operand::Zero());  // set to 0 if negative
  b(&done);


  bind(&overflow_label);  // set to satval if > satval
  LoadImmP(output_reg, Operand(satval));

  bind(&done);
}

void MacroAssembler::ClampDoubleToUint8(Register result_reg,
                                        DoubleRegister input_reg,
                                        DoubleRegister temp_double_reg,
                                        DoubleRegister temp_double_reg2) {
  Label above_zero;
  Label done;
  Label in_bounds;

  LoadDoubleLiteral(temp_double_reg, 0.0, result_reg);
  cdbr(input_reg, temp_double_reg);
  bgt(&above_zero);

  // Double value is less than zero, NaN or Inf, return 0.
  LoadIntLiteral(result_reg, 0);
  b(&done);

  // Double value is >= 255, return 255.
  bind(&above_zero);
  LoadDoubleLiteral(temp_double_reg, 255.0, result_reg);
  cdbr(input_reg, temp_double_reg);
  ble(&in_bounds);
  LoadIntLiteral(result_reg, 255);
  b(&done);

  // In 0-255 range, round and truncate.
  bind(&in_bounds);

  // round to nearest (default rounding mode)
  cfdbr(ROUND_TO_NEAREST_WITH_TIES_TO_EVEN, result_reg, input_reg);

  bind(&done);
}

void MacroAssembler::LoadInstanceDescriptors(Register map,
                                             Register descriptors) {
  LoadP(descriptors, FieldMemOperand(map, Map::kDescriptorsOffset));
}


void MacroAssembler::NumberOfOwnDescriptors(Register dst, Register map) {
  LoadP(dst, FieldMemOperand(map, Map::kBitField3Offset));
  DecodeField<Map::NumberOfOwnDescriptorsBits>(dst);
}


void MacroAssembler::EnumLength(Register dst, Register map) {
  STATIC_ASSERT(Map::EnumLengthBits::kShift == 0);
  LoadP(dst, FieldMemOperand(map, Map::kBitField3Offset));
  LoadSmiLiteral(r0, Smi::FromInt(Map::EnumLengthBits::kMask));
  AndP(dst, r0);
}


void MacroAssembler::CheckEnumCache(Register null_value, Label* call_runtime) {
  Register  empty_fixed_array_value = r8;
  LoadRoot(empty_fixed_array_value, Heap::kEmptyFixedArrayRootIndex);
  Label next, start;
  LoadRR(r4, r2);

  // Check if the enum length field is properly initialized, indicating that
  // there is an enum cache.
  LoadP(r3, FieldMemOperand(r4, HeapObject::kMapOffset));

  EnumLength(r5, r3);
  CmpSmiLiteral(r5, Smi::FromInt(Map::kInvalidEnumCache), r0);
  beq(call_runtime);

  b(&start);

  bind(&next);
  LoadP(r3, FieldMemOperand(r4, HeapObject::kMapOffset));

  // For all objects but the receiver, check that the cache is empty.
  EnumLength(r5, r3);
  CmpSmiLiteral(r5, Smi::FromInt(0), r0);
  bne(call_runtime);

  bind(&start);

  // Check that there are no elements. Register r4 contains the current JS
  // object we've reached through the prototype chain.
  LoadP(r4, FieldMemOperand(r4, JSObject::kElementsOffset));
  CmpP(r4, empty_fixed_array_value);
  bne(call_runtime);

  LoadP(r4, FieldMemOperand(r3, Map::kPrototypeOffset));
  CmpP(r4, null_value);
  bne(&next);
}


////////////////////////////////////////////////////////////////////////////////
//
// New MacroAssembler Interfaces added for S390
//
////////////////////////////////////////////////////////////////////////////////
// Primarily used for loading constants
// This should really move to be in macro-assembler as it
// is really a pseudo instruction
// Some usages of this intend for a FIXED_SEQUENCE to be used
// @TODO - break this dependency so we can optimize mov() in general
// and only use the generic version when we require a fixed sequence
void MacroAssembler::mov(Register dst, const Operand& src) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (src.rmode_ != RelocInfo::NONE) {
    // some form of relocation needed
    RecordRelocInfo(src.rmode_, src.imm_);
  }

#if V8_TARGET_ARCH_S390X
  int64_t value = src.immediate();
  int32_t hi_32 = static_cast<int64_t>(value) >> 32;
  int32_t lo_32 = static_cast<int32_t>(value);

  iihf(dst, Operand(hi_32));
  iilf(dst, Operand(lo_32));
#else
  int value = src.immediate();
  iilf(dst, Operand(value));
#endif
}

void MacroAssembler::Mul(Register dst, Register src1, Register src2) {
  Move(dst, src1);
  MulP(dst, src2);
}

void MacroAssembler::DivP(Register dividend, Register divider) {
  // have to make sure the src and dst are reg pairs
  ASSERT(dividend.code() % 2 == 0);
#if V8_TARGET_ARCH_S390X
  dsgr(dividend, divider);
#else
  dr(dividend, divider);
#endif
}

void MacroAssembler::MulP(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  msgfi(dst, opnd);
#else
  msfi(dst, opnd);
#endif
}

void MacroAssembler::MulP(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  msgr(dst, src);
#else
  msr(dst, src);
#endif
}

void MacroAssembler::MulP(Register dst, const MemOperand& opnd) {
#if V8_TARGET_ARCH_S390X
  if (is_uint16(opnd.offset())) {
    ms(dst, opnd);
  } else if (is_int20(opnd.offset())) {
    msy(dst, opnd);
  } else {
    UNIMPLEMENTED();
  }
#else
  if (is_int20(opnd.offset())) {
    msg(dst, opnd);
  } else {
    UNIMPLEMENTED();
  }
#endif
}

//----------------------------------------------------------------------------
//  Add Instructions
//----------------------------------------------------------------------------

// Add 32-bit (Register dst = Register dst + Immediate opnd)
void MacroAssembler::Add32(Register dst, const Operand& opnd) {
  if (is_int16(opnd.immediate()))
    ahi(dst, opnd);
  else
    afi(dst, opnd);
}

// Add Pointer Size (Register dst = Register dst + Immediate opnd)
void MacroAssembler::AddP(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  if (is_int16(opnd.immediate()))
    aghi(dst, opnd);
  else
    agfi(dst, opnd);
#else
  Add32(dst, opnd);
#endif
}

// Add 32-bit (Register dst = Register src + Immediate opnd)
void MacroAssembler::Add32(Register dst, Register src, const Operand& opnd) {
  if (!dst.is(src)) {
    if (CpuFeatures::IsSupported(DISTINCT_OPS) && is_int16(opnd.immediate())) {
      ahik(dst, src, opnd);
      return;
    }
    lr(dst, src);
  }
  Add32(dst, opnd);
}

// Add Pointer Size (Register dst = Register src + Immediate opnd)
void MacroAssembler::AddP(Register dst, Register src, const Operand& opnd) {
  if (!dst.is(src)) {
    if (CpuFeatures::IsSupported(DISTINCT_OPS) && is_int16(opnd.immediate())) {
      AddPImm_RRI(dst, src, opnd);
      return;
    }
    LoadRR(dst, src);
  }
  AddP(dst, opnd);
}

// Add 32-bit (Register dst = Register dst + Register src)
void MacroAssembler::Add32(Register dst, Register src) {
  ar(dst, src);
}

// Add Pointer Size (Register dst = Register dst + Register src)
void MacroAssembler::AddP(Register dst, Register src) {
  AddRR(dst, src);
}

// Add Pointer Size with src extension
//     (Register dst(ptr) = Register dst (ptr) + Register src (32 | 32->64))
// src is treated as a 32-bit signed integer, which is sign extended to
// 64-bit if necessary.
void MacroAssembler::AddP_ExtendSrc(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  agfr(dst, src);
#else
  ar(dst, src);
#endif
}

// Add 32-bit (Register dst = Register src1 + Register src2)
void MacroAssembler::Add32(Register dst, Register src1, Register src2) {
  if (!dst.is(src1) && !dst.is(src2)) {
    // We prefer to generate AR/AGR, over the non clobbering ARK/AGRK
    // as AR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      ark(dst, src1, src2);
      return;
    } else {
      lr(dst, src1);
    }
  } else if (dst.is(src2)) {
    src2 = src1;
  }
  ar(dst, src2);
}

// Add Pointer Size (Register dst = Register src1 + Register src2)
void MacroAssembler::AddP(Register dst, Register src1, Register src2) {
  if (!dst.is(src1) && !dst.is(src2)) {
    // We prefer to generate AR/AGR, over the non clobbering ARK/AGRK
    // as AR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      AddP_RRR(dst, src1, src2);
      return;
    } else {
      LoadRR(dst, src1);
    }
  } else if (dst.is(src2)) {
    src2 = src1;
  }
  AddRR(dst, src2);
}

// Add Pointer Size with src extension
//      (Register dst (ptr) = Register dst (ptr) + Register src1 (ptr) +
//                            Register src2 (32 | 32->64))
// src is treated as a 32-bit signed integer, which is sign extended to
// 64-bit if necessary.
void MacroAssembler::AddP_ExtendSrc(Register dst, Register src1,
                                    Register src2) {
#if V8_TARGET_ARCH_S390X
  if (dst.is(src2)) {
    // The source we need to sign extend is the same as result.
    lgfr(dst, src2);
    agr(dst, src1);
  } else {
    if (!dst.is(src1))
      LoadRR(dst, src1);
    agfr(dst, src2);
  }
#else
  AddP(dst, src1, src2);
#endif
}

// Add 32-bit (Register-Memory)
void MacroAssembler::Add32(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    a(dst, opnd);
  else
    ay(dst, opnd);
}

// Add Pointer Size (Register-Memory)
void MacroAssembler::AddP(Register dst, const MemOperand& opnd) {
#if V8_TARGET_ARCH_S390X
  ASSERT(is_int20(opnd.offset()));
  ag(dst, opnd);
#else
  Add32(dst, opnd);
#endif
}

// Add Pointer Size with src extension
//      (Register dst (ptr) = Register dst (ptr) + Mem opnd (32 | 32->64))
// src is treated as a 32-bit signed integer, which is sign extended to
// 64-bit if necessary.
void MacroAssembler::AddP_ExtendSrc(Register dst, const MemOperand& opnd) {
#if V8_TARGET_ARCH_S390X
  ASSERT(is_int20(opnd.offset()));
  agf(dst, opnd);
#else
  Add32(dst, opnd);
#endif
}

//----------------------------------------------------------------------------
//  Add Logical Instructions
//----------------------------------------------------------------------------

// Add Logical 32-bit (Register dst = Register dst + Immediate opnd)
void MacroAssembler::AddLogical(Register dst, const Operand& imm) {
  alfi(dst, imm);
}

// Add Logical Pointer Size (Register dst = Register dst + Immediate opnd)
void MacroAssembler::AddLogicalP(Register dst, const Operand& imm) {
#ifdef V8_TARGET_ARCH_S390X
  algfi(dst, imm);
#else
  AddLogical(dst, imm);
#endif
}

// Add Logical 32-bit (Register-Memory)
void MacroAssembler::AddLogical(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    al_z(dst, opnd);
  else
    aly(dst, opnd);
}

// Add Logical Pointer Size (Register-Memory)
void MacroAssembler::AddLogicalP(Register dst, const MemOperand& opnd) {
#if V8_TARGET_ARCH_S390X
  ASSERT(is_int20(opnd.offset()));
  alg(dst, opnd);
#else
  AddLogical(dst, opnd);
#endif
}

//----------------------------------------------------------------------------
//  Subtract Instructions
//----------------------------------------------------------------------------

// Subtract 32-bit (Register dst = Register dst - Immediate opnd)
void MacroAssembler::Sub32(Register dst, const Operand& imm) {
  Add32(dst, Operand(-(imm.imm_)));
}

// Subtract Pointer Size (Register dst = Register dst - Immediate opnd)
void MacroAssembler::SubP(Register dst, const Operand& imm) {
  AddP(dst, Operand(-(imm.imm_)));
}

// Subtract 32-bit (Register dst = Register src - Immediate opnd)
void MacroAssembler::Sub32(Register dst, Register src, const Operand& imm) {
  Add32(dst, src, Operand(-(imm.imm_)));
}

// Subtract Pointer Sized (Register dst = Register src - Immediate opnd)
void MacroAssembler::SubP(Register dst, Register src, const Operand& imm) {
  AddP(dst, src, Operand(-(imm.imm_)));
}

// Subtract 32-bit (Register dst = Register dst - Register src)
void MacroAssembler::Sub32(Register dst, Register src) {
  sr(dst, src);
}

// Subtract Pointer Size (Register dst = Register dst - Register src)
void MacroAssembler::SubP(Register dst, Register src) {
  SubRR(dst, src);
}

// Subtract Pointer Size with src extension
//     (Register dst(ptr) = Register dst (ptr) - Register src (32 | 32->64))
// src is treated as a 32-bit signed integer, which is sign extended to
// 64-bit if necessary.
void MacroAssembler::SubP_ExtendSrc(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  sgfr(dst, src);
#else
  sr(dst, src);
#endif
}

// Subtract 32-bit (Register = Register - Register)
void MacroAssembler::Sub32(Register dst, Register src1, Register src2) {
  // Use non-clobbering version if possible
  if (CpuFeatures::IsSupported(DISTINCT_OPS) && !dst.is(src1)) {
    srk(dst, src1, src2);
    return;
  }
  if (!dst.is(src1) && !dst.is(src2))
    lr(dst, src1);
  // In scenario where we have dst = src - dst, we need to swap and negate
  if (!dst.is(src1) && dst.is(src2)) {
    sr(dst, src1);  // dst = (dst - src)
    lcr(dst, dst);  // dst = -dst
  } else {
    sr(dst, src2);
  }
}

// Subtract Pointer Sized (Register = Register - Register)
void MacroAssembler::SubP(Register dst, Register src1, Register src2) {
  // Use non-clobbering version if possible
  if (CpuFeatures::IsSupported(DISTINCT_OPS) && !dst.is(src1)) {
    SubP_RRR(dst, src1, src2);
    return;
  }
  if (!dst.is(src1) && !dst.is(src2))
    LoadRR(dst, src1);
  // In scenario where we have dst = src - dst, we need to swap and negate
  if (!dst.is(src1) && dst.is(src2)) {
    SubP(dst, src1);  // dst = (dst - src)
    LoadComplementRR(dst, dst);  // dst = -dst
  } else {
    SubP(dst, src2);
  }
}

// Subtract Pointer Size with src extension
//     (Register dst(ptr) = Register dst (ptr) - Register src (32 | 32->64))
// src is treated as a 32-bit signed integer, which is sign extended to
// 64-bit if necessary.
void MacroAssembler::SubP_ExtendSrc(Register dst, Register src1,
                                    Register src2) {
#if V8_TARGET_ARCH_S390X
  if (!dst.is(src1) && !dst.is(src2))
    LoadRR(dst, src1);

  // In scenario where we have dst = src - dst, we need to swap and negate
  if (!dst.is(src1) && dst.is(src2)) {
    lgfr(dst, dst);  // Sign extend this operand first.
    SubP(dst, src1);  // dst = (dst - src)
    LoadComplementRR(dst, dst);  // dst = -dst
  } else {
    sgfr(dst, src2);
  }
#else
  SubP(dst, src1, src2);
#endif
}

// Subtract 32-bit (Register-Memory)
void MacroAssembler::Sub32(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    s(dst, opnd);
  else
    sy(dst, opnd);
}

// Subtract Pointer Sized (Register - Memory)
void MacroAssembler::SubP(Register dst, const MemOperand& opnd) {
#if V8_TARGET_ARCH_S390X
  sg(dst, opnd);
#else
  Sub32(dst, opnd);
#endif
}

// Subtract Pointer Size with src extension
//      (Register dst (ptr) = Register dst (ptr) - Mem opnd (32 | 32->64))
// src is treated as a 32-bit signed integer, which is sign extended to
// 64-bit if necessary.
void MacroAssembler::SubP_ExtendSrc(Register dst, const MemOperand& opnd) {
#if V8_TARGET_ARCH_S390X
  ASSERT(is_int20(opnd.offset()));
  sgf(dst, opnd);
#else
  Sub32(dst, opnd);
#endif
}

//----------------------------------------------------------------------------
//  Subtract Logical Instructions
//----------------------------------------------------------------------------

// Subtract Logical 32-bit (Register - Memory)
void MacroAssembler::SubLogical(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    sl(dst, opnd);
  else
    sly(dst, opnd);
}

// Subtract Logical Pointer Sized (Register - Memory)
void MacroAssembler::SubLogicalP(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  slgf(dst, opnd);
#else
  SubLogical(dst, opnd);
#endif
}

// Subtract Logical Pointer Size with src extension
//      (Register dst (ptr) = Register dst (ptr) - Mem opnd (32 | 32->64))
// src is treated as a 32-bit signed integer, which is sign extended to
// 64-bit if necessary.
void MacroAssembler::SubLogicalP_ExtendSrc(Register dst,
                                           const MemOperand& opnd) {
#if V8_TARGET_ARCH_S390X
  ASSERT(is_int20(opnd.offset()));
  slgf(dst, opnd);
#else
  SubLogical(dst, opnd);
#endif
}

//----------------------------------------------------------------------------
//  Bitwise Operations
//----------------------------------------------------------------------------

// AND 32-bit - dst = dst & src
void MacroAssembler::And(Register dst, Register src) {
  nr(dst, src);
}

// AND Pointer Size - dst = dst & src
void MacroAssembler::AndP(Register dst, Register src) {
  AndRR(dst, src);
}

// Non-clobbering AND 32-bit - dst = src1 & src1
void MacroAssembler::And(Register dst, Register src1, Register src2) {
  if (!dst.is(src1) && !dst.is(src2)) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      nrk(dst, src1, src2);
      return;
    } else {
      lr(dst, src1);
    }
  } else if (dst.is(src2)) {
    src2 = src1;
  }
  And(dst, src2);
}

// Non-clobbering AND pointer size - dst = src1 & src1
void MacroAssembler::AndP(Register dst, Register src1, Register src2) {
  if (!dst.is(src1) && !dst.is(src2)) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      AndP_RRR(dst, src1, src2);
      return;
    } else {
      LoadRR(dst, src1);
    }
  } else if (dst.is(src2)) {
    src2 = src1;
  }
  AndP(dst, src2);
}

// AND 32-bit (Reg - Mem)
void MacroAssembler::And(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    n(dst, opnd);
  else
    ny(dst, opnd);
}

// AND Pointer Size (Reg - Mem)
void MacroAssembler::AndP(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  ng(dst, opnd);
#else
  And(dst, opnd);
#endif
}

// AND 32-bit - dst = dst & imm
void MacroAssembler::And(Register dst, const Operand& opnd) {
  nilf(dst, opnd);
}

// AND Pointer Size - dst = dst & imm
void MacroAssembler::AndP(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  intptr_t value = opnd.imm_;
  if (value >> 32 != -1) {
    // this may not work b/c condition code won't be set correctly
    nihf(dst, Operand(value >> 32));
  }
  nilf(dst, Operand(value & 0xFFFFFFFF));
#else
  And(dst, opnd);
#endif
}

// AND 32-bit - dst = src & imm
void MacroAssembler::And(Register dst, Register src, const Operand& opnd) {
  if (!dst.is(src))
    lr(dst, src);
  nilf(dst, opnd);
}

// AND Pointer Size - dst = src & imm
void MacroAssembler::AndP(Register dst, Register src, const Operand& opnd) {
  // Try to exploit RISBG first
  intptr_t value = opnd.imm_;
  if (CpuFeatures::IsSupported(GENERAL_INSTR_EXT)) {
    intptr_t shifted_value = value;
    int trailing_zeros = 0;

    // We start checking how many trailing zeros are left at the end.
    while ((0 != shifted_value) && (0 == (shifted_value & 1))) {
       trailing_zeros++;
       shifted_value >>= 1;
    }

    // If temp (value with right-most set of zeros shifted out) is 1 less
    // than power of 2, we have consecutive bits of 1.
    // Special case: If shift_value is zero, we cannot use RISBG, as it requires
    //               selection of at least 1 bit.
    if ((0 != shifted_value) && IsPowerOf2(shifted_value + 1)) {
      int startBit = 32 + CompilerIntrinsics::CountLeadingZeros(shifted_value) -
                     trailing_zeros;
      int endBit = 63 - trailing_zeros;
      // Start: startBit, End: endBit, Shift = 0, true = zero unselected bits.
      risbg(dst, src, Operand(startBit), Operand(endBit), Operand::Zero(),
            true);
      return;
    } else if (-1 == shifted_value) {
    // A Special case in which all top bits up to MSB are 1's.  In this case,
    // we can set startBit to be 0.
      int endBit = 63 - trailing_zeros;
      risbg(dst, src, Operand::Zero(), Operand(endBit), Operand::Zero(), true);
      return;
    }
  }

  // If we are &'ing zero, we can just whack the dst register and skip copy
  if (!dst.is(src) && (0 != value))
     LoadRR(dst, src);
  AndP(dst, opnd);
}

// OR 32-bit - dst = dst & src
void MacroAssembler::Or(Register dst, Register src) {
  or_z(dst, src);
}

// OR Pointer Size - dst = dst & src
void MacroAssembler::OrP(Register dst, Register src) {
  OrRR(dst, src);
}

// Non-clobbering OR 32-bit - dst = src1 & src1
void MacroAssembler::Or(Register dst, Register src1, Register src2) {
  if (!dst.is(src1) && !dst.is(src2)) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      ork(dst, src1, src2);
      return;
    } else {
      lr(dst, src1);
    }
  } else if (dst.is(src2)) {
    src2 = src1;
  }
  Or(dst, src2);
}

// Non-clobbering OR pointer size - dst = src1 & src1
void MacroAssembler::OrP(Register dst, Register src1, Register src2) {
  if (!dst.is(src1) && !dst.is(src2)) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      OrP_RRR(dst, src1, src2);
      return;
    } else {
      LoadRR(dst, src1);
    }
  } else if (dst.is(src2)) {
    src2 = src1;
  }
  OrP(dst, src2);
}

// OR 32-bit (Reg - Mem)
void MacroAssembler::Or(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    o(dst, opnd);
  else
    oy(dst, opnd);
}

// OR Pointer Size (Reg - Mem)
void MacroAssembler::OrP(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  og(dst, opnd);
#else
  Or(dst, opnd);
#endif
}

// OR 32-bit - dst = dst & imm
void MacroAssembler::Or(Register dst, const Operand& opnd) {
  oilf(dst, opnd);
}

// OR Pointer Size - dst = dst & imm
void MacroAssembler::OrP(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  intptr_t value = opnd.imm_;
  if (value >> 32 != 0) {
    // this may not work b/c condition code won't be set correctly
    oihf(dst, Operand(value >> 32));
  }
  oilf(dst, Operand(value & 0xFFFFFFFF));
#else
  Or(dst, opnd);
#endif
}

// OR 32-bit - dst = src & imm
void MacroAssembler::Or(Register dst, Register src, const Operand& opnd) {
  if (!dst.is(src))
    lr(dst, src);
  oilf(dst, opnd);
}

// OR Pointer Size - dst = src & imm
void MacroAssembler::OrP(Register dst, Register src, const Operand& opnd) {
  if (!dst.is(src))
    LoadRR(dst, src);
  OrP(dst, opnd);
}

// XOR 32-bit - dst = dst & src
void MacroAssembler::Xor(Register dst, Register src) {
  xr(dst, src);
}

// XOR Pointer Size - dst = dst & src
void MacroAssembler::XorP(Register dst, Register src) {
  XorRR(dst, src);
}

// Non-clobbering XOR 32-bit - dst = src1 & src1
void MacroAssembler::Xor(Register dst, Register src1, Register src2) {
  if (!dst.is(src1) && !dst.is(src2)) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      xrk(dst, src1, src2);
      return;
    } else {
      lr(dst, src1);
    }
  } else if (dst.is(src2)) {
    src2 = src1;
  }
  Xor(dst, src2);
}

// Non-clobbering XOR pointer size - dst = src1 & src1
void MacroAssembler::XorP(Register dst, Register src1, Register src2) {
  if (!dst.is(src1) && !dst.is(src2)) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      XorP_RRR(dst, src1, src2);
      return;
    } else {
      LoadRR(dst, src1);
    }
  } else if (dst.is(src2)) {
    src2 = src1;
  }
  XorP(dst, src2);
}

// XOR 32-bit (Reg - Mem)
void MacroAssembler::Xor(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    x(dst, opnd);
  else
    xy(dst, opnd);
}

// XOR Pointer Size (Reg - Mem)
void MacroAssembler::XorP(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  xg(dst, opnd);
#else
  Xor(dst, opnd);
#endif
}

// XOR 32-bit - dst = dst & imm
void MacroAssembler::Xor(Register dst, const Operand& opnd) {
  xilf(dst, opnd);
}

// XOR Pointer Size - dst = dst & imm
void MacroAssembler::XorP(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  intptr_t value = opnd.imm_;
  xihf(dst, Operand(value >> 32));
  xilf(dst, Operand(value & 0xFFFFFFFF));
#else
  Xor(dst, opnd);
#endif
}

// XOR 32-bit - dst = src & imm
void MacroAssembler::Xor(Register dst, Register src, const Operand& opnd) {
  if (!dst.is(src))
    lr(dst, src);
  xilf(dst, opnd);
}

// XOR Pointer Size - dst = src & imm
void MacroAssembler::XorP(Register dst, Register src, const Operand& opnd) {
  if (!dst.is(src))
    LoadRR(dst, src);
  XorP(dst, opnd);
}

// NOT 32-bit
void MacroAssembler::Not32(Register dst) {
  XorP(dst, Operand(0xFFFFFFFF));
}

// NOT Pointer Size
void MacroAssembler::NotP(Register dst) {
#if V8_TARGET_ARCH_S390X
  xihf(dst, Operand(0xFFFFFFFF));
  xilf(dst, Operand(0xFFFFFFFF));
#else
  Not32(dst);
#endif
}

// works the same as mov
void MacroAssembler::Load(Register dst, const Operand& opnd) {
  intptr_t value = opnd.immediate();
  if (is_int16(value)) {
#if V8_TARGET_ARCH_S390X
    lghi(dst, opnd);
#else
    lhi(dst, opnd);
#endif
  } else {
#if V8_TARGET_ARCH_S390X
    llilf(dst, opnd);
#else
    iilf(dst, opnd);
#endif
  }
}

void MacroAssembler::Load(Register dst, const MemOperand& opnd) {
  ASSERT(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  lgf(dst, opnd);  // 64<-32
#else
  if (is_uint12(opnd.offset())) {
    l(dst, opnd);
  } else {
    ly(dst, opnd);
  }
#endif
}

//-----------------------------------------------------------------------------
//  Compare Helpers
//-----------------------------------------------------------------------------

// Compare 32-bit Register vs Register
void MacroAssembler::Cmp32(Register src1, Register src2) {
  cr_z(src1, src2);
}

// Compare Pointer Sized Register vs Register
void MacroAssembler::CmpP(Register src1, Register src2) {
#if V8_TARGET_ARCH_S390X
  cgr(src1, src2);
#else
  Cmp32(src1, src2);
#endif
}

// Compare 32-bit Register vs Immediate
// This helper will set up proper relocation entries if required.
void MacroAssembler::Cmp32(Register dst, const Operand& opnd) {
  if (opnd.rmode_ == RelocInfo::NONE) {
    intptr_t value = opnd.immediate();
    if (is_int16(value))
      chi(dst, opnd);
    else
      cfi(dst, opnd);
  } else {
    // Need to generate relocation record here
    RecordRelocInfo(opnd.rmode_, opnd.imm_);
    cfi(dst, opnd);
  }
}

// Compare Pointer Sized  Register vs Immediate
// This helper will set up proper relocation entries if required.
void MacroAssembler::CmpP(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  if (opnd.rmode_ == RelocInfo::NONE) {
    cgfi(dst, opnd);
  } else {
    mov(r0, opnd);   // Need to generate 64-bit relocation
    Cmp32(dst, r0);
  }
#else
  Cmp32(dst, opnd);
#endif
}

// Compare 32-bit Register vs Memory
void MacroAssembler::Cmp32(Register dst, const MemOperand& opnd) {
  // make sure offset is within 20 bit range
  ASSERT(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    c(dst, opnd);
  else
    cy(dst, opnd);
}

// Compare Pointer Size Register vs Memory
void MacroAssembler::CmpP(Register dst, const MemOperand& opnd) {
  // make sure offset is within 20 bit range
  ASSERT(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  cg(dst, opnd);
#else
  Cmp32(dst, opnd);
#endif
}

//-----------------------------------------------------------------------------
// Compare Logical Helpers
//-----------------------------------------------------------------------------

// Compare Logical 32-bit Register vs Register
void MacroAssembler::CmpLogical32(Register dst, Register src) {
  clr(dst, src);
}

// Compare Logical Pointer Sized Register vs Register
void MacroAssembler::CmpLogicalP(Register dst, Register src) {
#ifdef V8_TARGET_ARCH_S390X
  clgr(dst, src);
#else
  CmpLogical32(dst, src);
#endif
}

// Compare Logical 32-bit Register vs Immediate
void MacroAssembler::CmpLogical32(Register dst, const Operand& opnd) {
  clfi(dst, opnd);
}

// Compare Logical Pointer Sized Register vs Immediate
void MacroAssembler::CmpLogicalP(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  ASSERT(static_cast<uint32_t>(opnd.immediate() >> 32) == 0);
  clgfi(dst, opnd);
#else
  CmpLogical32(dst, opnd);
#endif
}

// Compare Logical 32-bit Register vs Memory
void MacroAssembler::CmpLogical32(Register dst, const MemOperand& opnd) {
  // make sure offset is within 20 bit range
  ASSERT(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    cl(dst, opnd);
  else
    cly(dst, opnd);
}

// Compare Logical Pointer Sized Register vs Memory
void MacroAssembler::CmpLogicalP(Register dst, const MemOperand& opnd) {
  // make sure offset is within 20 bit range
  ASSERT(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  clg(dst, opnd);
#else
  CmpLogical32(dst, opnd);
#endif
}

// Compare Logical Byte (Mem - Imm)
void MacroAssembler::CmpLogicalByte(const MemOperand& mem, const Operand& imm) {
  ASSERT(is_uint8(imm.immediate()));
  if (is_uint12(mem.offset()))
    cli(mem, imm);
  else
    cliy(mem, imm);
}



void MacroAssembler::Branch(Condition c, const Operand& opnd) {
  intptr_t value = opnd.immediate();
  if (is_int16(value))
    brc(c, opnd);
  else
    brcl(c, opnd);
}

// Branch On Count.  Decrement R1, and branch if R1 != 0.
void MacroAssembler::BranchOnCount(Register r1, Label *l) {
  int32_t offset = branch_offset(l, false);
  positions_recorder()->WriteRecordedPositions();
  if (is_int16(offset)) {
#if V8_TARGET_ARCH_S390X
    brctg(r1, Operand(offset));
#else
    brct(r1, Operand(offset));
#endif
  } else {
    AddP(r1, Operand(-1));
    Branch(ne, Operand(offset));
  }
}

void MacroAssembler::LoadIntLiteral(Register dst, int value) {
  Load(dst, Operand(value));
}

void MacroAssembler::LoadSmiLiteral(Register dst, Smi *smi) {
  intptr_t value = reinterpret_cast<intptr_t>(smi);
#if V8_TARGET_ARCH_S390X
  ASSERT((value & 0xffffffff) == 0);
  // The smi value is loaded in upper 32-bits.  Lower 32-bit are zeros.
  llihf(dst, Operand(value >> 32));
#else
  llilf(dst, Operand(value));
#endif
}

void MacroAssembler::LoadDoubleLiteral(DoubleRegister result,
                                       double value,
                                       Register scratch) {
  uint64_t int_val = BitCast<uint64_t, double>(value);
  uint32_t hi_32 = int_val >> 32;
  uint32_t lo_32 = static_cast<uint32_t>(int_val);

  // Load the 64-bit value into a GPR, then transfer it to FPR via LDGR
  iihf(scratch, Operand(hi_32));
  iilf(scratch, Operand(lo_32));
  ldgr(result, scratch);
}

void MacroAssembler::CmpSmiLiteral(Register src1, Smi *smi, Register scratch) {
#if V8_TARGET_ARCH_S390X
  LoadSmiLiteral(scratch, smi);
  cgr(src1, scratch);
#else
  // CFI takes 32-bit immediate.
  cfi(src1, Operand(smi));
#endif
}

void MacroAssembler::CmpLogicalSmiLiteral(Register src1, Smi *smi,
                                          Register scratch) {
#if V8_TARGET_ARCH_S390X
  LoadSmiLiteral(scratch, smi);
  clgr(src1, scratch);
#else
  // CLFI takes 32-bit immediate
  clfi(src1, Operand(smi));
#endif
}

void MacroAssembler::AddSmiLiteral(Register dst, Register src, Smi *smi,
                                   Register scratch) {
#if V8_TARGET_ARCH_S390X
  LoadSmiLiteral(scratch, smi);
  AddP(dst, src, scratch);
#else
  AddP(dst, src, Operand(reinterpret_cast<intptr_t>(smi)));
#endif
}

void MacroAssembler::SubSmiLiteral(Register dst, Register src, Smi *smi,
                                   Register scratch) {
#if V8_TARGET_ARCH_S390X
  LoadSmiLiteral(scratch, smi);
  SubP(dst, src, scratch);
#else
  AddP(dst, src, Operand(-(reinterpret_cast<intptr_t>(smi))));
#endif
}

void MacroAssembler::AndSmiLiteral(Register dst, Register src, Smi *smi) {
  if (!dst.is(src))
    LoadRR(dst, src);
#if V8_TARGET_ARCH_S390X
  ASSERT((reinterpret_cast<intptr_t>(smi) & 0xffffffff) == 0);
  int value = static_cast<int>(reinterpret_cast<intptr_t>(smi) >> 32);
  nihf(dst, Operand(value));
#else
  nilf(dst, Operand(reinterpret_cast<int>(smi)));
#endif
}


// Load a "pointer" sized value from the memory location
void MacroAssembler::LoadP(Register dst, const MemOperand& mem,
                           Register scratch) {
  int offset = mem.offset();

  if (!scratch.is(no_reg) && !is_int20(offset)) {
    /* cannot use d-form */
    LoadIntLiteral(scratch, offset);
#if V8_TARGET_ARCH_S390X
    lg(dst, MemOperand(mem.rb(), scratch));
#else
    l(dst, MemOperand(mem.rb(), scratch));
#endif
  } else {
#if V8_TARGET_ARCH_S390X
    lg(dst, mem);
#else
    if (is_uint12(offset)) {
      l(dst, mem);
    } else {
      ly(dst, mem);
    }
#endif
  }
}

// Store a "pointer" sized value to the memory location
void MacroAssembler::StoreP(Register src, const MemOperand& mem,
                           Register scratch) {
  if (!is_int20(mem.offset())) {
    ASSERT(!scratch.is(no_reg));
    LoadIntLiteral(scratch, mem.offset());
#if V8_TARGET_ARCH_S390X
    stg(src, MemOperand(mem.rb(), scratch));
#else
    st(src, MemOperand(mem.rb(), scratch));
#endif
  } else {
#if V8_TARGET_ARCH_S390X
    stg(src, mem);
#else
    // StoreW will try to generate ST if offset fits, otherwise
    // it'll generate STY.
    StoreW(src, mem);
#endif
  }
}

void MacroAssembler::LoadMultipleP(Register dst1, Register dst2,
    const MemOperand& mem) {
#if V8_TARGET_ARCH_S390X
  ASSERT(is_int20(mem.offset()));
  lmg(dst1, dst2, mem);
#else
  if (is_uint12(mem.offset())) {
    lm(dst1, dst2, mem);
  } else {
    ASSERT(is_int20(mem.offset()));
    lmy(dst1, dst2, mem);
  }
#endif
}

void MacroAssembler::StoreMultipleP(Register src1, Register src2,
    const MemOperand& mem) {
#if V8_TARGET_ARCH_S390X
  ASSERT(is_int20(mem.offset()));
  stmg(src1, src2, mem);
#else
  if (is_uint12(mem.offset())) {
    stm(src1, src2, mem);
  } else {
    ASSERT(is_int20(mem.offset()));
    stmy(src1, src2, mem);
  }
#endif
}

void MacroAssembler::LoadMultipleW(Register dst1, Register dst2,
    const MemOperand& mem) {
  if (is_uint12(mem.offset())) {
    lm(dst1, dst2, mem);
  } else {
    ASSERT(is_int20(mem.offset()));
    lmy(dst1, dst2, mem);
  }
}

void MacroAssembler::StoreMultipleW(Register src1, Register src2,
    const MemOperand& mem) {
  if (is_uint12(mem.offset())) {
    stm(src1, src2, mem);
  } else {
    ASSERT(is_int20(mem.offset()));
    stmy(src1, src2, mem);
  }
}

// Load 32-bits and sign extend if necessary.
void MacroAssembler::LoadW(Register dst, const MemOperand& mem,
                           Register scratch) {
  int offset = mem.offset();

  if (!is_int20(offset)) {
    ASSERT(!scratch.is(no_reg));
    LoadIntLiteral(scratch, offset);
#if V8_TARGET_ARCH_S390X
    lgf(dst, MemOperand(mem.rb(), scratch));
#else
    l(dst, MemOperand(mem.rb(), scratch));
#endif
  } else {
#if V8_TARGET_ARCH_S390X
    lgf(dst, mem);
#else
    if (is_uint12(offset)) {
      l(dst, mem);
    } else {
      ly(dst, mem);
    }
#endif
  }
}

// Variable length depending on whether offset fits into immediate field
// MemOperand of RX or RXY format
void MacroAssembler::LoadlW(Register dst, const MemOperand& mem,
                            Register scratch) {
  Register base = mem.rb();
  int offset = mem.offset();

#if V8_TARGET_ARCH_S390X
  if (is_int20(offset)) {
    llgf(dst, mem);
  } else if (!scratch.is(no_reg)) {
    // Materialize offset into scratch register.
    LoadIntLiteral(scratch, offset);
    llgf(dst, MemOperand(base, scratch));
  } else {
    ASSERT(false);
  }
#else
  bool use_RXform = false;
  bool use_RXYform = false;
  if (is_uint12(offset)) {
    // RX-format supports unsigned 12-bits offset.
    use_RXform = true;
  } else if (is_int20(offset)) {
    // RXY-format supports signed 20-bits offset.
    use_RXYform = true;
  } else if (!scratch.is(no_reg)) {
    // Materialize offset into scratch register.
    LoadIntLiteral(scratch, offset);
  } else {
    ASSERT(false);
  }

  if (use_RXform) {
    l(dst, mem);
  } else if (use_RXYform) {
    ly(dst, mem);
  } else {
    ly(dst, MemOperand(base, scratch));
  }
#endif
}

void MacroAssembler::LoadB(Register dst, const MemOperand& mem) {
#if V8_TARGET_ARCH_S390X
  lgb(dst, mem);
#else
  lb(dst, mem);
#endif
}

void MacroAssembler::LoadlB(Register dst, const MemOperand& mem) {
#if V8_TARGET_ARCH_S390X
  llgc(dst, mem);
#else
  llc(dst, mem);
#endif
}

void MacroAssembler::LoadF(DoubleRegister dst, const MemOperand& mem) {
  // for 32bit and 64bit we all use 64bit floating point regs
  if (is_uint12(mem.offset())) {
    ld(dst, mem);
  } else {
    ldy(dst, mem);
  }
}

void MacroAssembler::StoreF(DoubleRegister dst, const MemOperand& mem) {
  // for 32bit and 64bit we all use 64bit floating point regs
  if (is_uint12(mem.offset())) {
    std(dst, mem);
  } else {
    stdy(dst, mem);
  }
}

void MacroAssembler::StoreShortF(DoubleRegister dst, const MemOperand& mem) {
  // for 32bit and 64bit we all use 64bit floating point regs
  if (is_uint12(mem.offset())) {
    ste(dst, mem);
  } else {
    stey(dst, mem);
  }
}


// Variable length depending on whether offset fits into immediate field
// MemOperand of RX or RXY format
void MacroAssembler::StoreW(Register src, const MemOperand& mem,
                            Register scratch) {
  Register base = mem.rb();
  int offset = mem.offset();

  bool use_RXform = false;
  bool use_RXYform = false;

  if (is_uint12(offset)) {
    // RX-format supports unsigned 12-bits offset.
    use_RXform = true;
  } else if (is_int20(offset)) {
    // RXY-format supports signed 20-bits offset.
    use_RXYform = true;
  } else if (!scratch.is(no_reg)) {
    // Materialize offset into scratch register.
    LoadIntLiteral(scratch, offset);
  } else {
    // scratch is no_reg
    ASSERT(false);
  }

  if (use_RXform) {
    st(src, mem);
  } else if (use_RXYform) {
    sty(src, mem);
  } else {
    StoreW(src, MemOperand(base, scratch));
  }
}

// Loads 16-bits half-word value from memory and sign extends to pointer
// sized register
void MacroAssembler::LoadHalfWordP(Register dst, const MemOperand& mem,
                                  Register scratch) {
  Register base = mem.rb();
  int offset = mem.offset();

  if (!is_int20(offset)) {
    ASSERT(!scratch.is(no_reg));
    LoadIntLiteral(scratch, offset);
#if V8_TARGET_ARCH_S390X
    lgh(dst, MemOperand(base, scratch));
#else
    lh(dst, MemOperand(base, scratch));
#endif
  } else {
#if V8_TARGET_ARCH_S390X
    lgh(dst, mem);
#else
    if (is_uint12(offset)) {
      lh(dst, mem);
    } else {
      lhy(dst, mem);
    }
#endif
  }
}

// Variable length depending on whether offset fits into immediate field
// MemOperand current only supports d-form
void MacroAssembler::StoreHalfWord(Register src, const MemOperand& mem,
                                   Register scratch) {
  Register base = mem.rb();
  int offset = mem.offset();

  if (is_uint12(offset)) {
    sth(src, mem);
  } else if (is_int20(offset)) {
    sthy(src, mem);
  } else {
    ASSERT(!scratch.is(no_reg));
    LoadIntLiteral(scratch, offset);
    sth(src, MemOperand(base, scratch));
  }
}

// Variable length depending on whether offset fits into immediate field
// MemOperand current only supports d-form
void MacroAssembler::StoreByte(Register src, const MemOperand& mem,
                               Register scratch) {
  Register base = mem.rb();
  int offset = mem.offset();

  if (is_uint12(offset)) {
    stc(src, mem);
  } else if (is_int20(offset)) {
    stcy(src, mem);
  } else {
    ASSERT(!scratch.is(no_reg));
    LoadIntLiteral(scratch, offset);
    stc(src, MemOperand(base, scratch));
  }
}

// Shift left logical for 32-bit integer types.
void MacroAssembler::ShiftLeft(Register dst, Register src,
                               const Operand& val) {
  if (dst.is(src)) {
    sll(dst, val);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    sllk(dst, src, val);
  } else {
    lr(dst, src);
    sll(dst, val);
  }
}

// Shift left logical for 32-bit integer types.
void MacroAssembler::ShiftLeft(Register dst, Register src,
                               Register val) {
  ASSERT(!dst.is(val));  // The lr/sll path clobbers val.
  if (dst.is(src)) {
    sll(dst, val);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    sllk(dst, src, val);
  } else {
    lr(dst, src);
    sll(dst, val);
  }
}

// Shift right logical for 32-bit integer types.
void MacroAssembler::ShiftRight(Register dst, Register src,
                                const Operand& val) {
  if (dst.is(src)) {
    srl(dst, val);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    srlk(dst, src, val);
  } else {
    lr(dst, src);
    srl(dst, val);
  }
}

// Shift right logical for 32-bit integer types.
void MacroAssembler::ShiftRight(Register dst, Register src,
                                Register val) {
  ASSERT(!dst.is(val));  // The lr/srl path clobbers val.
  if (dst.is(src)) {
    srl(dst, val);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    srlk(dst, src, val);
  } else {
    lr(dst, src);
    srl(dst, val);
  }
}

// Shift left arithmetic for 32-bit integer types.
void MacroAssembler::ShiftLeftArith(Register dst, Register src,
                                    const Operand& val) {
  if (dst.is(src)) {
    sla(dst, val);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    slak(dst, src, val);
  } else {
    lr(dst, src);
    sla(dst, val);
  }
}

// Shift left arithmetic for 32-bit integer types.
void MacroAssembler::ShiftLeftArith(Register dst, Register src,
                                    Register val) {
  ASSERT(!dst.is(val));  // The lr/sla path clobbers val.
  if (dst.is(src)) {
    sla(dst, val);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    slak(dst, src, val);
  } else {
    lr(dst, src);
    sla(dst, val);
  }
}

// Shift right arithmetic for 32-bit integer types.
void MacroAssembler::ShiftRightArith(Register dst, Register src,
                                     const Operand& val) {
  if (dst.is(src)) {
    sra(dst, val);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    srak(dst, src, val);
  } else {
    lr(dst, src);
    sra(dst, val);
  }
}

// Shift right arithmetic for 32-bit integer types.
void MacroAssembler::ShiftRightArith(Register dst, Register src,
                                     Register val) {
  ASSERT(!dst.is(val));  // The lr/sra path clobbers val.
  if (dst.is(src)) {
    sra(dst, val);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    srak(dst, src, val);
  } else {
    lr(dst, src);
    sra(dst, val);
  }
}

// Clear right most # of bits
void MacroAssembler::ClearRightImm(Register dst, Register src,
                                  const Operand& val) {
  int numBitsToClear = val.imm_ % (kPointerSize * 8);

  // Try to use RISBG if possible
  if (CpuFeatures::IsSupported(GENERAL_INSTR_EXT)) {
    int endBit = 63 - numBitsToClear;
    risbg(dst, src, Operand::Zero(), Operand(endBit), Operand::Zero(), true);
    return;
  }

  uint64_t hexMask = ~((1L << numBitsToClear) - 1);

  // S390 AND instr clobbers source.  Make a copy if necessary
  if (!dst.is(src))
    LoadRR(dst, src);

  if (numBitsToClear <= 16) {
    nill(dst, Operand(static_cast<uint16_t>(hexMask)));
  } else if (numBitsToClear <= 32) {
    nilf(dst, Operand(static_cast<uint32_t>(hexMask)));
  } else if (numBitsToClear <= 64) {
    nilf(dst, Operand(static_cast<intptr_t>(0)));
    nihf(dst, Operand(hexMask >> 32));
  }
}

#ifdef DEBUG
bool AreAliased(Register reg1,
                Register reg2,
                Register reg3,
                Register reg4,
                Register reg5,
                Register reg6) {
  int n_of_valid_regs = reg1.is_valid() + reg2.is_valid() +
    reg3.is_valid() + reg4.is_valid() + reg5.is_valid() + reg6.is_valid();

  RegList regs = 0;
  if (reg1.is_valid()) regs |= reg1.bit();
  if (reg2.is_valid()) regs |= reg2.bit();
  if (reg3.is_valid()) regs |= reg3.bit();
  if (reg4.is_valid()) regs |= reg4.bit();
  if (reg5.is_valid()) regs |= reg5.bit();
  if (reg6.is_valid()) regs |= reg6.bit();
  int n_of_non_aliasing_regs = NumRegs(regs);

  return n_of_valid_regs != n_of_non_aliasing_regs;
}
#endif


CodePatcher::CodePatcher(byte* address, int size)
    : address_(address),
      size_(size),
      masm_(NULL, address, size_ + Assembler::kGap) {
  // Create a new macro assembler pointing to the address of the code to patch.
  // The size is adjusted with kGap on order for the assembler to generate size
  // bytes of instructions without failing with buffer size constraints.
  ASSERT(masm_.reloc_info_writer.pos() == address_ + size_ + Assembler::kGap);
}


CodePatcher::~CodePatcher() {
  // Indicate that code has changed.
  CPU::FlushICache(address_, size_);

  // Check that the code was patched as expected.
  ASSERT(masm_.pc_ == address_ + size_);
  ASSERT(masm_.reloc_info_writer.pos() == address_ + size_ + Assembler::kGap);
}

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_S390
