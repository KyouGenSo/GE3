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

#include "WinApp.h"
#include "DX12Basic.h"
#include"Input.h"
#include "SpriteBasic.h"
#include "Sprite.h"
#include"D3DResourceLeakCheker.h"
#include"TextureManager.h"

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

struct Material
{
	Vector4 color;
	bool enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
};

struct TransformationMatrix
{
	Matrix4x4 WVP;
	Matrix4x4 world;
};

struct DirectionalLight
{
	Vector4 color;
	Vector3 direction;
	int32_t lightType;
	float intensity;
};

struct MaterialData {
	std::string texturePath;
};

struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
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

ModelData LoadObjFile(const std::string& directoryPath, const std::string& fileName);

MaterialData LoadMtlFile(const std::string& directoryPath, const std::string& fileName);

std::vector<ModelData> LoadMutiMeshObjFile(const std::string& directoryPath, const std::string& fileName);

std::vector<ModelData> LoadMutiMaterialFile(const std::string& directoryPath, const std::string& fileName);

std::unordered_map<std::string, MaterialData> LoadMutiMaterialMtlFile(const std::string& directoryPath, const std::string& fileName);

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

	//-----------------------------------------基盤システムの初期化-----------------------------------------//


	//------------------------------------------------------Sprite------------------------------------------------------
	
	// textureの読み込み
	TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");

	uint32_t spriteNum = 5;
	std::vector<Sprite*> sprites;

	for (uint32_t i = 0; i < spriteNum; i++) {
		Sprite* sprite = new Sprite();
		sprite->Initialize(spriteBasic, "resources/uvChecker.png");
		sprite->SetPos(Vector2(i * 150.0f, 0.0f));
		sprite->SetSize(Vector2(100.0f, 100.0f));
		sprites.push_back(sprite);
	}

	//------------------------------------------------------Sprite------------------------------------------------------//




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


		/// <summary>
		/// 描画処理
		/// </summary>

		// 描画前の処理
		dx12->BeginDraw();

		//-------------------ImGui-------------------//
		ImGui::Begin("Option");


		ImGui::End();

		// ImGuiの内部コマンドを生成。描画処理の前に行う
		ImGui::Render();

		//-------------------ImGui-------------------//

		// 共通描画設定
		spriteBasic->SetCommonRenderSetting();




		//-----------Spriteの描画-----------//

		for (uint32_t i = 0; i < spriteNum; i++)
		{
			// Spriteの描画
			sprites[i]->Draw();
		}

		//-----------Spriteの描画-----------//


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

ModelData LoadObjFile(const std::string& directoryPath, const std::string& fileName)
{
	ModelData modelData;
	VertexData triangleVertices[3];
	std::vector<Vector4> positions;
	std::vector<Vector2> texcoords;
	std::vector<Vector3> normals;
	std::string line;

	std::ifstream file(directoryPath + "/" + fileName);
	assert(file.is_open());

	while (std::getline(file, line))
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);

		} else if (identifier == "vt") {
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoords.push_back(texcoord);

		} else if (identifier == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);

		} else if (identifier == "f") {

			for (int32_t facevertex = 0; facevertex < 3; facevertex++) {
				std::string vertexDefiniton;
				s >> vertexDefiniton;

				std::istringstream v(vertexDefiniton);
				uint32_t elementIndices[3];

				for (int32_t element = 0; element < 3; element++) {
					std::string index;
					std::getline(v, index, '/');
					elementIndices[element] = std::stoi(index);
				}

				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];

				position.z *= -1.0f;
				normal.z *= -1.0f;
				texcoord.y = 1.0f - texcoord.y;

				triangleVertices[facevertex] = { position, texcoord, normal };

			}

			// 三角形の頂点データを追加
			modelData.vertices.push_back(triangleVertices[2]);
			modelData.vertices.push_back(triangleVertices[1]);
			modelData.vertices.push_back(triangleVertices[0]);

		} else if (identifier == "mtllib") {
			std::string mtlFileName;
			s >> mtlFileName;
			modelData.material = LoadMtlFile(directoryPath, mtlFileName);
		}
	}

	return modelData;
}

MaterialData LoadMtlFile(const std::string& directoryPath, const std::string& fileName)
{
	MaterialData materialData;
	std::string line;

	std::ifstream file(directoryPath + "/" + fileName);
	assert(file.is_open());

	while (std::getline(file, line))
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "map_Kd") {
			std::string textureFileName;
			s >> textureFileName;
			materialData.texturePath = directoryPath + "/" + textureFileName;
		}
	}

	return materialData;
}

std::vector<ModelData> LoadMutiMeshObjFile(const std::string& directoryPath, const std::string& fileName)
{
	std::vector<ModelData> modelDatas;
	ModelData modelData;
	VertexData triangleVertices[3];
	std::vector<Vector4> positions;
	std::vector<Vector2> texcoords;
	std::vector<Vector3> normals;
	std::string line;

	std::ifstream file(directoryPath + "/" + fileName);
	assert(file.is_open());

	while (std::getline(file, line))
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);

		} else if (identifier == "vt") {
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoords.push_back(texcoord);

		} else if (identifier == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);

		} else if (identifier == "f") {

			for (int32_t facevertex = 0; facevertex < 3; facevertex++) {
				std::string vertexDefinition;
				s >> vertexDefinition;

				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];

				for (int32_t element = 0; element < 3; element++) {
					std::string index;
					std::getline(v, index, '/');
					elementIndices[element] = std::stoi(index);
				}

				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];

				position.z *= -1.0f;
				normal.z *= -1.0f;
				texcoord.y = 1.0f - texcoord.y;

				triangleVertices[facevertex] = { position, texcoord, normal };

			}

			// Add the triangle vertices in reverse order
			modelData.vertices.push_back(triangleVertices[2]);
			modelData.vertices.push_back(triangleVertices[1]);
			modelData.vertices.push_back(triangleVertices[0]);

		} else if (identifier == "o" || identifier == "g") {
			if (!modelData.vertices.empty()) {
				modelDatas.push_back(modelData);
				modelData = ModelData();
			}
		} else if (identifier == "mtllib") {
			std::string mtlFileName;
			s >> mtlFileName;
			modelData.material = LoadMtlFile(directoryPath, mtlFileName);
		}
	}

	if (!modelData.vertices.empty()) {
		modelDatas.push_back(modelData);
	}

	return modelDatas;
}

std::vector<ModelData> LoadMutiMaterialFile(const std::string& directoryPath, const std::string& fileName)
{
	std::vector<ModelData> modelDatas;
	ModelData modelData;
	VertexData triangleVertices[3];
	std::vector<Vector4> positions;
	std::vector<Vector2> texcoords;
	std::vector<Vector3> normals;
	std::unordered_map<std::string, MaterialData> materials;
	std::string currentMaterial;
	std::string line;

	std::ifstream file(directoryPath + "/" + fileName);
	assert(file.is_open());

	while (std::getline(file, line))
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);

		} else if (identifier == "vt") {
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoords.push_back(texcoord);

		} else if (identifier == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);

		} else if (identifier == "f") {

			for (int32_t facevertex = 0; facevertex < 3; facevertex++) {
				std::string vertexDefinition;
				s >> vertexDefinition;

				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];

				for (int32_t element = 0; element < 3; element++) {
					std::string index;
					std::getline(v, index, '/');
					elementIndices[element] = std::stoi(index);
				}

				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];

				position.z *= -1.0f;
				normal.z *= -1.0f;
				texcoord.y = 1.0f - texcoord.y;

				triangleVertices[facevertex] = { position, texcoord, normal };
			}

			modelData.vertices.push_back(triangleVertices[2]);
			modelData.vertices.push_back(triangleVertices[1]);
			modelData.vertices.push_back(triangleVertices[0]);

		} else if (identifier == "o" || identifier == "g") {
			if (!modelData.vertices.empty()) {
				modelDatas.push_back(modelData);
				modelData = ModelData();
			}
		} else if (identifier == "usemtl") {
			s >> currentMaterial;
			if (!modelData.vertices.empty()) {
				modelDatas.push_back(modelData);
				modelData = ModelData();
			}
			modelData.material = materials[currentMaterial];
		} else if (identifier == "mtllib") {
			std::string mtlFileName;
			s >> mtlFileName;
			materials = LoadMutiMaterialMtlFile(directoryPath, mtlFileName);
		}
	}

	if (!modelData.vertices.empty()) {
		modelDatas.push_back(modelData);
	}

	return modelDatas;
}

std::unordered_map<std::string, MaterialData> LoadMutiMaterialMtlFile(const std::string& directoryPath, const std::string& fileName)
{
	std::unordered_map<std::string, MaterialData> materials;
	std::ifstream file(directoryPath + "/" + fileName);
	assert(file.is_open());

	std::string line, currentMaterialName;

	while (std::getline(file, line))
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "newmtl") {
			s >> currentMaterialName;
			materials[currentMaterialName] = MaterialData(); // マテリアルを初期化
		} else if (identifier == "map_Kd") {
			std::string textureFileName;
			s >> textureFileName;
			materials[currentMaterialName].texturePath = directoryPath + "/" + textureFileName;
		}
	}

	return materials;
}

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