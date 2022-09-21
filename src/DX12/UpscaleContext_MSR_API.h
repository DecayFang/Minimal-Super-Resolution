#pragma once

#include "Base/Texture.h"
#include "Base/StaticBufferPool.h"
#include "Base/GBuffer.h"
#include "PostProc/PostProcCS.h"
#include "UI.h"

#include "UpscaleContext.h"

#include "../ffx-fsr2-api/ffx_fsr2.h"

using namespace CAULDRON_DX12;

constexpr size_t MSR_ROOT_PARAM_COUNT = 10;

enum MSR_PASS {
	MSR_PASS_ACCUMULATE_AND_UPSCALE = 0,

	MSR_PASS_COUNT
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

	float memoryUsageInMegabytes = 0;
};
