/*
 * Copyright 2011 Christoph Bumiller
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "codegen/nv50_ir.h"
#include "codegen/nv50_ir_build_util.h"

namespace nv50_ir {

class NVC0LegalizeSSA : public Pass
{
private:
   virtual bool visit(BasicBlock *);
   virtual bool visit(Function *);

   // we want to insert calls to the builtin library only after optimization
   void handleDIV(Instruction *); // integer division, modulus
   void handleRCPRSQ(Instruction *); // double precision float recip/rsqrt
   void handleFTZ(Instruction *);
   void handleSET(CmpInstruction *);
   void handleTEXLOD(TexInstruction *);
   void handleShift(Instruction *);

protected:
   BuildUtil bld;
};

class NVC0LegalizePostRA : public Pass
{
public:
   NVC0LegalizePostRA(const Program *);

private:
   virtual bool visit(Function *);
   virtual bool visit(BasicBlock *);

   void replaceZero(Instruction *);
   bool tryReplaceContWithBra(BasicBlock *);
   void propagateJoin(BasicBlock *);

   struct TexUse
   {
      TexUse(Instruction *use, const Instruction *tex, bool after)
         : insn(use), tex(tex), after(after), level(-1) { }
      Instruction *insn;
      const Instruction *tex; // or split / mov
      bool after;
      int level;
   };
   struct Limits
   {
      Limits() { }
      Limits(int min, int max) : min(min), max(max) { }
      int min, max;
   };
   bool insertTextureBarriers(Function *);
   inline bool insnDominatedBy(const Instruction *, const Instruction *) const;
   void findFirstUses(Instruction *texi, std::list<TexUse> &uses);
   void findFirstUsesBB(int minGPR, int maxGPR, Instruction *start,
                        const Instruction *texi, std::list<TexUse> &uses,
                        unordered_set<const BasicBlock *> &visited);
   void addTexUse(std::list<TexUse>&, Instruction *, const Instruction *);
   const Instruction *recurseDef(const Instruction *);

private:
   LValue *rZero;
   LValue *carry;
   LValue *pOne;
   const bool needTexBar;
};

class NVC0LoweringPass : public Pass
{
public:
   NVC0LoweringPass(Program *);

protected:
   bool handleRDSV(Instruction *);
   bool handleWRSV(Instruction *);
   bool handleEXPORT(Instruction *);
   bool handleOUT(Instruction *);
   bool handleDIV(Instruction *);
   bool handleMOD(Instruction *);
   bool handleSQRT(Instruction *);
   bool handlePOW(Instruction *);
   bool handleTEX(TexInstruction *);
   bool handleTXD(TexInstruction *);
   bool handleTXQ(TexInstruction *);
   virtual bool handleManualTXD(TexInstruction *);
   bool handleTXLQ(TexInstruction *);
   bool handleSUQ(TexInstruction *);
   bool handleATOM(Instruction *);
   bool handleCasExch(Instruction *, bool needCctl);
   void handleSurfaceOpGM107(TexInstruction *);
   void handleSurfaceOpNVE4(TexInstruction *);
   void handleSurfaceOpNVC0(TexInstruction *);
   void handleSharedATOM(Instruction *);
   void handleSharedATOMNVE4(Instruction *);
   void handleLDST(Instruction *);
   bool handleBUFQ(Instruction *);

   void checkPredicate(Instruction *);

   virtual bool visit(Instruction *);

private:
   virtual bool visit(Function *);
   virtual bool visit(BasicBlock *);

   void readTessCoord(LValue *dst, int c);

   Value *loadResInfo32(Value *ptr, uint32_t off, uint16_t base);
   Value *loadResInfo64(Value *ptr, uint32_t off, uint16_t base);
   Value *loadResLength32(Value *ptr, uint32_t off, uint16_t base);
   Value *loadSuInfo32(Value *ptr, int slot, uint32_t off);
   Value *loadBufInfo64(Value *ptr, uint32_t off);
   Value *loadBufLength32(Value *ptr, uint32_t off);
   Value *loadUboInfo64(Value *ptr, uint32_t off);
   Value *loadUboLength32(Value *ptr, uint32_t off);
   Value *loadMsInfo32(Value *ptr, uint32_t off);
   Value *loadTexHandle(Value *ptr, unsigned int slot);

   void adjustCoordinatesMS(TexInstruction *);
   void processSurfaceCoordsGM107(TexInstruction *);
   void processSurfaceCoordsNVE4(TexInstruction *);
   void processSurfaceCoordsNVC0(TexInstruction *);
   void convertSurfaceFormat(TexInstruction *);

protected:
   BuildUtil bld;

private:
   const Target *const targ;

   Symbol *gMemBase;
   LValue *gpEmitAddress;
};

} // namespace nv50_ir
