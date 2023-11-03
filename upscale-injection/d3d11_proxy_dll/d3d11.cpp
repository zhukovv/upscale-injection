#pragma once
#include <Windows.h>
#include "debug.h"
#include <stdint.h>
#include <d3dcompiler.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <DirectXMath.h>
#include <chrono>
#include <shlwapi.h>
#include "hooking.h"

#pragma comment (lib, "Shlwapi.lib") //for PathRemoveFileSpecA
#pragma comment(lib, "d3dcompiler.lib")


typedef HRESULT(__stdcall* fn_D3D11CreateDeviceAndSwapChain)(
	IDXGIAdapter*,
	D3D_DRIVER_TYPE,
	HMODULE,
	UINT,
	const D3D_FEATURE_LEVEL*,
	UINT,
	UINT,
	const DXGI_SWAP_CHAIN_DESC*,
	IDXGISwapChain**,
	ID3D11Device**,
	D3D_FEATURE_LEVEL*,
	ID3D11DeviceContext**);

typedef HRESULT(__stdcall* fn_DXGISwapChain_Present)(IDXGISwapChain*, UINT, UINT);

IDXGISwapChain* swapChain = nullptr;
ID3D11Device5* device = nullptr;
ID3D11DeviceContext4* devCon = nullptr;

// Shaders
ID3D10Blob* vsBlob = nullptr;
ID3D11VertexShader* vs = nullptr;
ID3D10Blob* psBlob = nullptr;
ID3D11PixelShader* ps = nullptr;
ID3D10Blob* csBlob = nullptr;
ID3D11ComputeShader* cs = nullptr;

ID3D11InputLayout* vertLayout = nullptr;
ID3D11RasterizerState* solidRasterState = nullptr;
ID3D11DepthStencilState* solidDepthStencilState = nullptr;

ID3D11SamplerState* samplerState = nullptr;

ID3D11Buffer* vertexBuffer = nullptr;
ID3D11Buffer* indexBuffer = nullptr;

ID3D11Texture2D* backBuffer;
ID3D11ShaderResourceView* backBufferSRV = nullptr;	
ID3D11Texture2D* tempOutput = nullptr;
ID3D11UnorderedAccessView* tempOutputUAV = nullptr;
// Dummy texture for Output-Merger (OM) while compute shader executing with backbuffer as resource.
ID3D11Texture2D* dummyTexture = nullptr;	
ID3D11RenderTargetView* dummyTextureRTV = nullptr;

ID3D11Texture2D* renderTexture = nullptr;
ID3D11RenderTargetView* renderTextureRTV = nullptr;
ID3D11UnorderedAccessView* renderTextureUAV = nullptr;
ID3D11ShaderResourceView* renderTextureSRV = nullptr;

ID3D11Buffer* inputBuffer = nullptr;
ID3D11Buffer* resolutionBuffer = nullptr;
// size of backbuffer
int resolutionX, resolutionY;	

auto lastTime = std::chrono::high_resolution_clock::now();
int lastFps;
double fpsTimePassed = 0.0;
double fpsTimePassMax = 0.1;

struct VertexData
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT2 Tex;
};

int DivideAndRoundUp(int x, int y)
{
	return (x % y) ? x / y + 1 : x / y;
}

HRESULT DXGISwapChain_Present_Hook(IDXGISwapChain* thisPtr, UINT SyncInterval, UINT Flags)
{
	auto currentTime = std::chrono::high_resolution_clock::now();

	
	const int targetCount = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

	ID3D11RenderTargetView* rtvs[targetCount] = { nullptr };
	ID3D11DepthStencilView* depthStencilView = nullptr;
	devCon->OMGetRenderTargets(targetCount, rtvs, &depthStencilView);

	devCon->OMSetRenderTargets(1, &renderTextureRTV, NULL);
	
	devCon->CSSetShader(cs, 0, 0);
	devCon->CSSetUnorderedAccessViews(0, 1, &renderTextureUAV, NULL);
	devCon->CSSetShaderResources(0, 1, &backBufferSRV);
	devCon->CSSetConstantBuffers(0, 1, &resolutionBuffer);
	devCon->CSSetConstantBuffers(1, 1, &inputBuffer);
	devCon->Dispatch(DivideAndRoundUp(resolutionX, 8), DivideAndRoundUp(resolutionY, 8), 1);

	// Unbinding UAV:
	ID3D11UnorderedAccessView* views[] = { NULL };
	devCon->CSSetUnorderedAccessViews(0, 1, views, NULL);

	// returning to the graphics pipeline:
	devCon->OMSetRenderTargets(targetCount, rtvs, depthStencilView);

	devCon->VSSetShader(vs, 0, 0);
	devCon->PSSetShader(ps, 0, 0);
	devCon->PSSetShaderResources(0, 1, &renderTextureSRV);
	devCon->PSSetSamplers(0, 1, &samplerState);
	devCon->IASetInputLayout(vertLayout);
	devCon->RSSetState(solidRasterState);
	devCon->OMSetDepthStencilState(solidDepthStencilState, 0);

	UINT stride = sizeof(VertexData);
	UINT offset = 0;
	devCon->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
	devCon->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
	devCon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	devCon->DrawIndexed(6, 0, 0);

	// Calculate FPS
	//auto endTime = std::chrono::high_resolution_clock::now();
	auto elapsed = std::chrono::duration<double, std::milli>(currentTime - lastTime);

	// Convert to seconds
	double dt = elapsed.count() * 0.001f;
	int fps = 1 / dt;
	lastTime = currentTime;

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	ZeroMemory(&mappedResource, sizeof(D3D11_MAPPED_SUBRESOURCE));

	//printf("FPS %d\n", fps);
	int input[] =
	{
		fps
	};

	// Send FPS to shader
	devCon->Map(inputBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	memcpy(mappedResource.pData, &input, sizeof(input));
	devCon->Unmap(inputBuffer, 0);

	fn_DXGISwapChain_Present DXGISwapChain_Present_Orig;
	PopAddress(uint64_t(&DXGISwapChain_Present_Orig));

	HRESULT r = DXGISwapChain_Present_Orig(thisPtr, SyncInterval, Flags);
	return r;
}


void LoadShaders()
{
#if defined(_DEBUG)
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif
	
	const int charNum = 512;
	wchar_t basepath[charNum], cspath[charNum], pspath[charNum], vspath[charNum];
	HMODULE hModule = GetModuleHandle(NULL);
	GetModuleFileNameW(hModule, basepath, 512);
	PathRemoveFileSpecW(basepath);

	wcscpy_s(cspath, charNum, basepath);
	wcscat_s(cspath, charNum, L"\\hook_content\\upscale_cs.shader");

	printf("path %S\n", cspath);

	wchar_t wPath[charNum+1];
	size_t outSize;

	ID3D10Blob* compileErrors;

	HRESULT err = D3DCompileFromFile(cspath, 0, 0, "main", "cs_5_0", compileFlags, 0, &csBlob, &compileErrors);
	if (compileErrors != nullptr && compileErrors)
	{
		ID3D10Blob* outErrorsDeref = compileErrors;
		OutputDebugStringA((char*)compileErrors->GetBufferPointer());
	}

	err = device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), NULL, &cs);
	check(err == S_OK);
	
	wcscpy_s(vspath, charNum, basepath);
	wcscat_s(vspath, charNum, L"\\hook_content\\passthrough_vs.shader");

	printf("path %S\n", vspath);

	compileErrors = nullptr;

	err = D3DCompileFromFile(vspath, 0, 0, "main", "vs_5_0", 0, 0, &vsBlob, &compileErrors);
	if (compileErrors != nullptr && compileErrors)
	{
		ID3D10Blob* outErrorsDeref = compileErrors;
		OutputDebugStringA((char*)compileErrors->GetBufferPointer());
	}

	err = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), NULL, &vs);
	check(err == S_OK);
	
	wcscpy_s(pspath, charNum, basepath);
	wcscat_s(pspath, charNum, L"\\hook_content\\passthrough_ps.shader");

	printf("path %S\n", pspath);

	compileErrors = nullptr;

	err = D3DCompileFromFile(pspath, 0, 0, "main", "ps_5_0", 0, 0, &psBlob, &compileErrors);
	if (compileErrors != nullptr && compileErrors)
	{
		ID3D10Blob* outErrorsDeref = compileErrors;
		OutputDebugStringA((char*)compileErrors->GetBufferPointer());
	}

	err = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), NULL, &ps);
	check(err == S_OK);
	
}

void CreateMesh()
{
	using namespace DirectX;

	const VertexData vertData[] =
	{
		VertexData { XMFLOAT3(-1, -1, 0.1), XMFLOAT2(0, 1) },
		VertexData { XMFLOAT3(1,  1, 0.1), XMFLOAT2(1, 0) },
		VertexData { XMFLOAT3(-1,  1, 0.1), XMFLOAT2(0, 0) },
		VertexData { XMFLOAT3(1, -1, 0.1), XMFLOAT2(1, 1) }
	};
	

	D3D11_BUFFER_DESC vertBufferDesc;
	ZeroMemory(&vertBufferDesc, sizeof(vertBufferDesc));
	vertBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vertBufferDesc.ByteWidth = sizeof(VertexData) * 4;
	vertBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertBufferDesc.CPUAccessFlags = 0;
	vertBufferDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA vertBufferData;
	ZeroMemory(&vertBufferData, sizeof(vertBufferData));
	vertBufferData.pSysMem = vertData;

	HRESULT res = device->CreateBuffer(&vertBufferDesc, &vertBufferData, &vertexBuffer);
	check(res == S_OK);

	// Indices:
	const uint32_t indices[] =
	{
		0, 1, 2,
		0, 1, 3
	};

	D3D11_BUFFER_DESC indexBufferDesc;
	ZeroMemory(&indexBufferDesc, sizeof(indexBufferDesc));
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = sizeof(uint32_t) * 6;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA indexBufferData;
	ZeroMemory(&indexBufferData, sizeof(indexBufferData));
	indexBufferData.pSysMem = indices;

	res = device->CreateBuffer(&indexBufferDesc, &indexBufferData, &indexBuffer);
	check(res == S_OK);
}

void CreateInputLayout()
{
	D3D11_INPUT_ELEMENT_DESC vertElements[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};

	HRESULT err = device->CreateInputLayout(vertElements, _countof(vertElements), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &vertLayout);
	check(err == S_OK);
}

void CreateRasterizerAndDepthStates()
{
	D3D11_RASTERIZER_DESC soliddesc;
	ZeroMemory(&soliddesc, sizeof(D3D11_RASTERIZER_DESC));
	soliddesc.FillMode = D3D11_FILL_SOLID;
	soliddesc.CullMode = D3D11_CULL_NONE;
	HRESULT err = device->CreateRasterizerState(&soliddesc, &solidRasterState);
	check(err == S_OK);

	D3D11_DEPTH_STENCIL_DESC depthDesc;
	ZeroMemory(&depthDesc, sizeof(D3D11_DEPTH_STENCIL_DESC));
	depthDesc.DepthEnable = true;
	depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
	err = device->CreateDepthStencilState(&depthDesc, &solidDepthStencilState);
	check(err == S_OK);

}

void CreateSRVFromBackBuffer()
{
	HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
	check(hr == S_OK);

	D3D11_TEXTURE2D_DESC bbTextureDesc;
	backBuffer->GetDesc(&bbTextureDesc);
	const DXGI_FORMAT backBufferFormat = bbTextureDesc.Format;

	//CD3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc(D3D11_SRV_DIMENSION_TEXTURE2D, backBufferFormat);
	hr = device->CreateShaderResourceView(backBuffer, NULL, &backBufferSRV);
	check(hr == S_OK);

	D3D11_TEXTURE2D_DESC dummyTextureDesc = bbTextureDesc;
	dummyTextureDesc.Width = 1;
	dummyTextureDesc.Height = 1;
	hr = device->CreateTexture2D(&dummyTextureDesc, NULL, &dummyTexture);
	check(hr == S_OK);
	hr = device->CreateRenderTargetView(dummyTexture, NULL, &dummyTextureRTV);
	check(hr == S_OK);


	D3D11_TEXTURE2D_DESC tempOutputDesc = {};
	tempOutputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	tempOutputDesc.Usage = D3D11_USAGE_DEFAULT;
	tempOutputDesc.Width = bbTextureDesc.Width;
	tempOutputDesc.Height = bbTextureDesc.Height;
	tempOutputDesc.Format = bbTextureDesc.Format;
	tempOutputDesc.ArraySize = tempOutputDesc.MipLevels = tempOutputDesc.SampleDesc.Count = 1;
	tempOutputDesc.Format = bbTextureDesc.Format;
	hr = device->CreateTexture2D(&tempOutputDesc, NULL, &tempOutput);
	check(hr == S_OK);
	hr = device->CreateUnorderedAccessView(tempOutput, NULL, &tempOutputUAV);
	check(hr == S_OK);

	D3D11_SAMPLER_DESC samplerDesc;
	ZeroMemory(&samplerDesc, sizeof(D3D11_SAMPLER_DESC));
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	samplerDesc.BorderColor[0] = 1;
	samplerDesc.BorderColor[1] = 0;
	samplerDesc.BorderColor[2] = 0;
	samplerDesc.BorderColor[3] = 0;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	hr = device->CreateSamplerState(&samplerDesc, &samplerState);
	check(hr == S_OK);
}

void CreateRenderTexture()
{
	ID3D11Texture2D* backBuffer = nullptr;
	HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);

	D3D11_TEXTURE2D_DESC bbTextureDesc;
	backBuffer->GetDesc(&bbTextureDesc);
	resolutionX = bbTextureDesc.Width;
	resolutionY = bbTextureDesc.Height;

	D3D11_TEXTURE2D_DESC renderTextureDesc = {};
	renderTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
	renderTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	renderTextureDesc.Width = bbTextureDesc.Width;
	renderTextureDesc.Height = bbTextureDesc.Height;
	renderTextureDesc.Format = bbTextureDesc.Format;	//DXGI_FORMAT_R8G8B8A8_UNORM;
	renderTextureDesc.ArraySize = renderTextureDesc.MipLevels = 1;
	renderTextureDesc.SampleDesc.Count = 1;
	//renderTextureDesc.SampleDesc.Quality = 0;

	hr = device->CreateTexture2D(&renderTextureDesc, NULL, &renderTexture);
	check(hr == S_OK);
	hr = device->CreateUnorderedAccessView(renderTexture, NULL, &renderTextureUAV);
	check(hr == S_OK);
	hr = device->CreateRenderTargetView(renderTexture, NULL, &renderTextureRTV);
	check(hr == S_OK);
	hr = device->CreateShaderResourceView(renderTexture, NULL, &renderTextureSRV);
	check(hr == S_OK);

	// Constant buffer 
	const uint32_t resValue[] =
	{
		resolutionX, resolutionY, 0, 0
	};

	D3D11_BUFFER_DESC resBufferDesc;
	ZeroMemory(&resBufferDesc, sizeof(resBufferDesc));
	resBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	resBufferDesc.ByteWidth = sizeof(uint32_t) * 4;
	resBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	resBufferDesc.CPUAccessFlags = 0;
	resBufferDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA resBufferData;
	ZeroMemory(&resBufferData, sizeof(resBufferData));
	resBufferData.pSysMem = resValue;

	hr = device->CreateBuffer(&resBufferDesc, &resBufferData, &resolutionBuffer);
	check(hr == S_OK);

	// Dynamic buffer
	int inputData[] =
	{
		0
	};

	D3D11_BUFFER_DESC inputDesc = { 0 };
	inputDesc.ByteWidth = sizeof(int) * ARRAYSIZE(inputData);
	inputDesc.Usage = D3D11_USAGE_DYNAMIC;
	inputDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	inputDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	inputDesc.MiscFlags = 0;
	inputDesc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA inputBufferData;
	inputBufferData.pSysMem = inputData;
	inputBufferData.SysMemPitch = 0;
	inputBufferData.SysMemSlicePitch = 0;

	hr = device->CreateBuffer(&inputDesc, &inputBufferData, &inputBuffer);
	check(hr == S_OK);


}

fn_D3D11CreateDeviceAndSwapChain LoadD3D11AndGetOriginalFuncPointer()
{
	char path[MAX_PATH];
	if (!GetSystemDirectoryA(path, MAX_PATH)) return nullptr;

	strcat_s(path, MAX_PATH * sizeof(char), "\\d3d11.dll");
	HMODULE d3d_dll = LoadLibraryA(path); 

	if (!d3d_dll)
	{
		MessageBox(NULL, TEXT("Could Not Locate Original D3D11 DLL"), TEXT("Darn"), 0);
		return nullptr;
	}

	return (fn_D3D11CreateDeviceAndSwapChain)GetProcAddress(d3d_dll, TEXT("D3D11CreateDeviceAndSwapChain"));
}

inline void** get_vtable_ptr(void* obj)
{
	return *reinterpret_cast<void***>(obj);
}

extern "C" HRESULT __stdcall D3D11CreateDeviceAndSwapChain(
	IDXGIAdapter * pAdapter,
	D3D_DRIVER_TYPE            DriverType,
	HMODULE                    Software,
	UINT                       Flags,
	const D3D_FEATURE_LEVEL * pFeatureLevels,
	UINT                       FeatureLevels,
	UINT                       SDKVersion,
	const DXGI_SWAP_CHAIN_DESC * pSwapChainDesc,
	IDXGISwapChain * *ppSwapChain,
	ID3D11Device * *ppDevice,
	D3D_FEATURE_LEVEL * pFeatureLevel,
	ID3D11DeviceContext * *ppImmediateContext
)
{
	//uncomment if you need to debug an issue in a project you aren't launching from VS
	//this gives you an easy way to make sure you can attach a debugger at the right time
	//MessageBox(NULL, TEXT("Calling D3D11CreateDeviceAndSwapChain"), TEXT("Ok"), 0);

	fn_D3D11CreateDeviceAndSwapChain D3D11CreateDeviceAndSwapChain_Orig = LoadD3D11AndGetOriginalFuncPointer();

	DXGI_SWAP_CHAIN_DESC newSwapChainDesc = *pSwapChainDesc;
	newSwapChainDesc.BufferUsage |= DXGI_USAGE_SHADER_INPUT;
	newSwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	HRESULT res = D3D11CreateDeviceAndSwapChain_Orig(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, &newSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);

	HRESULT hr = (*ppDevice)->QueryInterface(__uuidof(ID3D11Device5), (void**)&device);
	hr = (*ppImmediateContext)->QueryInterface(__uuidof(ID3D11DeviceContext), (void**)&devCon);

	LoadShaders();
	CreateMesh();
	CreateInputLayout();
	CreateRasterizerAndDepthStates();

	swapChain = *ppSwapChain;
	void** swapChainVTable = get_vtable_ptr(swapChain);
	
	InstallHook(swapChainVTable[8], DXGISwapChain_Present_Hook);
	//present is [8];

	CreateSRVFromBackBuffer();
	CreateRenderTexture();

	return res;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD ul_reason_for_call, LPVOID lpvReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		MessageBox(NULL, TEXT("Target app has loaded your proxy d3d11.dll and called DllMain. If you're launching the game via steam, you need to dismiss this popup quickly, otherwise you get a load error"), TEXT("Success"), 0);
	}

	return true;
}
