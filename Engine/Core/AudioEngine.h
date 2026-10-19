#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <wrl.h>
#include <xaudio2.h>

using Microsoft::WRL::ComPtr;

// LoadWaveで読み込んだWAVサウンドデータ（フォーマット情報＋PCMデータ本体）
struct SoundData {
    WAVEFORMATEX wfex{};
    BYTE* pBuffer = nullptr;
    uint32_t bufferSize = 0;
};

// 再生中の1つのサウンドを操作するためのハンドル。AudioEngine::Playの戻り値として受け取る
class SoundVoice {
public:
    void Stop();                     // 再生を止めてSourceVoiceを破棄する
    void SetVolume(float volume);    // 0.0〜（1.0が等倍）
    void SetPitch(float pitchRatio); // 再生ピッチ。1.0が等倍、0.5で1オクターブ低く、2.0で1オクターブ高く聞こえる
    bool IsValid() const { return voice_ != nullptr; }

private:
    IXAudio2SourceVoice* voice_ = nullptr;
    friend class AudioEngine;
};

// 音声再生を担当するクラス（XAudio2のラッパー）。EngineクラスがInitialize/Finalizeを呼び出す想定
// 再生したいWAVファイルはresources/Audio/以下に置く
class AudioEngine {
public:
    void Initialize();
    void Finalize();

    // WAVファイルを読み込む（例："resources/Audio/se_hit.wav"）
    SoundData LoadWave(const std::string& filePath);
    // LoadWaveで確保したバッファを解放する。使い終わったサウンドは必ず呼ぶこと
    void Unload(SoundData& soundData);

    // 再生する。loop=trueでループ再生。戻り値のSoundVoiceで後から停止・音量/ピッチ調整ができる。
    // 止めるつもりのない短い音は、あとの始末が要らないPlayOneShotの方を使うこと
    SoundVoice Play(const SoundData& soundData, bool loop = false, float volume = 1.0f);

    // 鳴らしっぱなしにする短い音（効果音）。手元に何も残らない代わりに、
    // 鳴り終わったSourceVoiceはUpdate()が片付ける。
    // pitchは1.0が等倍。同じ音を高さだけ変えて使い回せる
    void PlayOneShot(const SoundData& soundData, float volume = 1.0f, float pitch = 1.0f);

    // 毎フレーム呼ぶ。鳴り終わったSourceVoiceを破棄する。
    //
    // これが無いと、効果音を鳴らすたびにSourceVoiceが積み上がっていく。
    // 効果音を鳴らし続けるゲームでは、放っておくと数千個になり、
    // XAudio2の上限に当たって、あるときから音が出なくなる
    void Update();

private:
    ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;
    std::vector<IXAudio2SourceVoice*> oneShotVoices_; // 鳴らしっぱなしにした音（Updateが片付ける）
};
