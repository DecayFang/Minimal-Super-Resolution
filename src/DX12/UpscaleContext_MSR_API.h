#pragma once

#include "Base/Texture.h"
#include "Base/StaticBufferPool.h"
#include "Base/GBuffer.h"
#include "PostProc/PostProcCS.h"
#include "UI.h"

#include "UpscaleContext.h"

#include "../ffx-fsr2-api/ffx_fsr2.h"

using namespace CAULDRON_DX12;

struct MSRRootConstants {
	float jitter[2];
	uint32_t renderSize[2];
	uint32_t displaySize[2];
	float upscaleFactor;
};

constexpr size_t MSR_ROOT_PARAM_COUNT = 10;
constexpr size_t MSR_MAX_STATIC_SAMPLERS = 2;

enum MSR_PASS {
	MSR_PASS_ACCUMULATE_AND_UPSCALE = 0,

	MSR_PASS_COUNT
};

enum MSR_DESCRIPTOR_HEAP_INDEX {
	MSR_DESCRIPTOR_HEAP_SRV_START = 0,
	MSR_DESCRIPTOR_HEAP_SRV_COUNT = 10,

	MSR_DESCRIPTOR_HEAP_UAV_START = MSR_DESCRIPTOR_HEAP_SRV_START + MSR_DESCRIPTOR_HEAP_SRV_COUNT,
	MSR_DESCRIPTOR_HEAP_UAV_COUNT = 10,

	MSR_DESCRIPTOR_HEAP_SIZE = MSR_DESCRIPTOR_HEAP_SRV_COUNT + MSR_DESCRIPTOR_HEAP_UAV_COUNT
};

enum MSR_RESOURCE_VIEW_INDEX {
	// SRVs
	MSR_RESOURCE_VIEW_INPUT_COLOR = MSR_DESCRIPTOR_HEAP_SRV_START,
	MSR_RESOURCE_VIEW_INPUT_DEPTH,
	MSR_RESOURCE_VIEW_INPUT_MOTION,
	MSR_RESOURCE_VIEW_INTERNEL_COLOR_READ,

	// UAVs
	MSR_RESOURCE_VIEW_OUTPUT_COLOR = MSR_DESCRIPTOR_HEAP_UAV_START,
	MSR_RESOURCE_VIEW_INTERNEL_COLOR_WRITE,

};

class UpscaleContext_MSR_API : public UpscaleContext
{
public:
	UpscaleContext_MSR_API(UpscaleType type, std::string name);

	virtual std::string         Name() { return "Minimal Supersampling Resolution API"; }
	virtual void                OnCreate(const FfxUpscaleInitParams& initParams);
	virtual void                OnDestroy();
	virtual void                OnCreateWindowSizeDependentResources(
		ID3D12Resource* input,
		ID3D12Resource* output,
		uint32_t renderWidth,
		uint32_t renderHeight,
		uint32_t displayWidth,
		uint32_t displayHeight,
		bool hdr);
	virtual void                OnDestroyWindowSizeDependentResources();
	virtual void                BuildDevUI(UIState* pState) override;
	virtual void                GenerateReactiveMask(ID3D12GraphicsCommandList* pCommandList, const FfxUpscaleSetup& cameraSetup, UIState* pState);
	virtual void                Draw(ID3D12GraphicsCommandList* pCommandList, const FfxUpscaleSetup& cameraSetup, UIState* pState);

private:
	void ReloadPipelines();

	ID3D12PipelineState* m_pipelineStates[MSR_PASS_COUNT];
	ID3D12DescriptorHeap* m_UAV_SRV_CBV_Heap;
	ID3D12RootSignature* m_rootSignature;
	MSRRootConstants m_rootConstants;

	float memoryUsageInMegabytes = 0;
};
