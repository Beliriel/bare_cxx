//===- CodeEmitterGen.cpp - Code Emitter Generator ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// CodeEmitterGen uses the descriptions of instructions and their fields to
// construct an automated code emitter: a function that, given a MachineInstr,
// returns the (currently, 32-bit unsigned) value of the instruction.
//
//===----------------------------------------------------------------------===//

#include "CodeEmitterGen.h"
#include "CodeGenTarget.h"
#include "Record.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

void CodeEmitterGen::reverseBits(std::vector<Record*> &Insts) {
  for (std::vector<Record*>::iterator I = Insts.begin(), E = Insts.end();
       I != E; ++I) {
    Record *R = *I;
    if (R->getValueAsString("Namespace") == "TargetOpcode")
      continue;

    BitsInit *BI = R->getValueAsBitsInit("Inst");

    unsigned numBits = BI->getNumBits();
    BitsInit *NewBI = new BitsInit(numBits);
    for (unsigned bit = 0, end = numBits / 2; bit != end; ++bit) {
      unsigned bitSwapIdx = numBits - bit - 1;
      Init *OrigBit = BI->getBit(bit);
      Init *BitSwap = BI->getBit(bitSwapIdx);
      NewBI->setBit(bit, BitSwap);
      NewBI->setBit(bitSwapIdx, OrigBit);
    }
    if (numBits % 2) {
      unsigned middle = (numBits + 1) / 2;
      NewBI->setBit(middle, BI->getBit(middle));
    }

    // Update the bits in reversed order so that emitInstrOpBits will get the
    // correct endianness.
    R->getValue("Inst")->setValue(NewBI);
  }
}


// If the VarBitInit at position 'bit' matches the specified variable then
// return the variable bit position.  Otherwise return -1.
int CodeEmitterGen::getVariableBit(const std::string &VarName,
            BitsInit *BI, int bit) {
  if (VarBitInit *VBI = dynamic_cast<VarBitInit*>(BI->getBit(bit))) {
    TypedInit *TI = VBI->getVariable();

    if (VarInit *VI = dynamic_cast<VarInit*>(TI)) {
      if (VI->getName() == VarName) return VBI->getBitNum();
    }
  }

  return -1;
}


void CodeEmitterGen::run(raw_ostream &o) {
  CodeGenTarget Target;
  std::vector<Record*> Insts = Records.getAllDerivedDefinitions("Instruction");

  // For little-endian instruction bit encodings, reverse the bit order
  if (Target.isLittleEndianEncoding()) reverseBits(Insts);

  EmitSourceFileHeader("Machine Code Emitter", o);
  std::string Namespace = Insts[0]->getValueAsString("Namespace") + "::";

  const std::vector<const CodeGenInstruction*> &NumberedInstructions =
    Target.getInstructionsByEnumValue();

  // Emit function declaration
  o << "unsigned " << Target.getName() << "CodeEmitter::"
    << "getBinaryCodeForInstr(const MachineInstr &MI) const {\n";

  // Emit instruction base values
  o << "  static const unsigned InstBits[] = {\n";
  for (std::vector<const CodeGenInstruction*>::const_iterator
          IN = NumberedInstructions.begin(),
          EN = NumberedInstructions.end();
       IN != EN; ++IN) {
    const CodeGenInstruction *CGI = *IN;
    Record *R = CGI->TheDef;

    if (R->getValueAsString("Namespace") == "TargetOpcode") {
      o << "    0U,\n";
      continue;
    }

    BitsInit *BI = R->getValueAsBitsInit("Inst");

    // Start by filling in fixed values...
    unsigned Value = 0;
    for (unsigned i = 0, e = BI->getNumBits(); i != e; ++i) {
      if (BitInit *B = dynamic_cast<BitInit*>(BI->getBit(e-i-1))) {
        Value |= B->getValue() << (e-i-1);
      }
    }
    o << "    " << Value << "U," << '\t' << "// " << R->getName() << "\n";
  }
  o << "    0U\n  };\n";

  // Map to accumulate all the cases.
  std::map<std::string, std::vector<std::string> > CaseMap;

  // Construct all cases statement for each opcode
  for (std::vector<Record*>::iterator IC = Insts.begin(), EC = Insts.end();
        IC != EC; ++IC) {
    Record *R = *IC;
    if (R->getValueAsString("Namespace") == "TargetOpcode")
      continue;
    const std::string &InstName = R->getName();
    std::string Case("");

    BitsInit *BI = R->getValueAsBitsInit("Inst");
    const std::vector<RecordVal> &Vals = R->getValues();
    CodeGenInstruction &CGI = Target.getInstruction(R);

    // Loop over all of the fields in the instruction, determining which are the
    // operands to the instruction.
    unsigned NumberedOp = 0;
    for (unsigned i = 0, e = Vals.size(); i != e; ++i) {
      if (!Vals[i].getPrefix() && !Vals[i].getValue()->isComplete()) {
        // Is the operand continuous? If so, we can just mask and OR it in
        // instead of doing it bit-by-bit, saving a lot in runtime cost.
        const std::string &VarName = Vals[i].getName();
        bool gotOp = false;

        for (int bit = BI->getNumBits()-1; bit >= 0; ) {
          int varBit = getVariableBit(VarName, BI, bit);

          if (varBit == -1) {
            --bit;
          } else {
            int beginInstBit = bit;
            int beginVarBit = varBit;
            int N = 1;

            for (--bit; bit >= 0;) {
              varBit = getVariableBit(VarName, BI, bit);
              if (varBit == -1 || varBit != (beginVarBit - N)) break;
              ++N;
              --bit;
            }

            if (!gotOp) {
              // If the operand matches by name, reference according to that
              // operand number. Non-matching operands are assumed to be in
              // order.
              unsigned OpIdx;
              if (CGI.hasOperandNamed(VarName, OpIdx)) {
                // Get the machine operand number for the indicated operand.
                OpIdx = CGI.OperandList[OpIdx].MIOperandNo;
                assert (!CGI.isFlatOperandNotEmitted(OpIdx) &&
                        "Explicitly used operand also marked as not emitted!");
              } else {
                /// If this operand is not supposed to be emitted by the
                /// generated emitter, skip it.
                while (CGI.isFlatOperandNotEmitted(NumberedOp))
                  ++NumberedOp;
                OpIdx = NumberedOp++;
              }
              std::pair<unsigned, unsigned> SO = CGI.getSubOperandNumber(OpIdx);
              std::string &EncoderMethodName =
                CGI.OperandList[SO.first].EncoderMethodName;

              // If the source operand has a custom encoder, use it. This will
              // get the encoding for all of the suboperands.
              if (!EncoderMethodName.empty()) {
                // A custom encoder has all of the information for the
                // sub-operands, if there are more than one, so only
                // query the encoder once per source operand.
                if (SO.second == 0) {
                  Case += "      // op: " + VarName + "\n"
                       + "      op = " + EncoderMethodName + "(MI, "
                       + utostr(OpIdx) + ");\n";
                }
              } else {
                Case += "      // op: " + VarName + "\n"
                     +  "      op = getMachineOpValue(MI, MI.getOperand("
                     +  utostr(OpIdx) + "));\n";
              }
              gotOp = true;
            }

            unsigned opMask = ~0U >> (32-N);
            int opShift = beginVarBit - N + 1;
            opMask <<= opShift;
            opShift = beginInstBit - beginVarBit;

            if (opShift > 0) {
              Case += "      Value |= (op & " + utostr(opMask) + "U) << "
                   +  itostr(opShift) + ";\n";
            } else if (opShift < 0) {
              Case += "      Value |= (op & " + utostr(opMask) + "U) >> "
                   +  itostr(-opShift) + ";\n";
            } else {
              Case += "      Value |= op & " + utostr(opMask) + "U;\n";
            }
          }
        }
      }
    }

    std::vector<std::string> &InstList = CaseMap[Case];
    InstList.push_back(InstName);
  }


  // Emit initial function code
  o << "  const unsigned opcode = MI.getOpcode();\n"
    << "  unsigned Value = InstBits[opcode];\n"
    << "  unsigned op = 0;\n"
    << "  op = op;  // suppress warning\n"
    << "  switch (opcode) {\n";

  // Emit each case statement
  std::map<std::string, std::vector<std::string> >::iterator IE, EE;
  for (IE = CaseMap.begin(), EE = CaseMap.end(); IE != EE; ++IE) {
    const std::string &Case = IE->first;
    std::vector<std::string> &InstList = IE->second;

    for (int i = 0, N = InstList.size(); i < N; i++) {
      if (i) o << "\n";
      o << "    case " << Namespace << InstList[i]  << ":";
    }
    o << " {\n";
    o << Case;
    o << "      break;\n"
      << "    }\n";
  }

  // Default case: unhandled opcode
  o << "  default:\n"
    << "    std::string msg;\n"
    << "    raw_string_ostream Msg(msg);\n"
    << "    Msg << \"Not supported instr: \" << MI;\n"
    << "    report_fatal_error(Msg.str());\n"
    << "  }\n"
    << "  return Value;\n"
    << "}\n\n";
}
