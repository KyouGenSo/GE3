#include"Sprite.h"
#include"SpriteBasic.h"
#include "TextureManager.h"

void Sprite::Initialize(SpriteBasic* spriteBasic, std::string texturePath)
{
	// SpriteBasicクラスのインスタンスを保持
	spriteBasic_ = spriteBasic;

	// Transformの初期化
	transform_.scale = Vector3(size_.x, size_.y, 1.0f);
	transform_.rotate = Vector3(0.0f, 0.0f, rotation_);
	transform_.translate = { pos_.x, pos_.y, 0.0f };

	// 頂点データを生成
	CreateVertexData();

	// マテリアルデータを生成
	CreateMaterialData();

	// 座標変換行列データを生成
	CreateTransformationMatrixData();

	// テクスチャインデックスを保存
	textureIndex_ = TextureManager::GetInstance()->GetTextureIndex(texturePath);

}

void Sprite::Update()
{
	// トランスフォームの更新
	transform_.translate = { pos_.x, pos_.y, 0.0f };
	transform_.rotate = Vector3(0.0f, 0.0f, rotation_);
	transform_.scale = Vector3(size_.x, size_.y, 1.0f);

	// 頂点リソースにデータを書き込む
	vertexData_[0].position = { 0.0f, 1.0f, 0.0f, 1.0f }; // 左下
	vertexData_[0].texcoord = { 0.0f, 1.0f };
	vertexData_[0].normal = { 0.0f, 0.0f, -1.0f };

	vertexData_[1].position = { 0.0f, 0.0f, 0.0f, 1.0f }; // 左上
	vertexData_[1].texcoord = { 0.0f, 0.0f };
	vertexData_[1].normal = { 0.0f, 0.0f, -1.0f };

	vertexData_[2].position = { 1.0f, 1.0f, 0.0f, 1.0f }; // 右下
	vertexData_[2].texcoord = { 1.0f, 1.0f };
	vertexData_[2].normal = { 0.0f, 0.0f, -1.0f };

	vertexData_[3].position = { 1.0f, 0.0f, 0.0f, 1.0f }; // 右上
	vertexData_[3].texcoord = { 1.0f, 0.0f };
	vertexData_[3].normal = { 0.0f, 0.0f, -1.0f };

	// 三角形のインデックスデータを作成
	indexData_[0] = 0; indexData_[1] = 1; indexData_[2] = 2;
	indexData_[3] = 3; indexData_[4] = 2; indexData_[5] = 1;

	// Spriteの座標変換
	Matrix4x4 worldMatrixSprite = Mat4x4::MakeAffine(transform_.scale, transform_.rotate, transform_.translate);
	Matrix4x4 viewMatrixSprite = Mat4x4::MakeIdentity();
	Matrix4x4 projectionMatrixSprite = Mat4x4::MakeOrtho(0.0f, 0.0f, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 100.0f);
	Matrix4x4 wvpMatrixSprite = Mat4x4::Multiply(worldMatrixSprite, Mat4x4::Multiply(viewMatrixSprite, projectionMatrixSprite));

	transformationMatrixData_->WVP = wvpMatrixSprite;
	transformationMatrixData_->world = worldMatrixSprite;
}

void Sprite::Draw()
{
	// 頂点バッファビューの設定
	spriteBasic_->GetDX12Basic()->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// インデックスバッファビューの設定
	spriteBasic_->GetDX12Basic()->GetCommandList()->IASetIndexBuffer(&indexBufferView_);

	// マテリアルCBufferの場所を設定
	spriteBasic_->GetDX12Basic()->GetCommandList()->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

	// 座標変換行列CBufferの場所を設定
	spriteBasic_->GetDX12Basic()->GetCommandList()->SetGraphicsRootConstantBufferView(1, transformationMatrixResource_->GetGPUVirtualAddress());

	//D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle = TextureManager::GetInstance()->GetSRVGpuHandle(textureIndex_);
	
	// SRVのDescriptorTableを設定,テクスチャを指定
	spriteBasic_->GetDX12Basic()->GetCommandList()->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSRVGpuHandle(textureIndex_));
		//TextureManager::GetInstance()->GetSRVGpuHandle(textureIndex_)
		// spriteBasic_->GetDX12Basic()->GetSRVGpuDescriptorHandle(0)
	// 描画
	spriteBasic_->GetDX12Basic()->GetCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Sprite::CreateVertexData()
{
	// 頂点リソースを生成
	vertexResource_ = spriteBasic_->GetDX12Basic()->MakeBufferResource(sizeof(VertexData) * 4);

	// 頂点バッファビューを作成する
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	// 頂点リソースをマップ
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

	// インデックスリソースを生成
	indexResource_ = spriteBasic_->GetDX12Basic()->MakeBufferResource(sizeof(uint32_t) * 6);

	// インデックスバッファビューを作成する
	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

	// インデックスリソースをマップ
	indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));
}

void Sprite::CreateMaterialData()
{
	// マテリアルリソースを生成
	materialResource_ = spriteBasic_->GetDX12Basic()->MakeBufferResource(sizeof(Material));

	// マテリアルリソースをマップ
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

	// マテリアルデータの初期値を書き込む
	materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	materialData_->enableLighting = false;
	materialData_->uvTransform = Mat4x4::MakeIdentity();
}

void Sprite::CreateTransformationMatrixData()
{
	// 座標変換行列リソースを生成
	transformationMatrixResource_ = spriteBasic_->GetDX12Basic()->MakeBufferResource(sizeof(TransformationMatrix));

	// 座標変換行列リソースをマップ
	transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));

	// 座標変換行列データの初期値を書き込む
	transformationMatrixData_->WVP = Mat4x4::MakeIdentity();
	transformationMatrixData_->world = Mat4x4::MakeIdentity();
}
