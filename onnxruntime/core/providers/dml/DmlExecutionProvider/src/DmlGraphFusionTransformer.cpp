#pragma once

#include "precomp.h"
#include "GraphDescBuilder.h"
#include "ExecutionProvider.h"
#include "DmlGraphFusionTransformer.h"
#include "GraphPartitioner.h"
#include "core/framework/kernel_type_str_resolver.h"
#include "core/framework/kernel_lookup.h"
#include "FusedGraphKernel.h"
#include "MLOperatorAuthorImpl.h"
#include "GraphKernelHelper.h"
#include "DmlGraphFusionHelper.h"


namespace Dml
{
    DmlGraphFusionTransformer::DmlGraphFusionTransformer(
        const std::string& name,
        const onnxruntime::IExecutionProvider* provider
    )
        :onnxruntime::GraphTransformer(name),
         m_providerImpl(static_cast<const ExecutionProvider*>(provider)->GetImpl())
    {
    }
	
    onnxruntime::common::Status DmlGraphFusionTransformer::ApplyImpl(
        onnxruntime::Graph& graph,
        bool& modified,
        int graph_level,
        const onnxruntime::logging::Logger& logger) const
    {
        onnxruntime::ProviderType provider_type = onnxruntime::kDmlExecutionProvider;
        const gsl::not_null<const onnxruntime::KernelRegistry*> registry = m_providerImpl->GetKernelRegistry().get();
        const auto kernel_type_str_resolver = onnxruntime::OpSchemaKernelTypeStrResolver{};
        const auto kernel_lookup = onnxruntime::KernelLookup{provider_type,
                                                             gsl::make_span(&registry, 1),
                                                             kernel_type_str_resolver};

        // Initializers needed by any graph partition
        std::unordered_set<std::string> requiredInitializerMap;
        std::unordered_map<const onnxruntime::Node*, GraphNodeProperties> graphNodePropertyMap;
        onnxruntime::GraphViewer graphViewer(graph);
        std::vector<std::unique_ptr<GraphPartition>> partitions = BuildPartitions(
            graphViewer,
            *m_providerImpl->GetInternalRegistrationInfoMap(), 
            kernel_lookup,
            m_providerImpl->GetSupportedDeviceDataTypeMask(),
            graphNodePropertyMap, 
            requiredInitializerMap);

        // Create a map between each initialized tensor and the partition(s) it is part of.
        auto initializerPartitionMap = DmlGraphFusionHelper::GetInitializerToPartitionMap(graphViewer, partitions);

        for (uint32_t partitionIndex = 0; partitionIndex < partitions.size(); ++partitionIndex)
        {
            auto& partition = partitions[partitionIndex];

            if (partition->GetRootMergedPartition() != partition.get() ||
                !partition->IsDmlPartition())
            {
                continue;
            }

            // Create a map which will store by name each initializer which should be transferred to the 
            // partition.  This prevents OnnxRuntime from allocating GPU resources and uploading those initializers,
            // so the partiton's kernel can do so.  In the process, it will pre-process weights while consuming a CPU
            // backed resource, avoiding an extra set of GPU resources in memory.
            // A shared pointer is used so the functor and contained initializer captures can be cheaply copied within ORT.
            //auto transferredInitializerMap = std::make_shared<std::unordered_map<std::string, onnx::TensorProto>>();
            std::unordered_map<std::string, std::pair<const ONNX_NAMESPACE::TensorProto*, bool>> isInitializerTransferable;

            
            if (partition->IsDmlGraphPartition())
            {
                // populate transferredInitializerMap
                for (const auto& input : partition->GetInputs())
                {
                    const onnx::TensorProto* tensor = nullptr;
                    if (graph.GetInitializedTensor(input, tensor))
                    {
                        // It's only safe to transfer tensors which are used by this partition alone.
                        auto iter = initializerPartitionMap.find(tensor);
                        assert(iter != initializerPartitionMap.end());
                        if (iter->second.size() > 1)
                        {
                            if (requiredInitializerMap.find(input) != requiredInitializerMap.end())
                            {
                                // The kernel relies on this input to be initialized, and it should be small enough to copy
                                // cheaply. FusedGraphKernel only handles constant CPU inputs through transferred initializers,
                                // rather than ORT, to avoid mismatches in policy or implementation causing failures.
                                //(*transferredInitializerMap)[input] = *tensor;
                                isInitializerTransferable[input] = {tensor, false};
                            }

                            continue;
                        }
                        //ORT_RETURN_IF_ERROR(graph.ExtractInitializedTensor(tensor->name(), (*transferredInitializerMap)[input]));
                        isInitializerTransferable[input] = {tensor, true};
                    }
                }

                std::string partitionKernelPrefix = std::to_string(m_providerImpl->GetPartitionKernelPrefixVal()) + "_";
                m_providerImpl->IncreasePartitionKernelPrefixVal();

                DmlGraphFusionHelper::FusePartitionAndRegisterKernel(
                    partition.get(), 
                    partitionIndex, 
                    graph, 
                    graphNodePropertyMap,
                    m_providerImpl->GetKernelRegistry().get(),
                    partitionKernelPrefix,
                    isInitializerTransferable,
                    m_providerImpl
                );
            }
        }

        return onnxruntime::common::Status::OK();
    }
}
