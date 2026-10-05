#pragma once
#include "Renderer.h"
#include <map>
#include <random>

// ============================================================================
// パーティクル（煙・火・火花のような、小さな粒をたくさん出す表現）。
//
// 描き方：粒は「カメラの方を向いた四角形1枚」。粒の数だけDrawを呼ぶと重いので、
//   Instancing（同じ形を1回の命令で何個も描く）で全部まとめて描く。
//   粒ごとに違うのは位置・大きさ・色だけなので、それを粒の数だけ並べた配列
//   （StructuredBuffer）に書き込み、頂点シェーダーがSV_InstanceIDで自分の番号を引く。
//
// 使い方（ゲーム側）：
//   ParticleEmitter fire;
//   fire.position = { 0, 1, 0 };
//   int id = engine.AddParticleEmitter(fire);   // 出し続ける
//   engine.EmitParticles(id, 30);               // 一度にまとめて出す（爆発など）
//
// デバッグビュー（F2）のParticlesパネルで、全部の項目を画面から変えられる。
// 変えた内容は saved_particles.txt へ保存され、次の起動で重なる
// ============================================================================

// 粒1つぶんの、GPUへ渡す情報。Particle.hlsli の ParticleInstance と並びを合わせること
struct ParticleInstance {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

// 粒1つぶんの、CPU側で動かす状態
struct Particle {
    Vector3 position{};
    Vector3 velocity{};
    float age = 0.0f;
    float lifetime = 1.0f;
    float sizeStart = 0.3f;
    float sizeEnd = 0.0f;
};

// 粒を出す「口」。項目はすべて画面から変えられる
struct ParticleEmitter {
    std::string name = "Emitter";
    bool enabled = true;                       // falseの間は新しい粒を出さない（出ている粒は最後まで動く）
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    float rate = 40.0f;                        // 1秒に出す数（0なら EmitParticles で出したときだけ）
    float lifetimeMin = 0.8f, lifetimeMax = 1.6f; // 粒の寿命[秒]
    float speedMin = 1.0f, speedMax = 3.0f;    // 出るときの速さ[m/s]
    Vector3 direction{ 0.0f, 1.0f, 0.0f };     // 出る向き（正規化されなくてよい）
    float spread = 0.5f;                       // 向きからの広がり[rad]（円錐の半角）。0で一直線
    Vector3 gravity{ 0.0f, 1.0f, 0.0f };       // 加速度[m/s^2]。上向きなら炎、下向きなら火の粉
    float sizeStart = 0.35f, sizeEnd = 0.05f;  // 寿命の初めと終わりの大きさ
    Vector4 startColor{ 1.0f, 0.55f, 0.15f, 1.0f };
    Vector4 endColor{ 1.0f, 0.1f, 0.0f, 0.0f }; // 終わりのαを0にすると、消えていく
    BlendMode blendMode = BlendMode::kAdd;     // 重なると明るくなる加算が、火や光に向く
    bool soft = true;                          // 縁をぼかした丸にする（falseなら四角のまま）
    bool billboard = true;                     // trueならカメラの方を向く。falseなら向きを変えず、置いた板のまま
    bool uvChecker = false;                    // trueならuvChecker.pngを貼る（falseは白＝色だけ）
    int maxParticles = 512;                    // 同時に出しておく上限

    // ---- 実行時の状態（保存しない）----
    std::vector<Particle> particles;
    float emitAccumulator = 0.0f;
};

// 保存する項目だけを1行の文字にする／戻す（数字はexactに戻る桁数で書く）
inline std::string SerializeEmitter(const ParticleEmitter& e) {
    std::ostringstream s;
    s.precision(9);
    s << (e.enabled ? 1 : 0) << ' ' << e.position.x << ' ' << e.position.y << ' ' << e.position.z << ' '
      << e.rate << ' ' << e.lifetimeMin << ' ' << e.lifetimeMax << ' ' << e.speedMin << ' ' << e.speedMax << ' '
      << e.direction.x << ' ' << e.direction.y << ' ' << e.direction.z << ' ' << e.spread << ' '
      << e.gravity.x << ' ' << e.gravity.y << ' ' << e.gravity.z << ' ' << e.sizeStart << ' ' << e.sizeEnd << ' '
      << e.startColor.x << ' ' << e.startColor.y << ' ' << e.startColor.z << ' ' << e.startColor.w << ' '
      << e.endColor.x << ' ' << e.endColor.y << ' ' << e.endColor.z << ' ' << e.endColor.w << ' '
      << (int)e.blendMode << ' ' << (e.soft ? 1 : 0) << ' ' << e.maxParticles << ' '
      << (e.billboard ? 1 : 0) << ' ' << (e.uvChecker ? 1 : 0);
    return s.str();
}
inline bool DeserializeEmitter(const std::string& text, ParticleEmitter& e) {
    std::istringstream s(text);
    int enabled = 0, blend = 0, soft = 0;
    ParticleEmitter r = e;
    s >> enabled >> r.position.x >> r.position.y >> r.position.z
      >> r.rate >> r.lifetimeMin >> r.lifetimeMax >> r.speedMin >> r.speedMax
      >> r.direction.x >> r.direction.y >> r.direction.z >> r.spread
      >> r.gravity.x >> r.gravity.y >> r.gravity.z >> r.sizeStart >> r.sizeEnd
      >> r.startColor.x >> r.startColor.y >> r.startColor.z >> r.startColor.w
      >> r.endColor.x >> r.endColor.y >> r.endColor.z >> r.endColor.w
      >> blend >> soft >> r.maxParticles;
    if (s.fail()) { return false; } // 壊れた行は使わない
    // あとから足した項目。古い保存ファイルには無いので、読めなければ今の値のまま
    int billboard = r.billboard ? 1 : 0, uvChecker = r.uvChecker ? 1 : 0;
    if (s >> billboard >> uvChecker) { r.billboard = billboard != 0; r.uvChecker = uvChecker != 0; }
    r.enabled = enabled != 0;
    r.soft = soft != 0;
    r.blendMode = (blend >= 0 && blend < (int)BlendMode::kCount) ? (BlendMode)blend : BlendMode::kAdd;
    r.maxParticles = (std::max)(r.maxParticles, 0);
    e = std::move(r);
    return true;
}

// ---------------------------------------------------------------------------
// パーティクル描画用のシェーダー・ルートシグネチャ・PSO
// ---------------------------------------------------------------------------
struct ParticlePipeline {
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> psos[(int)BlendMode::kCount];
};

inline ParticlePipeline CreateParticlePipeline(ID3D12Device* device, Object3DPipeline& sharedCompiler, DXGI_FORMAT rtvFormat) {
    ParticlePipeline pipeline;

    // Object3D用のルートシグネチャとは別に作る（あちらは頂点シェーダーがCBVを使う前提）。
    //   0: 32bit定数2つ（描く先頭の番号・丸くするか）
    //   1: 粒の配列（StructuredBuffer = SRV）。頂点シェーダーがt0で読む
    //   2: テクスチャ。ピクセルシェーダーがt0で読む
    D3D12_DESCRIPTOR_RANGE instancingRange[1] = {};
    instancingRange[0].BaseShaderRegister = 0;
    instancingRange[0].NumDescriptors = 1;
    instancingRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    instancingRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE textureRange[1] = {};
    textureRange[0].BaseShaderRegister = 0;
    textureRange[0].NumDescriptors = 1;
    textureRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[3] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[0].Constants.ShaderRegister = 0;
    rootParameters[0].Constants.Num32BitValues = 2;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].DescriptorTable.pDescriptorRanges = instancingRange;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(instancingRange);

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = textureRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(textureRange);

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    ComPtr<ID3DBlob> sigBlob;
    ComPtr<ID3DBlob> errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&pipeline.rootSignature));
    assert(SUCCEEDED(hr));

    // 頂点の形はObject3Dと同じ（四角形1枚のModel）
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    ComPtr<IDxcBlob> vsBlob = CompileShader(L"Particle.VS.hlsl", L"vs_6_0", sharedCompiler.dxcUtils.Get(), sharedCompiler.dxcCompiler.Get(), sharedCompiler.includeHandler.Get());
    ComPtr<IDxcBlob> psBlob = CompileShader(L"Particle.PS.hlsl", L"ps_6_0", sharedCompiler.dxcUtils.Get(), sharedCompiler.dxcCompiler.Get(), sharedCompiler.includeHandler.Get());

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = pipeline.rootSignature.Get();
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = rtvFormat;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    // 手前にある物には隠れてほしいので奥行きは調べる。
    // 半透明の粒は奥行きを書かない（書くと後ろの粒が消えてしまう）。
    // 合成なし(None)の粒は不透明な板なので書く（奥の板が、手前の板を上書きしないように）
    psoDesc.DepthStencilState.DepthEnable = true;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    for (int i = 0; i < (int)BlendMode::kCount; ++i) {
        psoDesc.DepthStencilState.DepthWriteMask =
            (i == (int)BlendMode::kNone) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.BlendState.RenderTarget[0] = MakeBlendDesc((BlendMode)i);
        hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline.psos[i]));
        assert(SUCCEEDED(hr));
    }
    return pipeline;
}

// ---------------------------------------------------------------------------
// 粒の管理・更新・描画
// ---------------------------------------------------------------------------
class ParticleSystem {
public:
    // 全エミッターの粒を合わせた、1フレームで描ける上限。
    // 用意したResourceの数を超えて描くと範囲外を読むので、超える分は描かない
    static constexpr uint32_t kMaxInstances = 4096;

    void Initialize(GraphicsContext& gfx, Object3DPipeline& sharedCompiler) {
        pipeline_ = CreateParticlePipeline(gfx.device.Get(), sharedCompiler, gfx.rtvFormat);
        CreateQuad(gfx.device.Get());

        // 粒の配列を置くResource（毎フレームCPUから書くのでUploadヒープのまま持つ）
        instanceResource_ = CreateBufferResource(gfx.device.Get(), sizeof(ParticleInstance) * kMaxInstances);
        instanceResource_->Map(0, nullptr, reinterpret_cast<void**>(&instanceData_));

        // StructuredBufferのSRV。Textureと違い、形式はUNKNOWN・見方はBUFFERにする
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.NumElements = kMaxInstances;
        srvDesc.Buffer.StructureByteStride = sizeof(ParticleInstance);

        const UINT index = gfx.nextSrvIndex++; // ヒープの空いている次の番号を使う
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = gfx.srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += (SIZE_T)gfx.descriptorSizeSRV * index;
        instanceSrv_ = gfx.srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        instanceSrv_.ptr += (UINT64)gfx.descriptorSizeSRV * index;
        gfx.device->CreateShaderResourceView(instanceResource_.Get(), &srvDesc, cpu);
    }

    // ----- エミッターの管理 -----
    int AddEmitter(const ParticleEmitter& emitter) {
        emitters_.push_back(emitter);
        return (int)emitters_.size() - 1;
    }
    ParticleEmitter& Emitter(int id) { return emitters_[id]; }
    std::vector<ParticleEmitter>& Emitters() { return emitters_; }

    // まとめて出す（爆発・着弾など）。上限を超える分は出さない
    void Burst(int id, int count) {
        if (id < 0 || id >= (int)emitters_.size()) { return; }
        for (int i = 0; i < count; ++i) { Spawn(emitters_[id]); }
    }

    // ----- 毎フレーム -----
    void Update(float dt) {
        for (ParticleEmitter& e : emitters_) {
            if (e.enabled && e.rate > 0.0f) {
                e.emitAccumulator += e.rate * dt;
                while (e.emitAccumulator >= 1.0f) { e.emitAccumulator -= 1.0f; Spawn(e); }
            } else {
                e.emitAccumulator = 0.0f;
            }

            for (Particle& p : e.particles) {
                p.age += dt;
                p.velocity.x += e.gravity.x * dt;
                p.velocity.y += e.gravity.y * dt;
                p.velocity.z += e.gravity.z * dt;
                p.position.x += p.velocity.x * dt;
                p.position.y += p.velocity.y * dt;
                p.position.z += p.velocity.z * dt;
            }
            // 寿命の来た粒を消す（順番は問わないので、末尾と入れ替えて捨てる）
            for (size_t i = 0; i < e.particles.size();) {
                if (e.particles[i].age >= e.particles[i].lifetime) {
                    e.particles[i] = e.particles.back();
                    e.particles.pop_back();
                } else { ++i; }
            }
        }
    }

    // 位置を指定して1つ出す。位置以外の項目（寿命・速さ・大きさ・色）はエミッターの設定に従う。
    // 速さを0・重力を0にしておけば、その場に置いたままの板になる
    void SpawnAt(int id, const Vector3& position) {
        if (id < 0 || id >= (int)emitters_.size()) { return; }
        Spawn(emitters_[id], &position);
    }

    // 出ている粒をすべて消す
    void Clear() { for (ParticleEmitter& e : emitters_) { e.particles.clear(); e.emitAccumulator = 0.0f; } }

    // 今出ている粒の数（表示用）
    uint32_t ParticleCount() const {
        uint32_t n = 0;
        for (const ParticleEmitter& e : emitters_) { n += (uint32_t)e.particles.size(); }
        return n;
    }

    // 描く。カメラの向きを渡すのは、粒をカメラの方へ向けるため（ビルボード）
    void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewProjection,
              const Vector3& cameraRight, const Vector3& cameraUp, const Vector3& cameraForward,
              D3D12_GPU_DESCRIPTOR_HANDLE whiteTextureSrv, D3D12_GPU_DESCRIPTOR_HANDLE uvCheckerSrv) {
        if (ParticleCount() == 0) { return; }

        commandList->SetGraphicsRootSignature(pipeline_.rootSignature.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->IASetVertexBuffers(0, 1, &quadVbv_);
        commandList->IASetIndexBuffer(&quadIbv_);
        commandList->SetGraphicsRootDescriptorTable(1, instanceSrv_);

        uint32_t used = 0; // 配列のどこまで書いたか
        std::vector<const Particle*> sorted;
        for (ParticleEmitter& e : emitters_) {
            if (e.particles.empty()) { continue; }

            // 通常のαブレンドは、奥から順に重ねないと前後が逆に見える。加算などは順番に依らない
            sorted.clear();
            for (const Particle& p : e.particles) { sorted.push_back(&p); }
            if (e.blendMode == BlendMode::kNormal) {
                auto depth = [&](const Particle* p) {
                    return p->position.x * cameraForward.x + p->position.y * cameraForward.y + p->position.z * cameraForward.z;
                };
                std::sort(sorted.begin(), sorted.end(), [&](const Particle* a, const Particle* b) { return depth(a) > depth(b); });
            }

            const uint32_t first = used;
            for (const Particle* p : sorted) {
                if (used >= kMaxInstances) { break; } // 用意した数を超える分は描かない
                const float t = (std::min)(p->age / p->lifetime, 1.0f);
                const float size = p->sizeStart + (p->sizeEnd - p->sizeStart) * t;

                // カメラの右・上・前を軸にした行列＝カメラに正対した板。行ベクトル規約なので軸は行に並べる
                // billboardがfalseなら、軸は世界の軸のまま（置いた向きの板）
                const Vector3 right = e.billboard ? cameraRight : Vector3{ 1.0f, 0.0f, 0.0f };
                const Vector3 up = e.billboard ? cameraUp : Vector3{ 0.0f, 1.0f, 0.0f };
                const Vector3 fwd = e.billboard ? cameraForward : Vector3{ 0.0f, 0.0f, 1.0f };
                Matrix4x4 world = MakeIdentity4x4();
                world.m[0][0] = right.x * size; world.m[0][1] = right.y * size; world.m[0][2] = right.z * size;
                world.m[1][0] = up.x * size;    world.m[1][1] = up.y * size;    world.m[1][2] = up.z * size;
                world.m[2][0] = fwd.x * size;   world.m[2][1] = fwd.y * size;   world.m[2][2] = fwd.z * size;
                world.m[3][0] = p->position.x;          world.m[3][1] = p->position.y;          world.m[3][2] = p->position.z;

                ParticleInstance& inst = instanceData_[used++];
                inst.World = world;
                inst.WVP = Multiply(world, viewProjection);
                inst.color = {
                    e.startColor.x + (e.endColor.x - e.startColor.x) * t,
                    e.startColor.y + (e.endColor.y - e.startColor.y) * t,
                    e.startColor.z + (e.endColor.z - e.startColor.z) * t,
                    e.startColor.w + (e.endColor.w - e.startColor.w) * t,
                };
            }
            const uint32_t count = used - first;
            if (count == 0) { continue; }

            const uint32_t constants[2] = { first, e.soft ? 1u : 0u };
            commandList->SetGraphicsRoot32BitConstants(0, 2, constants, 0);
            commandList->SetGraphicsRootDescriptorTable(2, e.uvChecker ? uvCheckerSrv : whiteTextureSrv);
            commandList->SetPipelineState(pipeline_.psos[(int)e.blendMode].Get());
            // 6つのインデックスの四角形を、count個ぶん1回で描く
            commandList->DrawIndexedInstanced(6, count, 0, 0, 0);
        }
    }

    // ----- 保存 -----
    static constexpr const char* kFile = "saved_particles.txt";

    static std::string EmitterKey(const std::vector<ParticleEmitter>& list, size_t index) {
        int n = 0;
        for (size_t i = 0; i < index; ++i) { if (list[i].name == list[index].name) { ++n; } }
        return list[index].name + "#" + std::to_string(n);
    }

    // 起動時に、前回変えた内容を重ねる（エミッターを作ったあとで呼ぶ）
    void LoadSaved() {
        saved_.clear();
        std::ifstream file(kFile);
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') { continue; }
            size_t tab = line.find('\t');
            if (tab == std::string::npos) { continue; }
            saved_[line.substr(0, tab)] = line.substr(tab + 1);
        }
        for (size_t i = 0; i < emitters_.size(); ++i) {
            auto it = saved_.find(EmitterKey(emitters_, i));
            if (it != saved_.end()) { DeserializeEmitter(it->second, emitters_[i]); }
        }
    }

    // デバッグビューに入った時点の内容を控える／そこから変わった物だけを書く（Object3Dの保存と同じ考え方）
    void BeginEditSession() {
        snapshot_.clear();
        for (size_t i = 0; i < emitters_.size(); ++i) { snapshot_[EmitterKey(emitters_, i)] = SerializeEmitter(emitters_[i]); }
    }
    void SaveEdits() {
        bool changed = false;
        for (size_t i = 0; i < emitters_.size(); ++i) {
            const std::string key = EmitterKey(emitters_, i);
            const std::string now = SerializeEmitter(emitters_[i]);
            auto snap = snapshot_.find(key);
            if (snap != snapshot_.end() && snap->second == now) { continue; }
            saved_[key] = now;
            snapshot_[key] = now;
            changed = true;
        }
        if (!changed) { return; }
        std::ofstream file(kFile, std::ios::trunc);
        file << "# CG3 saved particle emitters (key \\t settings)\n";
        for (const auto& [key, text] : saved_) { file << key << '\t' << text << '\n'; }
    }

private:
    // 四角形1枚。中心が原点の1辺1（拡大縮小がそのまま粒の大きさになる）
    void CreateQuad(ID3D12Device* device) {
        std::vector<VertexData> vertices = {
            { { -0.5f,  0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
            { {  0.5f,  0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
            { { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
            { {  0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
        };
        std::vector<uint32_t> indices = { 0, 1, 2, 2, 1, 3 };

        const size_t vsize = sizeof(VertexData) * vertices.size();
        quadVertex_ = CreateBufferResource(device, vsize);
        VertexData* vdata = nullptr;
        quadVertex_->Map(0, nullptr, reinterpret_cast<void**>(&vdata));
        std::copy(vertices.begin(), vertices.end(), vdata);
        quadVertex_->Unmap(0, nullptr);
        quadVbv_.BufferLocation = quadVertex_->GetGPUVirtualAddress();
        quadVbv_.SizeInBytes = (UINT)vsize;
        quadVbv_.StrideInBytes = sizeof(VertexData);

        const size_t isize = sizeof(uint32_t) * indices.size();
        quadIndex_ = CreateBufferResource(device, isize);
        uint32_t* idata = nullptr;
        quadIndex_->Map(0, nullptr, reinterpret_cast<void**>(&idata));
        std::copy(indices.begin(), indices.end(), idata);
        quadIndex_->Unmap(0, nullptr);
        quadIbv_.BufferLocation = quadIndex_->GetGPUVirtualAddress();
        quadIbv_.SizeInBytes = (UINT)isize;
        quadIbv_.Format = DXGI_FORMAT_R32_UINT;
    }

    float Random(float lo, float hi) { return lo + (hi - lo) * std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_); }

    // 粒を1つ出す。向きは「direction を軸にした円錐の中」からランダムに選ぶ
    void Spawn(ParticleEmitter& e, const Vector3* at = nullptr) {
        if ((int)e.particles.size() >= e.maxParticles) { return; }

        Vector3 d = e.direction;
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        d = (len > 1e-6f) ? Vector3{ d.x / len, d.y / len, d.z / len } : Vector3{ 0.0f, 1.0f, 0.0f };

        // dに直角な軸を2本作る（dが真上・真下に近いときは、別の軸を基準にする）
        Vector3 ref = (std::fabs(d.y) < 0.99f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
        Vector3 t1 = { ref.y * d.z - ref.z * d.y, ref.z * d.x - ref.x * d.z, ref.x * d.y - ref.y * d.x };
        float l1 = std::sqrt(t1.x * t1.x + t1.y * t1.y + t1.z * t1.z);
        t1 = { t1.x / l1, t1.y / l1, t1.z / l1 };
        Vector3 t2 = { d.y * t1.z - d.z * t1.y, d.z * t1.x - d.x * t1.z, d.x * t1.y - d.y * t1.x };

        // 円錐の中で偏りなく選ぶ（cosθを一様にする）
        const float cosMax = std::cos((std::min)(e.spread, 3.14159265f));
        const float cosT = 1.0f - Random(0.0f, 1.0f) * (1.0f - cosMax);
        const float sinT = std::sqrt((std::max)(0.0f, 1.0f - cosT * cosT));
        const float phi = Random(0.0f, 6.2831853f);
        const float cx = std::cos(phi) * sinT, sy = std::sin(phi) * sinT;
        const Vector3 dir = { d.x * cosT + t1.x * cx + t2.x * sy,
                              d.y * cosT + t1.y * cx + t2.y * sy,
                              d.z * cosT + t1.z * cx + t2.z * sy };

        const float speed = Random(e.speedMin, e.speedMax);
        Particle p;
        p.position = at ? *at : e.position;
        p.velocity = { dir.x * speed, dir.y * speed, dir.z * speed };
        p.lifetime = (std::max)(Random(e.lifetimeMin, e.lifetimeMax), 0.01f);
        p.sizeStart = e.sizeStart;
        p.sizeEnd = e.sizeEnd;
        e.particles.push_back(p);
    }

    ParticlePipeline pipeline_;
    ComPtr<ID3D12Resource> quadVertex_, quadIndex_;
    D3D12_VERTEX_BUFFER_VIEW quadVbv_{};
    D3D12_INDEX_BUFFER_VIEW quadIbv_{};
    ComPtr<ID3D12Resource> instanceResource_;
    ParticleInstance* instanceData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE instanceSrv_{};
    std::vector<ParticleEmitter> emitters_;
    std::mt19937 rng_{ 12345 };
    std::map<std::string, std::string> saved_;    // 保存ファイルの内容（キー→設定）
    std::map<std::string, std::string> snapshot_; // デバッグビューに入った時点の内容
};

#ifdef USE_IMGUI
// デバッグビューに出す、エミッターの設定パネル
inline void DrawParticlePanel(ParticleSystem& system) {
    ImGui::SetNextWindowPos(ImVec2(380.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Particles")) { ImGui::End(); return; }

    ImGui::Text("particles: %u / %u", system.ParticleCount(), ParticleSystem::kMaxInstances);
    if (ImGui::Button("Clear")) { system.Clear(); }
    ImGui::Separator();

    static const char* kBlendNames[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
    std::vector<ParticleEmitter>& list = system.Emitters();
    for (int i = 0; i < (int)list.size(); ++i) {
        ParticleEmitter& e = list[i];
        ImGui::PushID(i);
        if (ImGui::CollapsingHeader(e.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("enabled", &e.enabled);
            ImGui::SameLine();
            if (ImGui::Button("Burst 30")) { system.Burst(i, 30); }
            ImGui::DragFloat3("position", &e.position.x, 0.05f);
            ImGui::DragFloat("rate (/s)", &e.rate, 0.5f, 0.0f, 2000.0f);
            ImGui::DragInt("max particles", &e.maxParticles, 1.0f, 0, (int)ParticleSystem::kMaxInstances);
            ImGui::DragFloatRange2("lifetime", &e.lifetimeMin, &e.lifetimeMax, 0.02f, 0.05f, 30.0f);
            ImGui::DragFloatRange2("speed", &e.speedMin, &e.speedMax, 0.05f, 0.0f, 50.0f);
            ImGui::DragFloat3("direction", &e.direction.x, 0.02f);
            ImGui::SliderAngle("spread", &e.spread, 0.0f, 180.0f);
            ImGui::DragFloat3("gravity", &e.gravity.x, 0.05f);
            ImGui::DragFloat("size start", &e.sizeStart, 0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("size end", &e.sizeEnd, 0.01f, 0.0f, 20.0f);
            ImGui::ColorEdit4("start color", &e.startColor.x);
            ImGui::ColorEdit4("end color", &e.endColor.x);
            int blend = (int)e.blendMode;
            if (ImGui::Combo("blendMode", &blend, kBlendNames, (int)BlendMode::kCount)) { e.blendMode = (BlendMode)blend; }
            ImGui::Checkbox("soft circle", &e.soft);
            ImGui::Checkbox("billboard", &e.billboard);
            ImGui::SameLine();
            ImGui::Checkbox("uvChecker", &e.uvChecker);
        }
        ImGui::PopID();
    }
    if (list.empty()) { ImGui::TextDisabled("No emitters. Use engine.AddParticleEmitter()."); }

    ImGui::End();
}
#endif // USE_IMGUI
