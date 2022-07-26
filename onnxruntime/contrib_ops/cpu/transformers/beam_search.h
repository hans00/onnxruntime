// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <memory>
#include "core/common/common.h"
#include "core/framework/op_kernel.h"
#include "core/providers/cpu/controlflow/utils.h"
#include "contrib_ops/cpu/transformers/beam_search_parameters.h"
#include "contrib_ops/cpu/transformers/subgraph_gpt.h"
#include "contrib_ops/cpu/transformers/subgraph_t5_encoder.h"
#include "contrib_ops/cpu/transformers/subgraph_t5_decoder.h"
#include "contrib_ops/cpu/transformers/generation_device_helper.h"

namespace onnxruntime {
class FeedsFetchesManager;

namespace contrib {
namespace transformers {

using namespace onnxruntime::controlflow;  // namespace of IControlFlowKernel

class BeamSearch : public IControlFlowKernel {
 public:
  BeamSearch(const OpKernelInfo& info)
      : IControlFlowKernel(info),
        encoder_feeds_fetches_manager_(nullptr),
        decoder_feeds_fetches_manager_(nullptr),
        ort_stream_(nullptr),
        dumper_(nullptr) {
    Init(info);
  }

  void Init(const OpKernelInfo& info);

  Status Compute(OpKernelContext* ctx) const override;

  Status SetupSubgraphExecutionInfo(const SessionState& session_state,
                                    const std::string& attribute_name,
                                    const SessionState& subgraph_session_state) override;

 protected:
  void SetComputeStream(Stream* stream) { ort_stream_ = stream; }
  void SetConsoleDumper(IConsoleDumper* dumper) { dumper_ = dumper; }

  // device helpers that is same for both GPT and encoder-decoder models.
  void SetDeviceHelpers(
      const GenerationDeviceHelper::AddToFeedsFunc& add_to_feeds_func,
      const GenerationDeviceHelper::TopkFunc& topk_func,
      const GenerationDeviceHelper::DeviceCopyFunc<float>& device_copy_func,
      const GenerationDeviceHelper::DeviceCopyFunc<int32_t>& device_copy_int32_func,
      const GenerationDeviceHelper::ProcessLogitsFunc<float>& process_logits_func,
      const GenerationDeviceHelper::ProcessLogitsFunc<MLFloat16>& process_logits_fp16_func,
      const GenerationDeviceHelper::InitBeamStateFunc<float>& init_beam_state_func,
      const GenerationDeviceHelper::InitBeamStateFunc<MLFloat16>& init_beam_state_fp16_func) {
    add_to_feeds_func_ = add_to_feeds_func;
    topk_func_ = topk_func;
    device_copy_func_ = device_copy_func;
    device_copy_int32_func_ = device_copy_int32_func;
    process_logits_func_ = process_logits_func;
    process_logits_fp16_func_ = process_logits_fp16_func;
    init_beam_state_func_ = init_beam_state_func;
    init_beam_state_fp16_func_ = init_beam_state_fp16_func;
  }

  void SetDeviceHelpers_Gpt(
      const GenerationDeviceHelper::UpdateGptFeedsFunc<float>& update_gpt_feeds_func,
      const GenerationDeviceHelper::UpdateGptFeedsFunc<MLFloat16>& update_gpt_feeds_fp16_func) {
    update_gpt_feeds_func_ = update_gpt_feeds_func;
    update_gpt_feeds_fp16_func_ = update_gpt_feeds_fp16_func;
  }

  // device helpers for encoder-decoder model like T5
  void SetDeviceHelpers_EncoderDecoder(
      const GenerationDeviceHelper::UpdateDecoderFeedsFunc<float>& update_decoder_feeds_func,
      const GenerationDeviceHelper::UpdateDecoderFeedsFunc<MLFloat16>& update_decoder_feeds_fp16_func,
      const GenerationDeviceHelper::ExpandBufferFunc<int32_t>& expand_buffer_int32_func,
      const GenerationDeviceHelper::ExpandBufferFunc<float>& expand_buffer_float_func,
      const GenerationDeviceHelper::ExpandBufferFunc<MLFloat16>& expand_buffer_float16_func) {
    update_decoder_feeds_func_ = update_decoder_feeds_func;
    update_decoder_feeds_fp16_func_ = update_decoder_feeds_fp16_func;
    expand_buffer_int32_func_ = expand_buffer_int32_func;
    expand_buffer_float_func_ = expand_buffer_float_func;
    expand_buffer_float16_func_ = expand_buffer_float16_func;
  }

 private:
  // Device specific functions
  GenerationDeviceHelper::AddToFeedsFunc add_to_feeds_func_;
  GenerationDeviceHelper::TopkFunc topk_func_;
  GenerationDeviceHelper::DeviceCopyFunc<float> device_copy_func_;
  GenerationDeviceHelper::DeviceCopyFunc<int32_t> device_copy_int32_func_;

  GenerationDeviceHelper::ProcessLogitsFunc<float> process_logits_func_;
  GenerationDeviceHelper::ProcessLogitsFunc<MLFloat16> process_logits_fp16_func_;

  GenerationDeviceHelper::InitBeamStateFunc<float> init_beam_state_func_;
  GenerationDeviceHelper::InitBeamStateFunc<MLFloat16> init_beam_state_fp16_func_;

  //------------------------------------------------------------
  // Device specific functions for GPT
  //------------------------------------------------------------
  GenerationDeviceHelper::UpdateGptFeedsFunc<float> update_gpt_feeds_func_;
  GenerationDeviceHelper::UpdateGptFeedsFunc<MLFloat16> update_gpt_feeds_fp16_func_;

  //------------------------------------------------------------
  // Device specific functions for encoder-decoder model like T5
  //------------------------------------------------------------
  GenerationDeviceHelper::CreateEncoderInputsFunc create_encoder_inputs_func_;

  GenerationDeviceHelper::UpdateDecoderFeedsFunc<float> update_decoder_feeds_func_;
  GenerationDeviceHelper::UpdateDecoderFeedsFunc<MLFloat16> update_decoder_feeds_fp16_func_;

  GenerationDeviceHelper::ExpandBufferFunc<int32_t> expand_buffer_int32_func_;
  GenerationDeviceHelper::ExpandBufferFunc<float> expand_buffer_float_func_;
  GenerationDeviceHelper::ExpandBufferFunc<MLFloat16> expand_buffer_float16_func_;

  //------------------------------------------------------------
  // Subgraph and FeedsFetchesManager re-used for each subgraph execution.
  //------------------------------------------------------------
  std::unique_ptr<GptSubgraph> gpt_subgraph_;
  std::unique_ptr<T5EncoderSubgraph> t5_encoder_subgraph_;
  std::unique_ptr<T5DecoderSubgraph> t5_decoder_subgraph_;
  FeedsFetchesManager* encoder_feeds_fetches_manager_;
  FeedsFetchesManager* decoder_feeds_fetches_manager_;

  Stream* ort_stream_;

  IConsoleDumper* dumper_;

  BeamSearchParameters parameters_;
};

}  // namespace transformers
}  // namespace contrib
}  // namespace onnxruntime