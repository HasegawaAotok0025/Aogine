#include "AudioEngine.h"
#include "../Graphics/GraphicsCommon.h" // Log（読み込みに失敗したことを画面のログにも出すため）
#include <fstream>
#include <cassert>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "xaudio2.lib")

void AudioEngine::Initialize() {
    HRESULT hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(hr));
    hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
    assert(SUCCEEDED(hr));
}

void AudioEngine::Finalize() {
    // IXAudio2を捨てればSourceVoiceもまとめて消えるが、控えの方は自分で空にしておく
    for (IXAudio2SourceVoice* voice : oneShotVoices_) {
        if (voice) { voice->Stop(); voice->DestroyVoice(); }
    }
    oneShotVoices_.clear();
    // MasteringVoiceはIXAudio2破棄時に自動的に破棄されるので、明示的なDestroyVoiceは不要
    xAudio2_.Reset();
    masterVoice_ = nullptr;
}

namespace {
    // WAVファイルの各チャンク共通のヘッダ
    struct ChunkHeader {
        char id[4];
        int32_t size;
    };
    // RIFFヘッダ（"RIFF"＋ファイルサイズ＋"WAVE"）
    struct RiffHeader {
        ChunkHeader chunk;
        char type[4];
    };
}

// 読めなければ、中身の無いSoundDataを返す。
//
// 以前はassertで止めていたが、assertはRelease構成では消える。
// 配布した一式から音のファイルが1つ欠けているだけで、読めていないまま先へ進み、
// 出鱈目な長さのバッファを確保して落ちていた。
// 音は「無くても遊べるもの」なので、欠けていたらその音だけ鳴らさずに続ける方がよい
SoundData AudioEngine::LoadWave(const std::string& filePath) {
    SoundData soundData;

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Log("[LoadWave] 開けませんでした: " + filePath + " → この音は鳴らさずに続けます");
        return soundData;
    }

    RiffHeader riff{};
    file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
    if (!file || std::strncmp(riff.chunk.id, "RIFF", 4) != 0 || std::strncmp(riff.type, "WAVE", 4) != 0) {
        Log("[LoadWave] WAVファイルではありません: " + filePath);
        return soundData;
    }

    // "fmt "チャンクを探す（"JUNK"チャンク等が先に来るファイルもあるため、見つかるまで読み飛ばす）
    WAVEFORMATEX wfex{};
    bool foundFormat = false;
    while (file) {
        ChunkHeader chunk{};
        file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));
        if (!file || chunk.size < 0) { break; }
        if (std::strncmp(chunk.id, "fmt ", 4) == 0) {
            // 拡張の付いた形式（WAVE_FORMAT_EXTENSIBLE）はWAVEFORMATEXに収まらない。
            // 収まるぶんだけ読んで、残りは読み飛ばす
            int32_t readable = (std::min)(chunk.size, (int32_t)sizeof(wfex));
            file.read(reinterpret_cast<char*>(&wfex), readable);
            if (chunk.size > readable) { file.seekg(chunk.size - readable, std::ios_base::cur); }
            foundFormat = true;
            break;
        }
        file.seekg(chunk.size, std::ios_base::cur);
    }
    if (!foundFormat) {
        Log("[LoadWave] 形式の情報が見つかりません: " + filePath);
        return soundData;
    }

    // "data"チャンク（実際の音声データ本体）を探す
    ChunkHeader data{};
    bool foundData = false;
    while (file) {
        file.read(reinterpret_cast<char*>(&data), sizeof(data));
        if (!file || data.size < 0) { break; }
        if (std::strncmp(data.id, "data", 4) == 0) { foundData = true; break; }
        file.seekg(data.size, std::ios_base::cur);
    }
    if (!foundData || data.size <= 0) {
        Log("[LoadWave] 音のデータが入っていません: " + filePath);
        return soundData;
    }

    char* buffer = new char[data.size];
    file.read(buffer, data.size);
    std::streamsize actuallyRead = file.gcount();
    file.close();
    if (actuallyRead <= 0) {
        delete[] buffer;
        Log("[LoadWave] 音のデータを読み切れませんでした: " + filePath);
        return soundData;
    }

    soundData.wfex = wfex;
    soundData.pBuffer = reinterpret_cast<BYTE*>(buffer);
    // 書かれている長さより短いファイルもあるので、実際に読めた量を使う
    soundData.bufferSize = static_cast<uint32_t>(actuallyRead);
    return soundData;
}

void AudioEngine::Unload(SoundData& soundData) {
    delete[] soundData.pBuffer;
    soundData.pBuffer = nullptr;
    soundData.bufferSize = 0;
}

SoundVoice AudioEngine::Play(const SoundData& soundData, bool loop, float volume) {
    SoundVoice handle;
    // 読み込めなかった音は、中身の無いSoundDataのまま渡ってくる。
    // ここで弾かないと、鳴らそうとした場所すべてで落ちることになる
    if (!xAudio2_ || !soundData.pBuffer || soundData.bufferSize == 0) { return handle; }

    IXAudio2SourceVoice* sourceVoice = nullptr;
    HRESULT hr = xAudio2_->CreateSourceVoice(&sourceVoice, &soundData.wfex);
    if (FAILED(hr) || !sourceVoice) { return handle; }

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = soundData.pBuffer;
    buffer.AudioBytes = soundData.bufferSize;
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    sourceVoice->SetVolume(volume);
    if (FAILED(sourceVoice->SubmitSourceBuffer(&buffer)) || FAILED(sourceVoice->Start())) {
        sourceVoice->DestroyVoice();
        return handle;
    }

    handle.voice_ = sourceVoice;
    return handle;
}

void AudioEngine::PlayOneShot(const SoundData& soundData, float volume, float pitch) {
    if (!xAudio2_ || !soundData.pBuffer || soundData.bufferSize == 0) { return; }

    IXAudio2SourceVoice* sourceVoice = nullptr;
    if (FAILED(xAudio2_->CreateSourceVoice(&sourceVoice, &soundData.wfex)) || !sourceVoice) { return; }

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = soundData.pBuffer;
    buffer.AudioBytes = soundData.bufferSize;
    buffer.Flags = XAUDIO2_END_OF_STREAM;

    sourceVoice->SetVolume(volume);
    sourceVoice->SetFrequencyRatio((std::max)(0.05f, pitch));
    if (FAILED(sourceVoice->SubmitSourceBuffer(&buffer)) || FAILED(sourceVoice->Start())) {
        sourceVoice->DestroyVoice();
        return;
    }
    oneShotVoices_.push_back(sourceVoice);
}

void AudioEngine::Update() {
    // 積んである順に見て、鳴り終わっているものだけ捨てる。
    // 残す方を前へ詰めていくので、鳴っている音の順番は変わらない
    size_t kept = 0;
    for (IXAudio2SourceVoice* voice : oneShotVoices_) {
        if (!voice) { continue; }
        XAUDIO2_VOICE_STATE state{};
        voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (state.BuffersQueued > 0) { oneShotVoices_[kept++] = voice; continue; }
        voice->DestroyVoice();
    }
    oneShotVoices_.resize(kept);
}

void SoundVoice::Stop() {
    if (voice_) {
        voice_->Stop();
        voice_->DestroyVoice();
        voice_ = nullptr;
    }
}

void SoundVoice::SetVolume(float volume) {
    if (voice_) { voice_->SetVolume(volume); }
}

void SoundVoice::SetPitch(float pitchRatio) {
    if (voice_) { voice_->SetFrequencyRatio(pitchRatio); }
}
