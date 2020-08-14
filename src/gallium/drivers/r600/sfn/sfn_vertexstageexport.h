#ifndef VERTEXSTAGEEXPORT_H
#define VERTEXSTAGEEXPORT_H

#include "sfn_shader_base.h"

namespace r600 {

class VertexStage : public ShaderFromNirProcessor {
public:
   using ShaderFromNirProcessor::ShaderFromNirProcessor;

   virtual PValue primitive_id() = 0;
};

class VertexStageExportBase
{
public:
   VertexStageExportBase(VertexStage& proc);
   virtual ~VertexStageExportBase();
   virtual bool store_deref(const nir_variable *out_var, nir_intrinsic_instr* instr) = 0;
   virtual void finalize_exports() = 0;
   virtual bool do_process_outputs(nir_variable *output);
   int cur_param() const {return m_cur_param;}
protected:
   VertexStage& m_proc;
   std::map<unsigned, unsigned> m_param_map;
   int m_cur_clip_pos;
   int m_cur_param;
   GPRVector m_clip_vertex;
};

class VertexStageExportForFS : public VertexStageExportBase
{
public:
   VertexStageExportForFS(VertexStage& proc,
                          const pipe_stream_output_info *so_info,
                          r600_pipe_shader *pipe_shader,
                          const r600_shader_key& key);

   bool store_deref(const nir_variable *out_var, nir_intrinsic_instr* instr) override;
   void finalize_exports() override;

private:
   bool emit_varying_param(const nir_variable *out_var, nir_intrinsic_instr* instr);
   bool emit_varying_pos(const nir_variable *out_var, nir_intrinsic_instr* instr,
                         std::array<uint32_t, 4> *swizzle_override = nullptr);
   bool emit_clip_vertices(const nir_variable *out_var, nir_intrinsic_instr* instr);
   bool emit_stream(int stream);

   ExportInstruction *m_last_param_export;
   ExportInstruction *m_last_pos_export;

   int m_num_clip_dist;
   int m_enabled_stream_buffers_mask;
   const pipe_stream_output_info *m_so_info;
   r600_pipe_shader *m_pipe_shader;
   const r600_shader_key& m_key;

};

class VertexStageExportForGS : public VertexStageExportBase
{
public:
   VertexStageExportForGS(VertexStage& proc,
                          const r600_shader *gs_shader);
   bool store_deref(const nir_variable *out_var, nir_intrinsic_instr* instr) override;
   void finalize_exports() override;

private:
   unsigned m_num_clip_dist;
   const r600_shader *m_gs_shader;
};

class VertexStageExportForES : public VertexStageExportBase
{
public:
   VertexStageExportForES(VertexStage& proc);
   bool store_deref(const nir_variable *out_var, nir_intrinsic_instr* instr) override;
   void finalize_exports() override;
};


}

#endif // VERTEXSTAGEEXPORT_H
