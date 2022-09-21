#include "stdafx.h"
#include "UpscaleContext_MSR_API.h"

UpscaleContext_MSR_API::UpscaleContext_MSR_API(UpscaleType type, std::string name)
	: UpscaleContext(name)
{
}

void UpscaleContext_MSR_API::OnCreate(const FfxUpscaleInitParams& initParams)
{
	UpscaleContext::OnCreate(initParams);
	ID3D12Device *d3dDevice = m_pDevice->GetDevice();

	D3D12_ROOT_PARAMETER rootParameters[MSR_ROOT_PARAM_COUNT] = {{}};

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
	descRangeSRV.NumDescriptors = 1;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[1].DescriptorTable.pDescriptorRanges = &descRangeSRV;
	
	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.NumParameters = 2;
	rootSignatureDesc.pParameters = rootParameters;

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

	D3D12_SHADER_BYTECODE csShader;
	CompileShaderFromFile("accumulate_and_upscale_pass.hlsl", nullptr, "mainCS", "-T cs_6_0 -Zi -Od", &csShader);

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
	pipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	pipelineStateDesc.pRootSignature = m_rootSignature;
	pipelineStateDesc.CS = csShader;
	ThrowIfFailed(d3dDevice->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pipelineStates[MSR_PASS_ACCUMULATE_AND_UPSCALE])));

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 2;
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
	ID3D12Resource *output = cameraSetup.resolvedColorResource;

	ID3D12DescriptorHeap* descHeaps[] = { m_UAV_SRV_CBV_Heap };
	pCommandList->SetDescriptorHeaps(_countof(descHeaps), descHeaps);

	pCommandList->SetComputeRootSignature(m_rootSignature);

	pCommandList->SetPipelineState(m_pipelineStates[MSR_PASS_ACCUMULATE_AND_UPSCALE]);

	size_t heapIncrementSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		auto uavHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetCPUDescriptorHandleForHeapStart(), 0, heapIncrementSize);
		d3dDevice->CreateUnorderedAccessView(output, nullptr, nullptr, uavHandle);
	}
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetCPUDescriptorHandleForHeapStart(), 1, heapIncrementSize);
		d3dDevice->CreateShaderResourceView(inputColor, nullptr, srvHandle);
	}

	// Reference the updated descriptor from GPU in the command list
	pCommandList->SetComputeRootDescriptorTable(0, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetGPUDescriptorHandleForHeapStart(), 0, heapIncrementSize));
	pCommandList->SetComputeRootDescriptorTable(1, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_UAV_SRV_CBV_Heap->GetGPUDescriptorHandleForHeapStart(), 1, heapIncrementSize));

	//// Dispatch command
	pCommandList->Dispatch(240, 135, 1);
}

void UpscaleContext_MSR_API::ReloadPipelines()
{
	m_pDevice->GPUFlush();
	OnDestroyWindowSizeDependentResources();
	OnCreateWindowSizeDependentResources(m_input, m_output, m_renderWidth, m_renderHeight, m_displayWidth, m_displayHeight, m_hdr);
}
