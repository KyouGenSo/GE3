#include "TextureManager.h"
#include "DX12Basic.h"
#include "StringUtility.h"
#include <algorithm>
#include <cassert>

TextureManager* TextureManager::instance_ = nullptr;

uint32_t TextureManager::kSRVIndexStart = 1;

TextureManager* TextureManager::GetInstance()
{
	if (instance_ == nullptr)
	{
		instance_ = new TextureManager();
	}
	return instance_;
}

void TextureManager::Initialize(DX12Basic* dx12)
{
	dx12_ = dx12;
	textureDatas_.reserve(DX12Basic::kMaxSRVCount);
}

void TextureManager::Finalize()
{
	if (instance_ != nullptr)
	{
		delete instance_;
		instance_ = nullptr;
	}
}

void TextureManager::LoadTexture(const std::string& filePath)
{
	// 読み込み済みのテクスチャを検索
	auto it = std::find_if(
		textureDatas_.begin(),
		textureDatas_.end(),
		[&](const TextureData& textureData) { return textureData.filePath == filePath; });

	if (it != textureDatas_.end())
	{
		// すでに読み込まれている場合は何もしない
		return;
	}

	// テクスチャ枚数上限チェック
	assert(textureDatas_.size() + kSRVIndexStart < DX12Basic::kMaxSRVCount);
	
	// テクスチャの読み込み
	DirectX::ScratchImage image;
	std::wstring filePathW = StringUtility::ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	// mipmapを生成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	// テクスチャデータを追加
	textureDatas_.resize(textureDatas_.size() + 1);

	// 追加したテクスチャデータを取得
	TextureData& textureData = textureDatas_.back();

	textureData.filePath = filePath;
	textureData.metadata = mipImages.GetMetadata();
	textureData.resource = dx12_->MakeTextureResource(textureData.metadata);

	// テクスチャデータの要素番号をSRVのインデックスとして使用
	uint32_t srvIndex = static_cast<uint32_t>(textureDatas_.size() - 1) + kSRVIndexStart;

	// テクスチャデータのSRVハンドルを取得
	textureData.srvCpuHandle = dx12_->GetSRVCpuDescriptorHandle(srvIndex);
	textureData.srvGpuHandle = dx12_->GetSRVGpuDescriptorHandle(srvIndex);

	// SRVの作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = textureData.metadata.format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = UINT(textureData.metadata.mipLevels);

	dx12_->GetDevice()->CreateShaderResourceView(textureData.resource.Get(), &srvDesc, textureData.srvCpuHandle);
}

uint32_t TextureManager::GetTextureIndex(const std::string& filePath)
{
	// 読み込み済みのテクスチャを検索
	auto it = std::find_if(
		textureDatas_.begin(),
		textureDatas_.end(),
		[&](const TextureData& textureData) { return textureData.filePath == filePath; });

	if (it != textureDatas_.end()) {
		// 読み込み済みのテクスチャのインデックスを返す
		uint32_t textureIndex = static_cast<uint32_t>(std::distance(textureDatas_.begin(), it));
		return textureIndex;
	}

	assert(false);
	return 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSRVGpuHandle(uint32_t textureIndex)
{
	// 範囲外指定チェック
	assert(textureIndex < textureDatas_.size());

	TextureData& textureData = textureDatas_[textureIndex];
	return textureData.srvGpuHandle;
}
