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

		// UAV
		D3D12_DESCRIPTOR_RANGE descRangeUAV = {};
		descRangeUAV.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		descRangeUAV.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		descRangeUAV.BaseShaderRegister = 0;
		descRangeUAV.NumDescriptors = 1;

		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[0].DescriptorTable.pDescriptorRanges = &descRangeUAV;

		// SRV
		D3D12_DESCRIPTOR_RANGE descRangeSRV = {};
		descRangeSRV.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		descRangeSRV.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descRangeSRV.BaseShaderRegister = 0;
		descRangeSRV.NumDescriptors = 3;

		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[1].DescriptorTable.pDescriptorRanges = &descRangeSRV;

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

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = MSR_DESCRIPTOR_HEAP_SIZE;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	
	d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_UAV_SRV_CBV_Heap));
}

void UpscaleContext_MSR_API::OnDestroy()
{
	UpscaleContext::OnDestroy();

	for (auto pipelineState : m_pipelineStates)
		pipelineState->Release();
	m_UAV_SRV_CBV_Heap->Release();
	m_rootSignature->Release();
}

void UpscaleContext_MSR_API::OnCreateWindowSizeDependentResources(ID3D12Resource* input, ID3D12Resource* output, uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight, bool hdr)
{
	UpscaleContext::OnCreateWindowSizeDependentResources(input, output, renderWidth, renderHeight, displayWidth, displayHeight, hdr);
	ID3D12Device* d3dDevice = m_pDevice->GetDevice();

	ID3D12Resource* resource;
	D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, displayWidth, displayHeight);
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	ThrowIfFailed(d3dDevice->CreateCommittedResource(
		&heapProperties, 
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&resource)
	));

	// create UAV and SRV for the resource
	size_t heapIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetCPUDescriptorHandleForHeapStart(), MSR_RESOURCE_VIEW_INTERNEL_COLOR_READ, heapIncrementSize);
	d3dDevice->CreateShaderResourceView(resource, nullptr, srvHandle);

	auto uavHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetCPUDescriptorHandleForHeapStart(), MSR_RESOURCE_VIEW_INTERNEL_COLOR_WRITE, heapIncrementSize);
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

void UpscaleContext_MSR_API::Draw(ID3D12GraphicsCommandList* pCommandList, const FfxUpscaleSetup& cameraSetup, UIState* pState)
{
	ID3D12Device* d3dDevice = m_pDevice->GetDevice();

	ID3D12Resource* inputColor = cameraSetup.unresolvedColorResource;
	ID3D12Resource* inputDepth = cameraSetup.depthbufferResource;
	ID3D12Resource* inputMotion = cameraSetup.motionvectorResource;
	ID3D12Resource* output = cameraSetup.resolvedColorResource;

	m_rootConstants.upscaleFactor = std::max<float>(float(pState->displayWidth) / pState->renderWidth, float(pState->displayHeight) / pState->renderWidth);
	m_rootConstants.jitter[0] = m_JitterX;
	m_rootConstants.jitter[1] = m_JitterY;
	m_rootConstants.renderSize[0] = pState->renderWidth;
	m_rootConstants.renderSize[1] = pState->renderHeight;
	m_rootConstants.displaySize[0] = pState->displayWidth;
	m_rootConstants.displaySize[1] = pState->displayHeight;
	
	ID3D12DescriptorHeap* descHeaps[] = { m_UAV_SRV_CBV_Heap };
	pCommandList->SetDescriptorHeaps(_countof(descHeaps), descHeaps);

	pCommandList->SetComputeRootSignature(m_rootSignature);

	pCommandList->SetPipelineState(m_pipelineStates[MSR_PASS_ACCUMULATE_AND_UPSCALE]);

	size_t heapIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	{
		auto uavHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetCPUDescriptorHandleForHeapStart(), MSR_RESOURCE_VIEW_OUTPUT_COLOR, heapIncrementSize);
		d3dDevice->CreateUnorderedAccessView(output, nullptr, nullptr, uavHandle);
	}
	{
		auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetCPUDescriptorHandleForHeapStart(), MSR_RESOURCE_VIEW_INPUT_COLOR, heapIncrementSize);
		d3dDevice->CreateShaderResourceView(inputColor, nullptr, srvHandle);
	}
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_R32_FLOAT;
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipLevels = inputDepth->GetDesc().MipLevels;
		auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetCPUDescriptorHandleForHeapStart(), MSR_RESOURCE_VIEW_INPUT_DEPTH, heapIncrementSize);
		d3dDevice->CreateShaderResourceView(inputDepth, &desc, srvHandle);
	}
	{
		auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetCPUDescriptorHandleForHeapStart(), MSR_RESOURCE_VIEW_INPUT_MOTION, heapIncrementSize);
		d3dDevice->CreateShaderResourceView(inputMotion, nullptr, srvHandle);
	}

	// Reference the updated descriptor from GPU in the command list
	pCommandList->SetComputeRootDescriptorTable(0, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetGPUDescriptorHandleForHeapStart(), MSR_DESCRIPTOR_HEAP_UAV_START, heapIncrementSize));
	pCommandList->SetComputeRootDescriptorTable(1, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetGPUDescriptorHandleForHeapStart(), MSR_DESCRIPTOR_HEAP_SRV_START, heapIncrementSize));

	assert(sizeof(MSRRootConstants) % 4 == 0);
	pCommandList->SetComputeRoot32BitConstants(2, sizeof(MSRRootConstants) / 4, reinterpret_cast<void*>(&m_rootConstants), 0);

	//// Dispatch command
	pCommandList->Dispatch((pState->displayWidth + 7) / 8, (pState->displayHeight + 7) / 8, 1);
}

void UpscaleContext_MSR_API::ReloadPipelines()
{
	m_pDevice->GPUFlush();
	OnDestroyWindowSizeDependentResources();
	OnCreateWindowSizeDependentResources(m_input, m_output, m_renderWidth, m_renderHeight, m_displayWidth, m_displayHeight, m_hdr);
}
