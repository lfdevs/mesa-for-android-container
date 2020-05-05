#include "sfn_instruction_lds.h"

namespace r600 {

void LDSReadInstruction::do_print(std::ostream& os) const
{
   os << "LDS Read  [";
   for (unsigned i = 0; i < m_address.size(); ++i)
      os << *m_dest_value[i] << " ";
   os << "], ";
   for (unsigned i = 0; i < m_address.size(); ++i)
      os << *m_address[i] << " ";
}

LDSReadInstruction::LDSReadInstruction(std::vector<PValue>& address, std::vector<PValue>& value):
   Instruction(lds_read),
   m_address(address),
   m_dest_value(value)
{
   assert(address.size() == value.size());

   for (unsigned i = 0; i < address.size(); ++i) {
      add_remappable_src_value(&m_address[i]);
      add_remappable_dst_value(&m_dest_value[i]);
   }
}

void LDSReadInstruction::replace_values(const ValueSet& candiates, PValue new_value)
{
   for (auto& c : candiates) {
      for (auto& d: m_dest_value) {
         if (*c == *d)
            d = new_value;
      }

      for (auto& a: m_address) {
         if (*c == *a)
            a = new_value;
      }
   }
}

bool LDSReadInstruction::is_equal_to(const Instruction& lhs) const
{
   auto& other = static_cast<const LDSReadInstruction&>(lhs);
   return m_address == other.m_address &&
         m_dest_value == other.m_dest_value;
}

LDSWriteInstruction::LDSWriteInstruction(PValue address, unsigned idx_offset, PValue value0):
   LDSWriteInstruction::LDSWriteInstruction(address, idx_offset, value0, PValue())

{
}

LDSWriteInstruction::LDSWriteInstruction(PValue address, unsigned idx_offset, PValue value0, PValue value1):
   Instruction(lds_write),
   m_address(address),
   m_value0(value0),
   m_value1(value1),
   m_idx_offset(idx_offset)
{
   add_remappable_src_value(&m_address);
   add_remappable_src_value(&m_value0);
   if (m_value1)
      add_remappable_src_value(&m_value1);
}


void LDSWriteInstruction::do_print(std::ostream& os) const
{
   os << "LDS Write" << num_components()
      << " " << address() << ", " << value0();
   if (num_components() > 1)
      os << ", " << value1();
}

void LDSWriteInstruction::replace_values(const ValueSet& candiates, PValue new_value)
{
   for (auto c: candiates) {
      if (*c == *m_address)
         m_address = new_value;

      if (*c == *m_value0)
         m_value0 = new_value;

      if (*c == *m_value1)
         m_value1 = new_value;
   }
}

bool LDSWriteInstruction::is_equal_to(const Instruction& lhs) const
{
   auto& other = static_cast<const LDSWriteInstruction&>(lhs);

   if (m_value1) {
      if (!other.m_value1)
         return false;
      if (*m_value1 != *other.m_value1)
         return false;
   } else {
      if (other.m_value1)
         return false;
   }

   return (m_value0 != other.m_value0 &&
           *m_address != *other.m_address);
}

} // namespace r600
