#include<Windows.h>
#include<wrl.h>
#include<cstdint>
#include<string>
#include<fstream>
#include<sstream>
#include<format>
#include <unordered_map>
#include <cassert>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include<dxcapi.h>
#include <dxgidebug.h>
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dxguid.lib")

#include"externals/DirectXTex/d3dx12.h"
#include"externals/DirectXTex/DirectXTex.h"

#include"externals/imgui/imgui.h"
#include"externals/imgui/imgui_impl_win32.h"
#include"externals/imgui/imgui_impl_dx12.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include"WinApp.h"
#include"DX12Basic.h"
#include"Input.h"
#include"SpriteBasic.h"
#include"Sprite.h"
#include"D3DResourceLeakCheker.h"
#include"TextureManager.h"
#include"Object3d.h"
#include"Object3dBasic.h"
#include"Model.h"
#include"ModelBasic.h"

#include "xaudio2.h"
#pragma comment(lib, "xaudio2.lib")

#include "Logger.h"
#include "StringUtility.h"

// Function includes
#include"Vector4.h"
#include"Vector2.h"
#include"Mat4x4Func.h"
#include"Vec3Func.h"

#define PI 3.14159265359f

// ComPtrのエイリアス
template<class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;


struct VertexData
{
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct VertexDataNoTex
{
	Vector4 position;
	Vector3 normal;
};

struct VertexHash {
	size_t operator()(const VertexData& vertex) const {
		size_t h1 = std::hash<float>{}(vertex.position.x);
		size_t h2 = std::hash<float>{}(vertex.position.y);
		size_t h3 = std::hash<float>{}(vertex.position.z);
		size_t h4 = std::hash<float>{}(vertex.position.w);
		size_t h5 = std::hash<float>{}(vertex.texcoord.x);
		size_t h6 = std::hash<float>{}(vertex.texcoord.y);
		return h1 ^ h2 ^ h3 ^ h4 ^ h5 ^ h6;
	}
};

struct VertexEqual {
	bool operator()(const VertexData& lhs, const VertexData& rhs) const {
		return lhs.position.x == rhs.position.x &&
			lhs.position.y == rhs.position.y &&
			lhs.position.z == rhs.position.z &&
			lhs.position.w == rhs.position.w &&
			lhs.texcoord.x == rhs.texcoord.x &&
			lhs.texcoord.y == rhs.texcoord.y;
	}
};

struct ModelDataNoTex {
	std::vector<VertexDataNoTex> vertices;
};

struct ChunkHeader {
	char id[4]; // チャンクのID
	uint32_t size; // チャンクのサイズ
};

struct RiffHeader {
	ChunkHeader chunk; // "RIFF"
	char type[4]; // "WAVE"
};

struct FormatChunk {
	ChunkHeader chunk; // "fmt "
	WAVEFORMATEX fmt; // 波形フォーマット
};

struct SoundData {
	// 波形フォーマット
	WAVEFORMATEX wfex;
	// バッファの先頭アドレス
	BYTE* pBuffer;
	// バッファのサイズ
	unsigned int bufferSize;
};

//-----------------------------------------FUNCTION-----------------------------------------//

//std::vector<ModelData> LoadMutiMeshObjFile(const std::string& directoryPath, const std::string& fileName);

//std::vector<ModelData> LoadMutiMaterialFile(const std::string& directoryPath, const std::string& fileName);

//std::unordered_map<std::string, MaterialData> LoadMutiMaterialMtlFile(const std::string& directoryPath, const std::string& fileName);

ModelDataNoTex LoadObjFileNoTex(const std::string& directoryPath, const std::string& fileName);

SoundData LoadWaveFile(const char* filename);

void SoundUnload(SoundData* soundData);

void SoundPlay(IXAudio2* xAudio2, const SoundData& soundData);

//-----------------------------------------FUNCTION-----------------------------------------//


//Windowsプログラムのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{

	// リソースリークチェッカー
	D3DResourceLeakCheker d3dResourceLeakCheker;

	//------------------------------------------WINDOW------------------------------------------

	//ウィンドウクラスの初期化
	WinApp* winApp = new WinApp();
	winApp->Initialize();

	//-----------------------------------------WINDOW-----------------------------------------//

	//-----------------------------------------汎用機能初期化-----------------------------------------
	HRESULT hr;

	Input* input = new Input();
	input->Initialize(winApp);

	//XAudio2
	ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masterVoice;

	// XAudio2の初期化
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	hr = xAudio2->CreateMasteringVoice(&masterVoice);

	// サウンドの読み込み
	SoundData soundData = LoadWaveFile("resources/fanfare.wav");

	//-----------------------------------------汎用機能初期化-----------------------------------------//


	//-----------------------------------------基盤システムの初期化-----------------------------------------

	// DX12の初期化
	DX12Basic* dx12 = new DX12Basic();
	dx12->Initialize(winApp);

	// TextureManagerの初期化
	TextureManager::GetInstance()->Initialize(dx12);

	// Sprite共通クラスの初期化
	SpriteBasic* spriteBasic = new SpriteBasic();
	spriteBasic->Initialize(dx12);

	// Object共通クラスの初期化
	Object3dBasic* object3dBasic = new Object3dBasic();
	object3dBasic->Initialize(dx12);

	// Model共通クラスの初期化
	ModelBasic* modelBasic = new ModelBasic();
	modelBasic->Initialize(dx12);

	//-----------------------------------------基盤システムの初期化-----------------------------------------//

	

	//// 球のリソースを作る----------------------------------------------------------------------------------------------
	//const int kVertexCount = 16 * 16 * 6;

	//ComPtr<ID3D12Resource> sphereVertexResource = dx12->MakeBufferResource(sizeof(VertexData) * kVertexCount);
	//sphereVertexResource->SetName(L"SphereVertexResource");

	////VertexBufferView
	//D3D12_VERTEX_BUFFER_VIEW sphereVertexBufferView{};
	//// リソースの先頭アドレスから使う
	//sphereVertexBufferView.BufferLocation = sphereVertexResource->GetGPUVirtualAddress();
	//// 使用するリソースのサイズは頂点三つ分のサイズ
	//sphereVertexBufferView.SizeInBytes = sizeof(VertexData) * kVertexCount;
	//// 一つの頂点のサイズ
	//sphereVertexBufferView.StrideInBytes = sizeof(VertexData);

	//// 頂点リソースにデータを書き込む
	//VertexData* sphereVertexData = nullptr;
	//// アドレスを取得
	//sphereVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&sphereVertexData));

	//// 球を作成(vertexIndex Version)-------------------------------------------------------------//
	//ComPtr<ID3D12Resource> sphereIndexResource = dx12->MakeBufferResource(sizeof(uint32_t) * kVertexCount);
	//sphereIndexResource->SetName(L"SphereIndexResource");

	//D3D12_INDEX_BUFFER_VIEW sphereIndexBufferView{};
	//sphereIndexBufferView.BufferLocation = sphereIndexResource->GetGPUVirtualAddress();
	//sphereIndexBufferView.SizeInBytes = sizeof(uint32_t) * kVertexCount;
	//sphereIndexBufferView.Format = DXGI_FORMAT_R32_UINT;

	//uint32_t* indexData = nullptr;
	//sphereIndexResource->Map(0, nullptr, reinterpret_cast<void**>(&indexData));


	//const uint32_t kSubdivision = 16;
	//const float kLonEvery = DirectX::XM_2PI / float(kSubdivision);
	//const float kLatEvery = DirectX::XM_PI / float(kSubdivision);

	//std::unordered_map<VertexData, uint32_t, VertexHash, VertexEqual> uniqueVertices;

	//uint32_t uniqueVertexCount = 0;

	//for (int latIndex = 0; latIndex < kSubdivision; latIndex++) {

	//	float theta = -DirectX::XM_PIDIV2 + kLatEvery * latIndex;

	//	for (int lonIndex = 0; lonIndex < kSubdivision; lonIndex++) {

	//		uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;
	//		float phi = kLonEvery * lonIndex;

	//		VertexData vertices[6];
	//		vertices[0] = { cos(theta) * cos(phi), sin(theta), cos(theta) * sin(phi), 1.0f, float(lonIndex) / kSubdivision, 1.0f - float(latIndex) / kSubdivision };
	//		vertices[1] = { cos(theta + kLatEvery) * cos(phi), sin(theta + kLatEvery), cos(theta + kLatEvery) * sin(phi), 1.0f, float(lonIndex) / kSubdivision, 1.0f - float(latIndex + 1) / kSubdivision };
	//		vertices[2] = { cos(theta) * cos(phi + kLonEvery), sin(theta), cos(theta) * sin(phi + kLonEvery), 1.0f, float(lonIndex + 1) / kSubdivision, 1.0f - float(latIndex) / kSubdivision };
	//		vertices[3] = { cos(theta) * cos(phi + kLonEvery), sin(theta), cos(theta) * sin(phi + kLonEvery), 1.0f, float(lonIndex + 1) / kSubdivision, 1.0f - float(latIndex) / kSubdivision };
	//		vertices[4] = { cos(theta + kLatEvery) * cos(phi), sin(theta + kLatEvery), cos(theta + kLatEvery) * sin(phi), 1.0f, float(lonIndex) / kSubdivision, 1.0f - float(latIndex + 1) / kSubdivision };
	//		vertices[5] = { cos(theta + kLatEvery) * cos(phi + kLonEvery), sin(theta + kLatEvery), cos(theta + kLatEvery) * sin(phi + kLonEvery), 1.0f, float(lonIndex + 1) / kSubdivision, 1.0f - float(latIndex + 1) / kSubdivision };

	//		for (int i = 0; i < 6; i++) {
	//			auto iter = uniqueVertices.find(vertices[i]);
	//			if (iter != uniqueVertices.end()) {
	//				indexData[start + i] = iter->second;
	//			} else {
	//				uniqueVertices[vertices[i]] = uniqueVertexCount;
	//				sphereVertexData[uniqueVertexCount] = vertices[i];
	//				indexData[start + i] = uniqueVertexCount;
	//				uniqueVertexCount++;
	//			}
	//		}
	//	}
	//}

	//for (int i = 0; i < kVertexCount; i++)
	//{
	//	sphereVertexData[i].normal = Vector3(sphereVertexData[i].position.x, sphereVertexData[i].position.y, sphereVertexData[i].position.z);
	//}

	//sphereVertexBufferView.SizeInBytes = sizeof(VertexData) * uniqueVertexCount;
	//sphereIndexBufferView.SizeInBytes = sizeof(uint32_t) * kVertexCount;


#pragma region Sprite初期化

	// textureの読み込み
	TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");
	TextureManager::GetInstance()->LoadTexture("resources/checkerBoard.png");

	// スプライトの数
	uint32_t spriteNum = 2;

	std::vector<Sprite*> sprites;

	for (uint32_t i = 0; i < spriteNum; i++) {
		Sprite* sprite = new Sprite();
		if (i % 2 == 0)
			sprite->Initialize(spriteBasic, "resources/uvChecker.png");
		else
			sprite->Initialize(spriteBasic, "resources/checkerBoard.png");
		sprite->SetPos(Vector2(i * 500.0f, 0.0f));
		sprites.push_back(sprite);
	}

#pragma endregion


#pragma region Model初期化

	Model* teapotModel = new Model();
	teapotModel->Initialize(modelBasic, "teapot.obj");

#pragma endregion


#pragma region OBject3d初期化

	Object3d* object3d = new Object3d();
	object3d->Initialize(object3dBasic);
	object3d->SetModel(teapotModel);
	object3d->SetTranslate(Vector3(-3.0f, 0.0f, 0.0f));

	Object3d* object3d2 = new Object3d();
	object3d2->Initialize(object3dBasic);
	object3d2->SetModel(teapotModel);
	object3d2->SetTranslate(Vector3(3.0f, 0.0f, 0.0f));

#pragma endregion


	//---------------------------------------------------GAMELOOP-----------------------------------------------------//

	// ウィンドウが閉じられるまでループ
	while (true)
	{
		// ウィンドウメッセージの取得
		if (winApp->ProcessMessage()) {
			// ウィンドウが閉じられたらループを抜ける
			break;
		}

		//-------------imguiの初期化-------------//
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		//-------------imguiの初期化-------------//

		/// <summary>
		/// 更新処理
		/// </summary>

		// 入力情報の更新
		input->Update();


		// Spriteの更新
		for (uint32_t i = 0; i < spriteNum; i++) {
			sprites[i]->Update();
		}

		object3d->SetRotate(Vector3(0.0f, object3d->GetRotate().y + 0.01f, 0.0f));

		object3d2->SetRotate(Vector3(object3d2->GetRotate().x + 0.01f, 0.0f, 0.0f));

		// Object3dの更新
		object3d->Update();
		object3d2->Update();

		/// <summary>
		/// 描画処理
		/// </summary>

		// 描画前の処理
		dx12->BeginDraw();

		//-------------------ImGui-------------------//



		// ImGuiの内部コマンドを生成。描画処理の前に行う
		ImGui::Render();

		//-------------------ImGui-------------------//
		
		//-------------------Modelの描画-------------------//
		// 3Dモデル共通描画設定
		object3dBasic->SetCommonRenderSetting();

		// 3Dモデルの描画
		object3d->Draw();
		object3d2->Draw();

		//-------------------Modelの描画-------------------//


		//-------------------Spriteの描画-------------------//

		// スプライト共通描画設定
		spriteBasic->SetCommonRenderSetting();

		//for (uint32_t i = 0; i < spriteNum; i++)
		//{
		//	// Spriteの描画
		//	sprites[i]->Draw();
		//}

		//-------------------Spriteの描画-------------------//


		// commandListにimguiの描画コマンドを積む。描画処理の後、RTVからPRESENT Stateに戻す前に行う
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12->GetCommandList());

		// 描画後の処理
		dx12->EndDraw();

	}

	//-------------------------------------------------GAMELOOP-----------------------------------------------------/


	TextureManager::GetInstance()->Finalize();

	dx12->Finalize();

	// XAudio2の解放
	xAudio2.Reset();
	SoundUnload(&soundData);

	// pointerの解放
	delete input;
	delete dx12;
	delete spriteBasic;
	delete object3d;
	delete object3dBasic;

	for (uint32_t i = 0; i < spriteNum; i++)
	{
		delete sprites[i];
	}

#ifdef _DEBUG
	//debugController->Release();
#endif 

	winApp->Finalize();

	// ウィンドウクラスの解放
	delete winApp;

	return 0;
}


// 関数の定義-------------------------------------------------------------------------------------------------------------------

//std::vector<ModelData> LoadMutiMeshObjFile(const std::string& directoryPath, const std::string& fileName)
//{
//	std::vector<ModelData> modelDatas;
//	ModelData modelData;
//	VertexData triangleVertices[3];
//	std::vector<Vector4> positions;
//	std::vector<Vector2> texcoords;
//	std::vector<Vector3> normals;
//	std::string line;
//
//	std::ifstream file(directoryPath + "/" + fileName);
//	assert(file.is_open());
//
//	while (std::getline(file, line))
//	{
//		std::string identifier;
//		std::istringstream s(line);
//		s >> identifier;
//
//		if (identifier == "v") {
//			Vector4 position;
//			s >> position.x >> position.y >> position.z;
//			position.w = 1.0f;
//			positions.push_back(position);
//
//		} else if (identifier == "vt") {
//			Vector2 texcoord;
//			s >> texcoord.x >> texcoord.y;
//			texcoords.push_back(texcoord);
//
//		} else if (identifier == "vn") {
//			Vector3 normal;
//			s >> normal.x >> normal.y >> normal.z;
//			normals.push_back(normal);
//
//		} else if (identifier == "f") {
//
//			for (int32_t facevertex = 0; facevertex < 3; facevertex++) {
//				std::string vertexDefinition;
//				s >> vertexDefinition;
//
//				std::istringstream v(vertexDefinition);
//				uint32_t elementIndices[3];
//
//				for (int32_t element = 0; element < 3; element++) {
//					std::string index;
//					std::getline(v, index, '/');
//					elementIndices[element] = std::stoi(index);
//				}
//
//				Vector4 position = positions[elementIndices[0] - 1];
//				Vector2 texcoord = texcoords[elementIndices[1] - 1];
//				Vector3 normal = normals[elementIndices[2] - 1];
//
//				position.z *= -1.0f;
//				normal.z *= -1.0f;
//				texcoord.y = 1.0f - texcoord.y;
//
//				triangleVertices[facevertex] = { position, texcoord, normal };
//
//			}
//
//			// Add the triangle vertices in reverse order
//			modelData.vertices.push_back(triangleVertices[2]);
//			modelData.vertices.push_back(triangleVertices[1]);
//			modelData.vertices.push_back(triangleVertices[0]);
//
//		} else if (identifier == "o" || identifier == "g") {
//			if (!modelData.vertices.empty()) {
//				modelDatas.push_back(modelData);
//				modelData = ModelData();
//			}
//		} else if (identifier == "mtllib") {
//			std::string mtlFileName;
//			s >> mtlFileName;
//			modelData.material = LoadMtlFile(directoryPath, mtlFileName);
//		}
//	}
//
//	if (!modelData.vertices.empty()) {
//		modelDatas.push_back(modelData);
//	}
//
//	return modelDatas;
//}
//
//std::vector<ModelData> LoadMutiMaterialFile(const std::string& directoryPath, const std::string& fileName)
//{
//	std::vector<ModelData> modelDatas;
//	ModelData modelData;
//	VertexData triangleVertices[3];
//	std::vector<Vector4> positions;
//	std::vector<Vector2> texcoords;
//	std::vector<Vector3> normals;
//	std::unordered_map<std::string, MaterialData> materials;
//	std::string currentMaterial;
//	std::string line;
//
//	std::ifstream file(directoryPath + "/" + fileName);
//	assert(file.is_open());
//
//	while (std::getline(file, line))
//	{
//		std::string identifier;
//		std::istringstream s(line);
//		s >> identifier;
//
//		if (identifier == "v") {
//			Vector4 position;
//			s >> position.x >> position.y >> position.z;
//			position.w = 1.0f;
//			positions.push_back(position);
//
//		} else if (identifier == "vt") {
//			Vector2 texcoord;
//			s >> texcoord.x >> texcoord.y;
//			texcoords.push_back(texcoord);
//
//		} else if (identifier == "vn") {
//			Vector3 normal;
//			s >> normal.x >> normal.y >> normal.z;
//			normals.push_back(normal);
//
//		} else if (identifier == "f") {
//
//			for (int32_t facevertex = 0; facevertex < 3; facevertex++) {
//				std::string vertexDefinition;
//				s >> vertexDefinition;
//
//				std::istringstream v(vertexDefinition);
//				uint32_t elementIndices[3];
//
//				for (int32_t element = 0; element < 3; element++) {
//					std::string index;
//					std::getline(v, index, '/');
//					elementIndices[element] = std::stoi(index);
//				}
//
//				Vector4 position = positions[elementIndices[0] - 1];
//				Vector2 texcoord = texcoords[elementIndices[1] - 1];
//				Vector3 normal = normals[elementIndices[2] - 1];
//
//				position.z *= -1.0f;
//				normal.z *= -1.0f;
//				texcoord.y = 1.0f - texcoord.y;
//
//				triangleVertices[facevertex] = { position, texcoord, normal };
//			}
//
//			modelData.vertices.push_back(triangleVertices[2]);
//			modelData.vertices.push_back(triangleVertices[1]);
//			modelData.vertices.push_back(triangleVertices[0]);
//
//		} else if (identifier == "o" || identifier == "g") {
//			if (!modelData.vertices.empty()) {
//				modelDatas.push_back(modelData);
//				modelData = ModelData();
//			}
//		} else if (identifier == "usemtl") {
//			s >> currentMaterial;
//			if (!modelData.vertices.empty()) {
//				modelDatas.push_back(modelData);
//				modelData = ModelData();
//			}
//			modelData.material = materials[currentMaterial];
//		} else if (identifier == "mtllib") {
//			std::string mtlFileName;
//			s >> mtlFileName;
//			materials = LoadMutiMaterialMtlFile(directoryPath, mtlFileName);
//		}
//	}
//
//	if (!modelData.vertices.empty()) {
//		modelDatas.push_back(modelData);
//	}
//
//	return modelDatas;
//}
//
//std::unordered_map<std::string, MaterialData> LoadMutiMaterialMtlFile(const std::string& directoryPath, const std::string& fileName)
//{
//	std::unordered_map<std::string, MaterialData> materials;
//	std::ifstream file(directoryPath + "/" + fileName);
//	assert(file.is_open());
//
//	std::string line, currentMaterialName;
//
//	while (std::getline(file, line))
//	{
//		std::string identifier;
//		std::istringstream s(line);
//		s >> identifier;
//
//		if (identifier == "newmtl") {
//			s >> currentMaterialName;
//			materials[currentMaterialName] = MaterialData(); // マテリアルを初期化
//		} else if (identifier == "map_Kd") {
//			std::string textureFileName;
//			s >> textureFileName;
//			materials[currentMaterialName].texturePath = directoryPath + "/" + textureFileName;
//		}
//	}
//
//	return materials;
//}

ModelDataNoTex LoadObjFileNoTex(const std::string& directoryPath, const std::string& fileName) {
	ModelDataNoTex modelData;
	VertexDataNoTex triangleVertices[3];
	std::vector<Vector4> positions;
	std::vector<Vector3> normals;
	std::string line;

	std::ifstream file(directoryPath + "/" + fileName);
	assert(file.is_open());

	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);

		} else if (identifier == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);

		} else if (identifier == "f") {

			for (int32_t facevertex = 0; facevertex < 3; facevertex++) {
				std::string vertexDefinition;
				s >> vertexDefinition;

				// 文字列を二つのスラッシュ "//" で分割する
				size_t firstSlash = vertexDefinition.find("//");
				size_t secondSlash = vertexDefinition.find("//", firstSlash + 2);

				// 頂点位置と法線のインデックスを抽出する
				uint32_t positionIndex = std::stoi(vertexDefinition.substr(0, firstSlash));
				uint32_t normalIndex = std::stoi(vertexDefinition.substr(firstSlash + 2, secondSlash - (firstSlash + 2)));

				Vector4 position = positions[positionIndex - 1];
				Vector3 normal = normals[normalIndex - 1];

				// 座標系変換を処理する
				position.z *= -1.0f;
				normal.z *= -1.0f;

				triangleVertices[facevertex] = { position, normal };
			}

			// 三角形の頂点データを追加する
			modelData.vertices.push_back(triangleVertices[2]);
			modelData.vertices.push_back(triangleVertices[1]);
			modelData.vertices.push_back(triangleVertices[0]);
		}
	}

	return modelData;
}

SoundData LoadWaveFile(const char* filename)
{
	//HRESULT result;

	std::ifstream file;
	// バイナリモードで開く
	file.open(filename, std::ios::binary);
	assert(file.is_open());

	// wavファイルのヘッダーを読み込む
	RiffHeader riff;
	file.read(reinterpret_cast<char*>(&riff), sizeof(riff));

	if (strncmp(riff.chunk.id, "RIFF", 4) != 0 || strncmp(riff.type, "WAVE", 4) != 0)
	{
		assert(false);
	}


	FormatChunk format = {};
	file.read(reinterpret_cast<char*>(&format), sizeof(ChunkHeader));

	if (strncmp(format.chunk.id, "fmt ", 4) != 0)
	{
		assert(false);
	}
	assert(format.chunk.size <= sizeof(format.fmt));
	file.read(reinterpret_cast<char*>(&format.fmt), format.chunk.size);


	ChunkHeader data;
	file.read(reinterpret_cast<char*>(&data), sizeof(data));

	if (strncmp(data.id, "JUNK ", 4) == 0)
	{
		file.seekg(data.size, std::ios::cur);
		file.read(reinterpret_cast<char*>(&data), sizeof(data));
	}

	if (strncmp(data.id, "data ", 4) != 0)
	{
		assert(false);
	}

	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);

	file.close();

	SoundData soundData = {};

	soundData.wfex = format.fmt;
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
	soundData.bufferSize = data.size;

	return soundData;
}

void SoundUnload(SoundData* soundData) {
	delete[] soundData->pBuffer;
	soundData->pBuffer = 0;
	soundData->bufferSize = 0;
	soundData->wfex = {};
}

void SoundPlay(IXAudio2* xAudio2, const SoundData& soundData) {
	HRESULT result;

	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
	assert(SUCCEEDED(result));

	XAUDIO2_BUFFER buffer = {};
	buffer.pAudioData = soundData.pBuffer;
	buffer.AudioBytes = soundData.bufferSize;
	buffer.Flags = XAUDIO2_END_OF_STREAM;

	result = pSourceVoice->SubmitSourceBuffer(&buffer);
	result = pSourceVoice->Start();
}