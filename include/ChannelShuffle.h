#pragma once
#include "Layer.h"

namespace dnn
{
	class ChannelShuffle final : public Layer
	{
	private:
		std::unique_ptr<dnnl::shuffle_forward::primitive_desc> fwdDesc;
		std::unique_ptr<dnnl::shuffle_backward::primitive_desc> bwdDesc;
		std::unique_ptr<dnnl::shuffle_forward> fwd;
		std::unique_ptr<dnnl::shuffle_backward> bwd;

	public:
	    const size_t Groups;
		const size_t GroupSize;

		ChannelShuffle(const dnn::Device& device, const dnnl::memory::format_tag format, const std::string& name, const std::vector<Layer*>& inputs, const size_t groups) :
			Layer(device, format, name, LayerTypes::ChannelShuffle, 0, 0, inputs[0]->C, inputs[0]->D, inputs[0]->H, inputs[0]->W, 0, 0, 0, inputs),
			Groups(groups),
			GroupSize(inputs[0]->C / groups)
		{
			assert(Inputs.size() == 1);

			assert(Groups > 0 && Groups <= C);
		}

		std::string GetDescription() const final override
		{
			std::string description = GetDescriptionHeader();

			description.append(nwl + std::string(" Groups:") + tab + std::to_string(Groups));
			description.append(nwl + std::string(" GroupSize:") + tab + std::to_string(GroupSize));
			description.append(nwl + std::string(" Connections:") + tab + std::to_string(InputLayer->C / Groups));

			return description;
		}

		size_t FanIn() const final override
		{
			return 1;
		}

		size_t FanOut() const final override
		{
			return 1;
		}

		void InitializeDescriptors(const size_t batchSize) final override
		{
			if (InputLayer->DstMemDesc->data.ndims == 2)
			{
				if (Format == dnnl::memory::format_tag::any)
					Format = dnnl::memory::format_tag::nc;

				DstMemDesc = std::make_unique<dnnl::memory::desc>(dnnl::memory::desc(dnnl::memory::dims({ dnnl::memory::dim(batchSize), dnnl::memory::dim(C) }), dnnl::memory::data_type::f32, Format));
				DiffDstMemDesc = std::make_unique<dnnl::memory::desc>(dnnl::memory::desc(dnnl::memory::dims({ dnnl::memory::dim(batchSize), dnnl::memory::dim(C) }), dnnl::memory::data_type::f32, Format));
			}
			else
			{
				if (Format == dnnl::memory::format_tag::any)
				{
					Format = GetDataFmt(*InputLayer->DstMemDesc);
					if (Format != GetDataFmt(*InputLayer->DiffDstMemDesc))
						throw std::invalid_argument("Src and Diff format are different in " + std::string(magic_enum::enum_name<LayerTypes>(LayerType)) + " layer " + Name);
				}

				DstMemDesc = std::make_unique<dnnl::memory::desc>(dnnl::memory::desc(dnnl::memory::dims({ dnnl::memory::dim(batchSize), dnnl::memory::dim(C), dnnl::memory::dim(H), dnnl::memory::dim(W) }), dnnl::memory::data_type::f32, Format));
				DiffDstMemDesc = std::make_unique<dnnl::memory::desc>(dnnl::memory::desc(dnnl::memory::dims({ dnnl::memory::dim(batchSize), dnnl::memory::dim(C), dnnl::memory::dim(H), dnnl::memory::dim(W) }), dnnl::memory::data_type::f32, Format));
			}

			fwdDesc = std::make_unique<dnnl::shuffle_forward::primitive_desc>(dnnl::shuffle_forward::primitive_desc(dnnl::shuffle_forward::desc(dnnl::prop_kind::forward_training, *DstMemDesc, 1, int(GroupSize)), Device.first));
			bwdDesc = std::make_unique<dnnl::shuffle_backward::primitive_desc>(dnnl::shuffle_backward::primitive_desc(dnnl::shuffle_backward::desc(*DiffDstMemDesc, 1, int(GroupSize)), Device.first, *fwdDesc));

			fwd = std::make_unique<dnnl::shuffle_forward>(dnnl::shuffle_forward(*fwdDesc));
			bwd = std::make_unique<dnnl::shuffle_backward>(dnnl::shuffle_backward(*bwdDesc));
		}

		void ForwardProp(const size_t batchSize, const bool training) final override
		{
			auto srcMem = dnnl::memory(*InputLayer->DstMemDesc, Device.first, InputLayer->Neurons.data());
			auto dstMem = dnnl::memory(*DstMemDesc, Device.first, Neurons.data());

			fwd->execute(Device.second, std::unordered_map<int, dnnl::memory>{ {DNNL_ARG_SRC, srcMem}, { DNNL_ARG_DST, dstMem } });
			Device.second.wait();

#ifndef DNN_LEAN
			if (training)
				ZeroFloatVector(NeuronsD1.data(), batchSize * PaddedCDHW);
#else
			DNN_UNREF_PAR(batchSize);
#endif // DNN_LEAN
		}

		void BackwardProp(const size_t batchSize) final override
		{
#ifdef DNN_LEAN
			ZeroGradient(batchSize);
#else
			DNN_UNREF_PAR(batchSize);
#endif // DNN_LEAN

			auto diffDstMem = dnnl::memory(*DiffDstMemDesc, Device.first, NeuronsD1.data());
			auto diffSrcMem = dnnl::memory(*InputLayer->DiffDstMemDesc, Device.first, InputLayer->NeuronsD1.data());

			bwd->execute(Device.second, std::unordered_map<int, dnnl::memory>{ {DNNL_ARG_DIFF_DST, diffDstMem}, { DNNL_ARG_DIFF_SRC, diffSrcMem } });
			Device.second.wait();

#ifdef DNN_LEAN
			ReleaseGradient();
#endif // DNN_LEAN
		}
	};
}
