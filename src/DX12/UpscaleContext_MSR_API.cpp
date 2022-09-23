#include "stdafx.h"
#include <d3dx12.h>
#include <algorithm>
#include "UpscaleContext_MSR_API.h"

UpscaleContext_MSR_API::UpscaleContext_MSR_API(UpscaleType type, std::string name)
	: UpscaleContext(name)
{
}

void UpscaleContext_MSR_API::OnCreate(const FfxUpscaleInitParams& initParams)
{
	UpscaleContext::OnCreate(initParams);
	ID3D12Device* d3dDevice = m_pDevice->GetDevice();
	
	// Root signature setup
	{
		D3D12_ROOT_PARAMETER rootParameters[MSR_ROOT_PARAM_COUNT] = { {} };

		// SRV
		D3D12_DESCRIPTOR_RANGE descRangeSRV = {};
		descRangeSRV.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		descRangeSRV.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descRangeSRV.BaseShaderRegister = 0;
		descRangeSRV.NumDescriptors = ___MSR_RESOURCE_VIEW_GPU_INDEX_SRV_COUNT___;

		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[0].DescriptorTable.pDescriptorRanges = &descRangeSRV;

		// UAV
		D3D12_DESCRIPTOR_RANGE descRangeUAV = {};
		descRangeUAV.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		descRangeUAV.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		descRangeUAV.BaseShaderRegister = 0;
		descRangeUAV.NumDescriptors = ___MSR_RESOURCE_VIEW_GPU_INDEX_UAV_COUNT___;

		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[1].DescriptorTable.pDescriptorRanges = &descRangeUAV;

		// root constants
		rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParameters[2].Constants.Num32BitValues = sizeof(MSRRootConstants) / 4;
		rootParameters[2].Constants.ShaderRegister = 0;

		// static samplers
		const D3D12_STATIC_SAMPLER_DESC pointClampSamplerDescription {
			D3D12_FILTER_MIN_MAG_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			0,
			16,
			D3D12_COMPARISON_FUNC_NEVER,
			D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
			0.f,
			D3D12_FLOAT32_MAX,
			0,
			0,
			D3D12_SHADER_VISIBILITY_ALL,
		};

		const D3D12_STATIC_SAMPLER_DESC linearClampSamplerDescription {
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			0,
			16,
			D3D12_COMPARISON_FUNC_NEVER,
			D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
			0.f,
			D3D12_FLOAT32_MAX,
			1,
			0,
			D3D12_SHADER_VISIBILITY_ALL,
		};

		const size_t samplerCount = MSR_MAX_STATIC_SAMPLERS;
		D3D12_STATIC_SAMPLER_DESC samplerDescriptions[MSR_MAX_STATIC_SAMPLERS];
		samplerDescriptions[0] = pointClampSamplerDescription;
		samplerDescriptions[1] = linearClampSamplerDescription;


		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = 3;
		rootSignatureDesc.pParameters = rootParameters;
		rootSignatureDesc.NumStaticSamplers = samplerCount;
		rootSignatureDesc.pStaticSamplers = samplerDescriptions;

		ID3DBlob* outBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;

		//Query D3D12SerializeRootSignature from d3d12.dll handle
		typedef HRESULT(__stdcall* D3D12SerializeRootSignatureType)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);

		//Do not pass hD3D12 handle to the FreeLibrary function, as GetModuleHandle will not increment refcount
		HMODULE d3d12ModuleHandle = GetModuleHandleW(L"D3D12.dll");
		assert(NULL != d3d12ModuleHandle);

		D3D12SerializeRootSignatureType dx12SerializeRootSignatureType = (D3D12SerializeRootSignatureType)GetProcAddress(d3d12ModuleHandle, "D3D12SerializeRootSignature");
		assert(nullptr != dx12SerializeRootSignatureType);
		dx12SerializeRootSignatureType(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &outBlob, &errorBlob);

		ThrowIfFailed(d3dDevice->CreateRootSignature(0, outBlob->GetBufferPointer(), outBlob->GetBufferSize(), IID_PPV_ARGS(reinterpret_cast<ID3D12RootSignature**>(&m_rootSignature))));
	}

	D3D12_SHADER_BYTECODE csShader;
	CompileShaderFromFile("accumulate_and_upscale_pass.hlsl", nullptr, "mainCS", "-T cs_6_0 -Zi -Od", &csShader);

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
	pipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	pipelineStateDesc.pRootSignature = m_rootSignature;
	pipelineStateDesc.CS = csShader;
	ThrowIfFailed(d3dDevice->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pipelineStates[MSR_PASS_ACCUMULATE_AND_UPSCALE])));

	// cpu side descriptors heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = MSR_DESCRIPTOR_HEAP_SIZE;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_UAV_SRV_CBV_HeapCPU));
		SetName(m_UAV_SRV_CBV_HeapCPU, "MSRHeapCPU");
	}

	// gpu side descriptors heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = MSR_DESCRIPTOR_HEAP_SIZE;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_UAV_SRV_CBV_HeapGPU));
		SetName(m_UAV_SRV_CBV_HeapCPU, "MSRHeapGPU");
	}
}

void UpscaleContext_MSR_API::OnDestroy()
{
	UpscaleContext::OnDestroy();

	for (auto pipelineState : m_pipelineStates)
		pipelineState->Release();
	m_UAV_SRV_CBV_HeapCPU->Release();
	m_UAV_SRV_CBV_HeapGPU->Release();
	m_rootSignature->Release();
	m_internalColor1.resPtr->Release();
	m_internalColor2.resPtr->Release();
}

void UpscaleContext_MSR_API::OnCreateWindowSizeDependentResources(ID3D12Resource* input, ID3D12Resource* output, uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight, bool hdr)
{
	UpscaleContext::OnCreateWindowSizeDependentResources(input, output, renderWidth, renderHeight, displayWidth, displayHeight, hdr);
	ID3D12Device* d3dDevice = m_pDevice->GetDevice();

	CreateResource2DWithView(d3dDevice, &m_internalColor1, displayWidth, displayHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR1_READ, MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR1_WRITE, "MSR_InternalColor1");
	CreateResource2DWithView(d3dDevice, &m_internalColor2, displayWidth, displayHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR2_READ, MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR2_WRITE, "MSR_InternalColor2");
}

void UpscaleContext_MSR_API::CreateResource2DWithView(ID3D12Device* d3dDevice, MSRResource* outResource, uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_STATES initState, MSR_RESOURCE_VIEW_CPU_INDEX heapSrvIndex, MSR_RESOURCE_VIEW_CPU_INDEX heapUavIndex, std::string resourceName)
{
	D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	ThrowIfFailed(d3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		initState,
		nullptr,
		IID_PPV_ARGS(&outResource->resPtr)
	));
	outResource->currState = initState;
	SetName(outResource->resPtr, resourceName);

	// create UAV and SRV for the resource
	CreateCpuSRVForResource(d3dDevice, outResource->resPtr, heapSrvIndex);
	CreateCpuUAVForResource(d3dDevice, outResource->resPtr, heapUavIndex);
}

void UpscaleContext_MSR_API::CreateCpuSRVForResource(ID3D12Device* d3dDevice, ID3D12Resource* resource, MSR_RESOURCE_VIEW_CPU_INDEX cpuHeapIndex)
{
	UINT heapIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	auto resourceDesc = resource->GetDesc();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {}, * pDesc = nullptr;
	if (resourceDesc.Format == DXGI_FORMAT_D32_FLOAT) {
		// cannot create srv for d32 float, so special handling
		// todo: fsr2 like format transform to generalize this process
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = resource->GetDesc().MipLevels;
		pDesc = &srvDesc;
	}
	auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_HeapCPU->GetCPUDescriptorHandleForHeapStart(), cpuHeapIndex, heapIncrementSize);
	d3dDevice->CreateShaderResourceView(resource, pDesc, srvHandle);
}

void UpscaleContext_MSR_API::CreateCpuUAVForResource(ID3D12Device* d3dDevice, ID3D12Resource* resource, MSR_RESOURCE_VIEW_CPU_INDEX cpuHeapIndex)
{
	UINT heapIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto uavHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_HeapCPU->GetCPUDescriptorHandleForHeapStart(), cpuHeapIndex, heapIncrementSize);
	d3dDevice->CreateUnorderedAccessView(resource, nullptr, nullptr, uavHandle);
}

void UpscaleContext_MSR_API::OnDestroyWindowSizeDependentResources()
{
	UpscaleContext::OnDestroyWindowSizeDependentResources();
}

void UpscaleContext_MSR_API::BuildDevUI(UIState* pState)
{
}

void UpscaleContext_MSR_API::GenerateReactiveMask(ID3D12GraphicsCommandList* pCommandList, const FfxUpscaleSetup& cameraSetup, UIState* pState)
{
}



void UpscaleContext_MSR_API::DispatchAccumulateAndUpscalePass(ID3D12GraphicsCommandList* pCommandList, ID3D12Device* d3dDevice, UIState* pState)
{
	waitBarrier(pCommandList, m_isOddFrame ? &m_internalColor1 : &m_internalColor2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	waitBarrier(pCommandList, m_isOddFrame ? &m_internalColor2 : &m_internalColor1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	ID3D12DescriptorHeap* descHeaps[] = { m_UAV_SRV_CBV_HeapGPU };
	pCommandList->SetDescriptorHeaps(_countof(descHeaps), descHeaps);

	pCommandList->SetComputeRootSignature(m_rootSignature);

	pCommandList->SetPipelineState(m_pipelineStates[MSR_PASS_ACCUMULATE_AND_UPSCALE]);

	CreateCpuUAVForResource(d3dDevice, m_outputColor, MSR_RESOURCE_VIEW_CPU_OUTPUT_COLOR);
	CreateCpuSRVForResource(d3dDevice, m_inputColor, MSR_RESOURCE_VIEW_CPU_INPUT_COLOR);
	CreateCpuSRVForResource(d3dDevice, m_inputDepth, MSR_RESOURCE_VIEW_CPU_INPUT_DEPTH);
	CreateCpuSRVForResource(d3dDevice, m_inputMotion, MSR_RESOURCE_VIEW_CPU_INPUT_MOTION);

	CopyDescriptorsFromCpuToGpu();

	// Reference the updated descriptor from GPU in the command list
	UINT heapIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	pCommandList->SetComputeRootDescriptorTable(0, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_HeapGPU->GetGPUDescriptorHandleForHeapStart(), ___MSR_RESOURCE_VIEW_GPU_INDEX_SRV_START___, heapIncrementSize));
	pCommandList->SetComputeRootDescriptorTable(1, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_HeapGPU->GetGPUDescriptorHandleForHeapStart(), ___MSR_RESOURCE_VIEW_GPU_INDEX_UAV_START___, heapIncrementSize));

	assert(sizeof(MSRRootConstants) % 4 == 0);
	pCommandList->SetComputeRoot32BitConstants(2, sizeof(MSRRootConstants) / 4, reinterpret_cast<void*>(&m_rootConstants), 0);

	//// Dispatch command
	pCommandList->Dispatch((pState->displayWidth + 7) / 8, (pState->displayHeight + 7) / 8, 1);
}

void UpscaleContext_MSR_API::Draw(ID3D12GraphicsCommandList* pCommandList, const FfxUpscaleSetup& cameraSetup, UIState* pState)
{
	ID3D12Device* d3dDevice = m_pDevice->GetDevice();

	m_inputColor = cameraSetup.unresolvedColorResource;
	m_inputDepth = cameraSetup.depthbufferResource;
	m_inputMotion = cameraSetup.motionvectorResource;
	m_outputColor = cameraSetup.resolvedColorResource;

	m_rootConstants.upscaleFactor = std::max<float>(float(pState->displayWidth) / pState->renderWidth, float(pState->displayHeight) / pState->renderWidth);
	m_rootConstants.jitter[0] = m_JitterX;
	m_rootConstants.jitter[1] = m_JitterY;
	m_rootConstants.renderSize[0] = pState->renderWidth;
	m_rootConstants.renderSize[1] = pState->renderHeight;
	m_rootConstants.displaySize[0] = pState->displayWidth;
	m_rootConstants.displaySize[1] = pState->displayHeight;
	m_rootConstants.isOddFrame = static_cast<uint32_t>(m_isOddFrame);
	
	DispatchAccumulateAndUpscalePass(pCommandList, d3dDevice, pState);

	m_isOddFrame = !m_isOddFrame;
}

void UpscaleContext_MSR_API::ReloadPipelines()
{
	m_pDevice->GPUFlush();
	OnDestroyWindowSizeDependentResources();
	OnCreateWindowSizeDependentResources(m_input, m_output, m_renderWidth, m_renderHeight, m_displayWidth, m_displayHeight, m_hdr);
}

void UpscaleContext_MSR_API::CopyDescriptorsFromCpuToGpu()
{
	ID3D12Device* d3dDevice = m_pDevice->GetDevice();

	UINT heapIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto CopySingle = [&](MSR_RESOURCE_VIEW_GPU_INDEX gpuTo, MSR_RESOURCE_VIEW_CPU_INDEX cpuFrom) {
		auto gpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_HeapGPU->GetCPUDescriptorHandleForHeapStart(), gpuTo, heapIncrementSize);
		auto cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_HeapCPU->GetCPUDescriptorHandleForHeapStart(), cpuFrom, heapIncrementSize);
		d3dDevice->CopyDescriptorsSimple(1, gpuHandle, cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	};

	CopySingle(MSR_RESOURCE_VIEW_GPU_INPUT_COLOR, MSR_RESOURCE_VIEW_CPU_INPUT_COLOR);
	CopySingle(MSR_RESOURCE_VIEW_GPU_INPUT_DEPTH, MSR_RESOURCE_VIEW_CPU_INPUT_DEPTH);
	CopySingle(MSR_RESOURCE_VIEW_GPU_INPUT_MOTION, MSR_RESOURCE_VIEW_CPU_INPUT_MOTION);
	CopySingle(MSR_RESOURCE_VIEW_GPU_OUTPUT_COLOR, MSR_RESOURCE_VIEW_CPU_OUTPUT_COLOR);

	// resources that need to swap every 
	CopySingle(MSR_RESOURCE_VIEW_GPU_INTERNAL_COLOR_READ, m_isOddFrame ? MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR1_READ : MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR2_READ);
	CopySingle(MSR_RESOURCE_VIEW_GPU_INTERNAL_COLOR_WRITE, m_isOddFrame ? MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR2_WRITE : MSR_RESOURCE_VIEW_CPU_INTERNAL_COLOR1_WRITE);
}

// besides adding transition barrier, also recording resource's new state
void UpscaleContext_MSR_API::waitBarrier(ID3D12GraphicsCommandList* pCommandList, MSRResource* inOutResource, D3D12_RESOURCE_STATES toState)
{
	D3D12_RESOURCE_STATES fromState = inOutResource->currState;
	if (fromState == toState)
		return;

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(inOutResource->resPtr, fromState, toState);
	pCommandList->ResourceBarrier(1, &barrier);
	inOutResource->currState = toState;
}
