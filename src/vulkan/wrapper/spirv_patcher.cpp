#include <vector>
#include <map>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "spirv_patcher.hpp"
#include "wrapper_log.h"

namespace Decoration {
   static const uint32_t Builtin = 11;

   namespace Literals {
      static const uint32_t ClipDistance = 3;
      static const uint32_t CullDistance = 4;
   }
}

namespace Capability {
   static const uint32_t ClipDistance = 32;
   static const uint32_t CullDistance = 33;
}

namespace OpCode {
   static const uint32_t OpCapability = 17;
   static const uint32_t OpConstantComposite = 44;
   static const uint32_t OpSpecConstantTrue = 48;
   static const uint32_t OpSpecConstantFalse = 49;
   static const uint32_t OpSpecConstant = 50;
   static const uint32_t OpSpecConstantComposite = 51;
   static const uint32_t OpDecorate = 71;
}

namespace OpType {
	static const uint32_t OpTypeBool = 20;
	static const uint32_t OpTypeVector = 23;
}

void
remove_ClipDistance_CullDistance(uint32_t *pCode, size_t *codeSize)
{
   uint32_t offset = 5;
   std::vector<uint32_t> code(pCode, pCode + (*codeSize / sizeof(uint32_t)));
   std::map<uint32_t, uint32_t> instructions;

   while (offset < code.size()) {
      uint32_t instruction = code[offset];
      uint32_t length = instruction >> 16;
      uint32_t opcode = instruction & 0xffffu;

      if (length == 0 || offset + length > code.size())
         break;

      if (opcode == OpCode::OpCapability) {
         uint32_t capability = code[offset + 1];
         if (capability == Capability::ClipDistance) {
            instructions.insert({offset, length});
         }
         if (capability == Capability::CullDistance) {
	instructions.insert({offset, length});
         }
      }

      if (opcode == OpCode::OpDecorate) {
         uint32_t decoration = code[offset + 2];
         if (decoration == Decoration::Builtin) {
            uint32_t literal = code[offset + 3];
            if (literal == Decoration::Literals::ClipDistance) {
               instructions.insert({offset, length});
            }
            if (literal == Decoration::Literals::CullDistance) {
	instructions.insert({offset, length});
            }
         }
      }

      offset += length;
   }

   for (auto it = instructions.rbegin(); it != instructions.rend(); it++) {
      code.erase(code.begin() + it->first, code.begin() + it->first + it->second);
   }

   *codeSize = sizeof(uint32_t) * code.size();
   memcpy(pCode, code.data(), *codeSize);
}

void
patch_OpConstantComposite_to_OpSpecConstantComposite(uint32_t *pCode, uint32_t codeSize)
{
   std::unordered_set<uint32_t> bool_constants;
   std::unordered_map<uint32_t, uint32_t> vector_bool_constants;
   std::unordered_set<uint32_t> spec_constants;
   uint32_t offset = 5;

   while (offset < codeSize) {
      uint32_t instruction = pCode[offset];
      uint32_t length = instruction >> 16;
      uint32_t opcode = instruction & 0xffffu;

      if (length == 0 || offset + length > codeSize)
         break;

      if (opcode == OpType::OpTypeBool)
         bool_constants.insert(pCode[offset + 1]);

      if (opcode == OpType::OpTypeVector) {
         uint32_t variable_id = pCode[offset + 1];
	 uint32_t component_type = pCode[offset + 2];
	 uint32_t component_number = pCode[offset + 3];
	 if (bool_constants.count(component_type))
	    vector_bool_constants[variable_id] = component_number;
      }

      if (opcode == OpCode::OpSpecConstantTrue || opcode == OpCode::OpSpecConstantFalse || opcode == OpCode::OpSpecConstant)
         spec_constants.insert(pCode[offset + 2]);

      if (opcode == OpCode::OpConstantComposite) {
         uint32_t component_type = pCode[offset + 1];
         auto it = vector_bool_constants.find(component_type);
         if (it != vector_bool_constants.end()) {
	uint32_t component_number = it->second;
            uint32_t matches = 0;
	for (uint32_t index = 0; index < component_number; index ++) {
               uint32_t component = pCode[offset + 3 + index];
	   if (spec_constants.count(component))
	      matches++;
	}
	if (matches == component_number)
	   pCode[offset] = (pCode[offset] & ~0xffffu) | (OpCode::OpSpecConstantComposite & 0xffffu);
         }
      }

      offset += length;
   }
}
