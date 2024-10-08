#include "DX12Basic.h"
#include <cassert>
#include "Logger.h"
#include "StringUtility.h"
#include <format>
#include <thread>

#include"externals/imgui/imgui.h"
#include"externals/imgui/imgui_impl_win32.h"
#include"externals/imgui/imgui_impl_dx12.h"

#include"externals/DirectXTex/d3dx12.h"
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dxguid.lib")

// 最大のSRV数
const uint32_t DX12Basic::kMaxSRVCount = 512;

DX12Basic::~DX12Basic()
{

}

void DX12Basic::Initialize(WinApp* winApp)
{
	assert(winApp);
	this->winApp_ = winApp;

	commandQueue_ = nullptr;
	commandList_ = nullptr;
	commandAllocator_ = nullptr;

	// FPS制御の初期化
	InitFPSLimiter();

	// Deviceの初期化
	InitDevice();

	// Command関連の初期化
	InitCommand();

	// スワップチェインの生成
	CreateSwapChain();

	// 深度バッファの生成
	CreateDepthStencilResource();

	// デスクリプタヒープの初期化
	InitDescriptorHeap();

	// レンダーターゲットビューの初期化
	InitRTV();

	// 深度ステンシルビューの初期化
	InitDSV();

	// Fenceの初期化
	InitFence();

	// ビューポート矩形の初期化
	InitViewport();

	// シザー矩形の初期化
	InitScissorRect();

	// DXCコンパイラの生成
	CreateDXCCompiler();

	// ImGuiの初期化
	InitImGui();

}

void DX12Basic::Finalize()
{
	// ImGuiの終了処理
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	// コマンド完了まで待機
	fenceValue_++;
	// コマンドキューにFenceのシグナルを行う
	commandQueue_->Signal(fence_.Get(), fenceValue_);
	if (fence_->GetCompletedValue() < fenceValue_)
	{
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	// イベントの破棄
	CloseHandle(fenceEvent_);
}

void DX12Basic::BeginDraw()
{
	// バッグバッファのインデックスを取得
	UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

	// ---PRESENTからRTV用のStateにするTransitionBarrierを張る--- //
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	// バリアを張る対象のリソース。現在のバックバッファに対して行う
	barrier.Transition.pResource = swapChainResources_[backBufferIndex].Get();
	// 遷移前（現在）のresourceState
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	// 遷移後のresourceState
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	// バリアを張る
	commandList_->ResourceBarrier(1, &barrier);
	// ---PRESENTからRTV用のStateにするTransitionBarrierを張る--- //

	// 描画先のRTVとDSVを設定
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList_->OMSetRenderTargets(1, &rtvHandle_[backBufferIndex], false, &dsvHandle);

	// クリアカラー
	float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };

	// 画面の色をクリア
	commandList_->ClearRenderTargetView(rtvHandle_[backBufferIndex], clearColor, 0, nullptr);

	// 深度ステンシルをクリア
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// SRVのヒープを指定
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heaps[] = { srvHeap_.Get() };
	commandList_->SetDescriptorHeaps(_countof(heaps), heaps->GetAddressOf()); 

	// ビューポートとシザリング矩形をセット
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);

}

void DX12Basic::EndDraw()
{
	HRESULT	hr;

	// バッグバッファのインデックスを取得
	UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

	// ---RTVからPRESENT用のStateにするTransitionBarrierを張る--- //
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	// バリアを張る対象のリソース。現在のバックバッファに対して行う
	barrier.Transition.pResource = swapChainResources_[backBufferIndex].Get();
	// 遷移前（現在
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	// 遷移後
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	// バリアを張る
	commandList_->ResourceBarrier(1, &barrier);
	// ---RTVからPRESENT用のStateにするTransitionBarrierを張る--- //

	// コマンドリストのクローズ
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	// GPUにコマンドリストの実行を行わせる
	Microsoft::WRL::ComPtr<ID3D12CommandList> commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(_countof(commandLists), commandLists->GetAddressOf());

	// コマンド完了まで待機
	fenceValue_++;
	// コマンドキューにFenceのシグナルを行う
	commandQueue_->Signal(fence_.Get(), fenceValue_);
	if (fence_->GetCompletedValue() < fenceValue_)
	{
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	// FPS制御更新
	UpdateFPSLimiter();

	// GPUとOSに画面の交換を行うよう通知する
	swapChain_->Present(1, 0);

	// コマンドアロケータをリセット
	hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));

	// コマンドリストをリセット
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));

}

void DX12Basic::InitDevice()
{
	HRESULT hr;

	//-----------------------------------------------------デバッグレイヤーの設定-----------------------------------------------------
#ifdef _DEBUG
	ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		//デバッグレイヤーを有効にする
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	//------------------------------------------------------DXGIファクトリの生成------------------------------------------------------
	hr = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgiFactory_));
	assert(SUCCEEDED(hr));

	//-----------------------------------------------------------アダプターの列挙-----------------------------------------------------------
	ComPtr<IDXGIAdapter4> useAdapter = nullptr;
	for (UINT adapterIndex = 0; dxgiFactory_->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++adapterIndex)
	{
		//アダプターの情報を取得
		DXGI_ADAPTER_DESC3 desc;
		hr = useAdapter->GetDesc3(&desc);
		assert(SUCCEEDED(hr));

		if (!(desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
		{
			Logger::Log(std::format("Use Adapter:{}\n", StringUtility::ConvertString(desc.Description)));
			break;
		}
		useAdapter = nullptr;
	}
	assert(useAdapter != nullptr);

	//---------------------------------------------------------デバイスの生成---------------------------------------------------------
	// 機能レベルとログ出力用文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelNames[] = {
		"12.2",
		"12.1",
		"12.0"
	};

	// 高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
		if (SUCCEEDED(hr)) {
			Logger::Log(std::format("Feature Level: {}\n", featureLevelNames[i]));
			break;
		}
	}
	assert(device_ != nullptr);
	Logger::Log("D3D12Device Created\n");


	//-----------------------------------------------------エラーチェック-----------------------------------------------------
#ifdef _DEBUG
	ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue))))
	{
		// デバッグレイヤーの設定
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
		};

		D3D12_MESSAGE_SEVERITY severities[] = {
			D3D12_MESSAGE_SEVERITY_INFO
		};

		D3D12_INFO_QUEUE_FILTER filter = {};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;

		infoQueue->PushStorageFilter(&filter);
	}
#endif

}

void DX12Basic::InitCommand()
{
	HRESULT hr;

	// コマンドキューの設定
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // コマンドリストの種類
	commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL; // 優先度
	commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE; // フラグ
	commandQueueDesc.NodeMask = 0; // ノードマスク

	// コマンドキューの生成
	hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	assert(SUCCEEDED(hr));

	// コマンドアロケータの生成
	hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	assert(SUCCEEDED(hr));

	// コマンドリストの生成
	hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	assert(SUCCEEDED(hr));


}

void DX12Basic::CreateSwapChain()
{
	HRESULT hr;

	swapChainBufferCount_ = 2;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = WinApp::kClientWidth; // 画面の幅
	swapChainDesc.Height = WinApp::kClientHeight; // 画面の高さ
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 色の形式(バックバッファのフォーマット)
	swapChainDesc.SampleDesc.Count = 1; // マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // バックバッファとして使用
	swapChainDesc.BufferCount = swapChainBufferCount_; // バッファ数
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // フリップ後破棄

	//コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr = dxgiFactory_->CreateSwapChainForHwnd(commandQueue_.Get(),
		winApp_->GetHWnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		reinterpret_cast<IDXGISwapChain1**>(swapChain_.GetAddressOf())
	);
	assert(SUCCEEDED(hr));
}

void DX12Basic::CreateDepthStencilResource()
{
	// テクスチャの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = WinApp::kClientWidth; // テクスチャの幅
	resourceDesc.Height = WinApp::kClientHeight; // テクスチャの高さ
	resourceDesc.MipLevels = 1; // ミップマップレベル
	resourceDesc.DepthOrArraySize = 1; // 奥行き or 配列サイズ
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット
	resourceDesc.SampleDesc.Count = 1; // サンプル数、１固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // 深度ステンシルとして使う

	// ヒープの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

	// 深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	// クリア値
	depthClearValue.DepthStencil.Depth = 1.0f; // 1.0f(最大値)でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット

	// Resourceの生成
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties, // ヒープの設定
		D3D12_HEAP_FLAG_NONE, // Heapの特殊な設定。特になし
		&resourceDesc, // リソースの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE, // リソースの初期状態. DEPTH_WRITE
		&depthClearValue, // クリア値の設定.
		IID_PPV_ARGS(&depthStencilResource_)); // 生成したリソースのpointerへのpointerを取得
	assert(SUCCEEDED(hr));

}

void DX12Basic::InitDescriptorHeap()
{
	// ディスクリプタヒープのサイズを取得
	descriptorSizeRTV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	descriptorSizeDSV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// RTVのディスクリプタヒープの生成
	rtvHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	// DSVのディスクリプタヒープの生成
	dsvHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	// SRVのディスクリプタヒープの生成
	srvHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxSRVCount, true);
}

void DX12Basic::InitRTV()
{
	// SwapChainからResourceを取得
	for (UINT i = 0; i < 2; ++i)
	{
		HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
		assert(SUCCEEDED(hr));
	}

	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // 出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2Dテクスチャとして書き込む

	// DescriptorHeapの先頭を取得
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = GetCPUDescriptorHandle(rtvHeap_.Get(), descriptorSizeRTV_, 0);

	for (UINT i = 0; i < kRtvHandleCount; ++i)
	{
		// RTVのハンドルを取得
		if (i == 0) {
			rtvHandle_[i] = rtvStartHandle;
		} else {
			rtvHandle_[i].ptr += rtvHandle_[i - 1].ptr + descriptorSizeRTV_;
		}

		// RTVの作成
		device_->CreateRenderTargetView(swapChainResources_[i].Get(), &rtvDesc, rtvHandle_[i]);
	}

}

void DX12Basic::InitDSV()
{
	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2Dテクスチャとして書き込む

	// DSVのハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

	// DSVの作成
	device_->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, dsvHandle);
}

void DX12Basic::InitFence()
{
	// Fenceの初期値
	fenceValue_ = 0;

	// フェンスの生成
	HRESULT hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	assert(SUCCEEDED(hr));

	// イベントの生成
	fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent_ != nullptr);
}

void DX12Basic::InitViewport()
{
	// ビューポートの設定
	viewport_.TopLeftX = 0;
	viewport_.TopLeftY = 0;
	viewport_.Width = static_cast<float>(WinApp::kClientWidth);
	viewport_.Height = static_cast<float>(WinApp::kClientHeight);
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;
}

void DX12Basic::InitScissorRect()
{
	// シザー矩形の設定
	scissorRect_.left = 0;
	scissorRect_.right = WinApp::kClientWidth;
	scissorRect_.top = 0;
	scissorRect_.bottom = WinApp::kClientHeight;
}

void DX12Basic::CreateDXCCompiler()
{
	HRESULT hr;
	// DXCUtillitiesの生成
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	assert(SUCCEEDED(hr));

	// DXCコンパイラの生成
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
	assert(SUCCEEDED(hr));

	// IncludeHandlerの生成
	hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	assert(SUCCEEDED(hr));

}

void DX12Basic::InitImGui()
{
	// バージョンチェック
	IMGUI_CHECKVERSION();

	// コンテキストの生成
	ImGui::CreateContext();

	// スタイルの設定
	ImGui::StyleColorsDark();

	// Win32用の初期化
	ImGui_ImplWin32_Init(winApp_->GetHWnd());

	// DX12用の初期化
	ImGui_ImplDX12_Init(
		device_.Get(),                                   // デバイス
		swapChainBufferCount_,                           // バッファ数
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,                 // RTVのフォーマット
		srvHeap_.Get(),                                  // SRVのヒープ
		srvHeap_->GetCPUDescriptorHandleForHeapStart(),  // SRVのCPUハンドルの開始位置
		srvHeap_->GetGPUDescriptorHandleForHeapStart()); // SRVのGPUハンドルの開始位置

}

void DX12Basic::InitFPSLimiter()
{
	// 現在の時間を記録
	referenceTime_ = std::chrono::steady_clock::now();

}

void DX12Basic::UpdateFPSLimiter()
{
	// 1/60秒
	const std::chrono::microseconds kMinFrameTime(uint64_t(1000000.0 / 60.0));
	// 1/65秒
	const std::chrono::microseconds kMinCheckTime(uint64_t(1000000.0 / 65.0));

	// 現在の時間を取得
	auto currentTime = std::chrono::steady_clock::now();
	// 経過時間を取得
	auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - referenceTime_);

	// 経過時間が1/65秒未満の場合
	if (elapsedTime < kMinCheckTime)
	{
		// 経過時間が1/60秒未満の間、処理を待機
		while (std::chrono::steady_clock::now() - referenceTime_ < kMinFrameTime)
		{
			// 処理を待機
			std::this_thread::sleep_for(std::chrono::microseconds(1));
		}
	}

	// 現在の時間を更新
	referenceTime_ = std::chrono::steady_clock::now();
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DX12Basic::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
	// ヒープの設定
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.NumDescriptors = numDescriptors;
	heapDesc.Type = heapType;
	heapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	// ヒープの生成
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	HRESULT hr = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap));
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Basic::GetSRVCpuDescriptorHandle(uint32_t index)
{
	return GetCPUDescriptorHandle(srvHeap_.Get(), descriptorSizeSRV_, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12Basic::GetSRVGpuDescriptorHandle(uint32_t index)
{
	return GetGPUDescriptorHandle(srvHeap_.Get(), descriptorSizeSRV_, index);
}

Microsoft::WRL::ComPtr<IDxcBlob> DX12Basic::CompileShader(const std::wstring& filePath, const wchar_t* profile)
{
	//hlslファイルを読み込む
	Logger::Log(StringUtility::ConvertString(std::format(L"Begin CompileShader, path:{}, prefile:{}\n", filePath, profile)));
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	assert(SUCCEEDED(hr));

	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	//コンパイルオプション
	LPCWSTR arguments[] = {
		filePath.c_str(),
		L"-E", L"main",
		L"-T", profile,
		L"-Zi", //デバッグ情報を出力
		L"-Qembed_debug", //デバッグ情報を埋め込む
		L"-Od", //最適化なし
		L"-Zpr", //行数情報を出力
	};

	//コンパイル
	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	hr = dxcCompiler_->Compile(
		&shaderSourceBuffer,
		arguments, _countof(arguments),
		includeHandler_.Get(),
		IID_PPV_ARGS(&shaderResult));
	assert(SUCCEEDED(hr));

	//エラーメッセージを取得
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0)
	{
		Logger::Log(shaderError->GetStringPointer());
		assert(false);
	}

	//コンパイル結果を取得
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	Logger::Log(StringUtility::ConvertString(std::format(L"Compile Succeeded, path:{}, prefile:{}\n", filePath, profile)));
	shaderSource->Release();
	shaderResult->Release();
	return shaderBlob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> DX12Basic::MakeBufferResource(size_t sizeInBytes)
{
	// バッファの設定
	D3D12_RESOURCE_DESC bufferDesc{};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = sizeInBytes;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// ヒープの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// リソースの生成
	ID3D12Resource* bufferResource = nullptr;

	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&bufferResource));
	assert(SUCCEEDED(hr));

	return bufferResource;
}

void DX12Basic::CreateBufferResource(ComPtr<ID3D12Resource>& buffer, size_t sizeInBytes)
{
	// バッファの設定
	D3D12_RESOURCE_DESC bufferDesc{};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = sizeInBytes;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// ヒープの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer));
	assert(SUCCEEDED(hr));

}

Microsoft::WRL::ComPtr<ID3D12Resource> DX12Basic::MakeTextureResource(const DirectX::TexMetadata& metaData)
{
	// テクスチャの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metaData.width); // テクスチャの幅
	resourceDesc.Height = UINT(metaData.height); // テクスチャの高さ
	resourceDesc.DepthOrArraySize = UINT16(metaData.arraySize); // 配列サイズ
	resourceDesc.MipLevels = UINT16(metaData.mipLevels); // ミップマップレベル
	resourceDesc.Format = metaData.format; // フォーマット
	resourceDesc.SampleDesc.Count = 1; // サンプル数
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metaData.dimension); // テクスチャの次元

	// ヒープの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

	// Resourceの設定
	ID3D12Resource* textureResource = nullptr;

	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties, // ヒープの設定
		D3D12_HEAP_FLAG_NONE, // Heapの特殊な設定。特になし
		&resourceDesc, // リソースの設定
		D3D12_RESOURCE_STATE_COPY_DEST, // リソースの初期状態. データ転送される設定
		nullptr, // クリア値の設定。今回は使わないのでnullptr
		IID_PPV_ARGS(&textureResource)); // 生成したリソースのpointerへのpointerを取得
	assert(SUCCEEDED(hr));

	return textureResource;
}

void DX12Basic::CreateTextureResource(ComPtr<ID3D12Resource>& textureResource, const DirectX::TexMetadata& metaData)
{
	// テクスチャの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metaData.width); // テクスチャの幅
	resourceDesc.Height = UINT(metaData.height); // テクスチャの高さ
	resourceDesc.DepthOrArraySize = UINT16(metaData.arraySize); // 配列サイズ
	resourceDesc.MipLevels = UINT16(metaData.mipLevels); // ミップマップレベル
	resourceDesc.Format = metaData.format; // フォーマット
	resourceDesc.SampleDesc.Count = 1; // サンプル数
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metaData.dimension); // テクスチャの次元

	// ヒープの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties, // ヒープの設定
		D3D12_HEAP_FLAG_NONE, // Heapの特殊な設定。特になし
		&resourceDesc, // リソースの設定
		D3D12_RESOURCE_STATE_COPY_DEST, // リソースの初期状態. データ転送される設定
		nullptr, // クリア値の設定。今回は使わないのでnullptr
		IID_PPV_ARGS(&textureResource)); // 生成したリソースのpointerへのpointerを取得
	assert(SUCCEEDED(hr));

}

Microsoft::WRL::ComPtr<ID3D12Resource> DX12Basic::UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages)
{
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	// subresourcesの配列を作る
	DirectX::PrepareUpload(device_.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	// intermediateResourceに必要なサイズを取得
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));
	// intermediateResourceを作成
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = MakeBufferResource(intermediateSize);

	// intermediateResourceにSubresourceのデータを書き込み、textureに転送するコマンドを積む
	UpdateSubresources(commandList_.Get(), texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());

	// ResourceBarrierを使ってResourceStateを変更
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList_->ResourceBarrier(1, &barrier);

	return intermediateResource;
}

DirectX::ScratchImage DX12Basic::LoadTexture(const std::string& filePath)
{
	// テクスチャの読み込み
	DirectX::ScratchImage image;
	std::wstring filePathW = StringUtility::ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	// mipmapを生成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	return mipImages;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Basic::GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += (descriptorSize * index);
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12Basic::GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += (descriptorSize * index);
	return handle;
}

