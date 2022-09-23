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
	uint32_t isOddFrame;
};

constexpr size_t MSR_ROOT_PARAM_COUNT = 10;
constexpr size_t MSR_MAX_STATIC_SAMPLERS = 2;
constexpr size_t MSR_TEXTURE_RESOURCE_MAX = 50;

enum MSR_PASS {
	MSR_PASS_ACCUMULATE_AND_UPSCALE = 0,

	MSR_PASS_COUNT
};

enum MSR_DESCRIPTOR_HEAP_INDEX {
	MSR_DESCRIPTOR_HEAP_SRV_START = 0,
	MSR_DESCRIPTOR_HEAP_SRV_COUNT = 40,

	MSR_DESCRIPTOR_HEAP_UAV_START = MSR_DESCRIPTOR_HEAP_SRV_START + MSR_DESCRIPTOR_HEAP_SRV_COUNT,
	MSR_DESCRIPTOR_HEAP_UAV_COUNT = 40,

	MSR_DESCRIPTOR_HEAP_SIZE = MSR_DESCRIPTOR_HEAP_SRV_COUNT + MSR_DESCRIPTOR_HEAP_UAV_COUNT
};

enum MSR_RESOURCE_VIEW_CPU_INDEX {
	// SRVs
	MSR_RESOURCE_VIEW_CPU_INPUT_COLOR = MSR_DESCRIPTOR_HEAP_SRV_START,
	MSR_RESOURCE_VIEW_CPU_INPUT_DEPTH,
	MSR_RESOURCE_VIEW_CPU_INPUT_MOTION,
	MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR1_READ,
	MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR2_READ,

	// UAVs
	MSR_RESOURCE_VIEW_CPU_OUTPUT_COLOR = MSR_DESCRIPTOR_HEAP_UAV_START,
	MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR1_WRITE,
	MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR2_WRITE,

};

enum MSR_RESOURCE_VIEW_GPU_INDEX {
	___MSR_RESOURCE_VIEW_GPU_INDEX_SRV_START___ = MSR_DESCRIPTOR_HEAP_SRV_START,

	// SRVs
	MSR_RESOURCE_VIEW_GPU_INPUT_COLOR = MSR_DESCRIPTOR_HEAP_SRV_START,
	MSR_RESOURCE_VIEW_GPU_INPUT_DEPTH,
	MSR_RESOURCE_VIEW_GPU_INPUT_MOTION,
	MSR_RESOURCE_VIEW_GPU_INTERNAL_COLOR_READ,

	___MSR_RESOURCE_VIEW_GPU_INDEX_SRV_END___,


	___MSR_RESOURCE_VIEW_GPU_INDEX_UAV_START___ = MSR_DESCRIPTOR_HEAP_UAV_START,

	// UAVs
	MSR_RESOURCE_VIEW_GPU_OUTPUT_COLOR = MSR_DESCRIPTOR_HEAP_UAV_START,
	MSR_RESOURCE_VIEW_GPU_INTERNAL_COLOR_WRITE,

	___MSR_RESOURCE_VIEW_GPU_INDEX_UAV_END___,

	// resource view count
	___MSR_RESOURCE_VIEW_GPU_INDEX_SRV_COUNT___ = ___MSR_RESOURCE_VIEW_GPU_INDEX_SRV_END___ - ___MSR_RESOURCE_VIEW_GPU_INDEX_SRV_START___,
	___MSR_RESOURCE_VIEW_GPU_INDEX_UAV_COUNT___ = ___MSR_RESOURCE_VIEW_GPU_INDEX_UAV_END___ - ___MSR_RESOURCE_VIEW_GPU_INDEX_UAV_START___,
};

struct MSRResource {
	ID3D12Resource* resPtr;
	D3D12_RESOURCE_STATES currState;
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
	void CreateResource2DWithView(ID3D12Device* d3dDevice, MSRResource* outResource, uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_STATES initState, MSR_RESOURCE_VIEW_CPU_INDEX heapSrvIndex, MSR_RESOURCE_VIEW_CPU_INDEX heapUavIndex, std::string resourceName);
	void CreateCpuSRVForResource(ID3D12Device* d3dDevice, ID3D12Resource* resource, MSR_RESOURCE_VIEW_CPU_INDEX cpuHeapIndex);
	void CreateCpuUAVForResource(ID3D12Device* d3dDevice, ID3D12Resource* resource, MSR_RESOURCE_VIEW_CPU_INDEX cpuHeapIndex);
	virtual void                OnDestroyWindowSizeDependentResources();
	virtual void                BuildDevUI(UIState* pState) override;
	virtual void                GenerateReactiveMask(ID3D12GraphicsCommandList* pCommandList, const FfxUpscaleSetup& cameraSetup, UIState* pState);
	void DispatchAccumulateAndUpscalePass(ID3D12GraphicsCommandList* pCommandList, ID3D12Device* d3dDevice, UIState* pState);
	virtual void                Draw(ID3D12GraphicsCommandList* pCommandList, const FfxUpscaleSetup& cameraSetup, UIState* pState);

private:
	void ReloadPipelines();
	void CopyDescriptorsFromCpuToGpu();
	void waitBarrier(ID3D12GraphicsCommandList* pCommandList, MSRResource* inOutResource, D3D12_RESOURCE_STATES toState);

	ID3D12PipelineState* m_pipelineStates[MSR_PASS_COUNT];
	ID3D12DescriptorHeap* m_UAV_SRV_CBV_HeapGPU;
	ID3D12DescriptorHeap* m_UAV_SRV_CBV_HeapCPU;
	ID3D12RootSignature* m_rootSignature;
	size_t m_currResourceIndex = 0;
	MSRRootConstants m_rootConstants;

	// input & output resouces for convenience pass reference
	ID3D12Resource* m_inputColor;
	ID3D12Resource* m_inputDepth;
	ID3D12Resource* m_inputMotion;
	ID3D12Resource* m_outputColor;

	// internal resources keep tracked one by one so that no complex bookkeeping needed.
	MSRResource m_internalColor1;
	MSRResource m_internalColor2;

	bool m_isOddFrame = false;

	float memoryUsageInMegabytes = 0;
};
