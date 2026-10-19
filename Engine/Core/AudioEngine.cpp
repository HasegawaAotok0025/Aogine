#include "AudioEngine.h"
#include "../Graphics/GraphicsCommon.h" // Log（読み込みに失敗したことを画面のログにも出すため）
#include <fstream>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

void AudioEngine::Initialize() {
    HRESULT hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(hr));
    hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
    assert(SUCCEEDED(hr));
    // 圧縮された音（mp3など）を展開するMedia Foundation。COMの開始（WinAppInitialize）のあとで呼ぶ
    mediaFoundationStarted_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));
    if (!mediaFoundationStarted_) { Log("[Audio] Media Foundationを始められませんでした。WAV以外の音は鳴らせません"); }
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
    if (mediaFoundationStarted_) { MFShutdown(); mediaFoundationStarted_ = false; }
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

    std::vector<BYTE> buffer(static_cast<size_t>(data.size));
    file.read(reinterpret_cast<char*>(buffer.data()), data.size);
    std::streamsize actuallyRead = file.gcount();
    file.close();
    if (actuallyRead <= 0) {
        Log("[LoadWave] 音のデータを読み切れませんでした: " + filePath);
        return soundData;
    }

    // 書かれている長さより短いファイルもあるので、実際に読めた量に詰める
    buffer.resize(static_cast<size_t>(actuallyRead));
    soundData.wfex = wfex;
    soundData.buffer = std::move(buffer);
    return soundData;
}

// Media Foundationで、ファイルの形式を問わずPCMへ展開して読む。
// 読めなければ（ファイルが無い・形式が分からない）、中身の無いSoundDataを返す（LoadWaveと同じく、鳴らないだけで止まらない）
SoundData AudioEngine::LoadAudio(const std::string& filePath) {
    SoundData soundData;
    if (!mediaFoundationStarted_) { return LoadWave(filePath); } // 展開できないなら、WAVとしてだけ読んでみる

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(ConvertString(filePath).c_str(), nullptr, &reader);
    if (FAILED(hr)) {
        Log("[LoadAudio] 開けませんでした: " + filePath + " → この音は鳴らさずに続けます");
        return soundData;
    }

    // 出てくる中身を「PCMの音」に指定する。mp3などはここで展開されて出てくる
    ComPtr<IMFMediaType> pcmType;
    MFCreateMediaType(&pcmType);
    pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, pcmType.Get());
    if (FAILED(hr)) {
        Log("[LoadAudio] 音として読めない形式です: " + filePath);
        return soundData;
    }

    // 実際に出てくる形式（チャンネル数・サンプリング周波数・ビット数）を受け取る
    ComPtr<IMFMediaType> outputType;
    reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &outputType);
    WAVEFORMATEX* format = nullptr;
    UINT32 formatSize = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(outputType.Get(), &format, &formatSize);
    if (FAILED(hr) || !format) {
        Log("[LoadAudio] 音の形式を受け取れませんでした: " + filePath);
        return soundData;
    }
    soundData.wfex = *format;
    CoTaskMemFree(format);
    // 拡張付きの形式（WAVE_FORMAT_EXTENSIBLE）で返ることがある。中身はただのPCMなので、
    // 拡張部分を持たない普通のPCMとして名乗り直す（WAVEFORMATEXには拡張部分の置き場が無い）
    soundData.wfex.wFormatTag = WAVE_FORMAT_PCM;
    soundData.wfex.cbSize = 0;

    // 最後まで読み、つなげて1本のPCMにする
    std::vector<BYTE> pcm;
    while (true) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) { break; }
        if (!sample) { continue; }

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) { continue; }
        BYTE* data = nullptr;
        DWORD length = 0;
        if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
            pcm.insert(pcm.end(), data, data + length);
            buffer->Unlock();
        }
    }
    if (pcm.empty()) {
        Log("[LoadAudio] 音のデータが入っていません: " + filePath);
        return soundData;
    }
    soundData.buffer = std::move(pcm);
    return soundData;
}

void AudioEngine::Unload(SoundData& soundData) {
    soundData.buffer.clear();
    soundData.buffer.shrink_to_fit(); // clearだけでは確保した領域が残る
}

SoundVoice AudioEngine::Play(const SoundData& soundData, bool loop, float volume) {
    SoundVoice handle;
    // 読み込めなかった音は、中身の無いSoundDataのまま渡ってくる。
    // ここで弾かないと、鳴らそうとした場所すべてで落ちることになる
    if (!xAudio2_ || !soundData.IsLoaded()) { return handle; }

    IXAudio2SourceVoice* sourceVoice = nullptr;
    HRESULT hr = xAudio2_->CreateSourceVoice(&sourceVoice, &soundData.wfex);
    if (FAILED(hr) || !sourceVoice) { return handle; }

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = soundData.buffer.data();
    buffer.AudioBytes = static_cast<UINT32>(soundData.buffer.size());
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
    if (!xAudio2_ || !soundData.IsLoaded()) { return; }

    IXAudio2SourceVoice* sourceVoice = nullptr;
    if (FAILED(xAudio2_->CreateSourceVoice(&sourceVoice, &soundData.wfex)) || !sourceVoice) { return; }

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = soundData.buffer.data();
    buffer.AudioBytes = static_cast<UINT32>(soundData.buffer.size());
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
