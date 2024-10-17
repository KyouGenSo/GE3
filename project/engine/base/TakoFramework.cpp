#include "TakoFramework.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "SceneManager.h"
#include "Object3dBasic.h"
#include "SpriteBasic.h"
#include "Model.h"
#include "ModelManager.h"
#include "Draw2D.h"

void TakoFramework::Initialize()
{
#pragma region ウィンドウの初期化-------------------------------------------------------------------------------------------------------------------
	winApp_ = new WinApp();
	winApp_->Initialize();
#pragma endregion


#pragma region 基盤システムの初期化-------------------------------------------------------------------------------------------------------------------
	dx12_ = new DX12Basic();
	dx12_->Initialize(winApp_);

#ifdef _DEBUG
	imguiManager_ = new ImGuiManager();
	imguiManager_->Initialize(winApp_, dx12_);
#endif

	TextureManager::GetInstance()->Initialize(dx12_);

	ModelManager::GetInstance()->Initialize(dx12_);

	Object3dBasic::GetInstance()->Initialize(dx12_);

	SpriteBasic::GetInstance()->Initialize(dx12_);

	Draw2D::GetInstance()->Initialize(dx12_);

	defaultCamera_ = new Camera();
	defaultCamera_->SetRotate(Vector3(0.3f, 0.0f, 0.0f));
	defaultCamera_->SetTranslate(Vector3(0.0f, 4.0f, -10.0f));

	// デフォルトカメラを設定
	Object3dBasic::GetInstance()->SetDefaultCamera(defaultCamera_);
#pragma endregion

}

void TakoFramework::Finalize()
{
	// SRVマネージャーの終了処理
	SrvManager::GetInstance()->Finalize();

	// ModelManagerの終了処理
	ModelManager::GetInstance()->Finalize();

	// TextureManagerの終了処理
	TextureManager::GetInstance()->Finalize();

	// SpriteBasicの終了処理
	SpriteBasic::GetInstance()->Finalize();

	// Object3dBasicの終了処理
	Object3dBasic::GetInstance()->Finalize();

	// シーンマネージャーの終了処理
	SceneManager::GetInstance()->Finalize();

	// Draw2Dの終了処理
	Draw2D::GetInstance()->Finalize();

#ifdef _DEBUG
	// ImGuiManagerの終了処理
	imguiManager_->Shutdown();
	delete imguiManager_;
#endif

	// DX12の終了処理
	dx12_->Finalize();

	// pointerの解放
	delete dx12_;
	delete defaultCamera_;
	delete sceneFactory_;

	winApp_->Finalize();

	// ウィンドウクラスの解放
	delete winApp_;
}

void TakoFramework::Update()
{
	// ウィンドウメッセージの取得
	if (winApp_->ProcessMessage()) {
		endFlag_ = true;
		return;
	}

	//	Draw2Dの更新
	Draw2D::GetInstance()->Update();

	// シーンマネージャーの更新
	SceneManager::GetInstance()->Update();
}

void TakoFramework::Run()
{
	Initialize();

	while (true)
	{
		Update();

		if (GetEndFlag()) {
			break;
		}

		Draw();
	}

	Finalize();
}
