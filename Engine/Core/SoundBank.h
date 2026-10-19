#pragma once
#include "AudioEngine.h"
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// SoundBank：名前で引ける音の置き場
//
// ■ なぜこれがあるのか
//
// AudioEngineは「このWAVを鳴らす」までしかしない。読み込んだSoundDataを
// 誰がどこに持っておくのかは決めていないので、そのままではアプリが
// 「ファイルを読む・持っておく・後始末する」を全部自分で書くことになる。
//
// 一方でエンジンの側にも、名前で音を鳴らしたい所がある。
// 部品（コンポーネント）の「触れたら鳴らす」がそれで、
// 部品に書かれた名前（文字列）だけを手がかりに鳴らす必要がある。
// 部品はどのアプリに挿されるか分からないので、音の一覧を知ることはできない。
//
// そこで「名前 → WAVファイル」の対応だけをここに置き、
// 中身（どんな名前でどの音を入れるか）はアプリが決める形にした。
// エンジンは名前で鳴らすことしかせず、音の並びを一切知らない。
//
// ■ 担当すること
//   名前とWAVの対応を覚える／読み込む／鳴らす／後始末する
//   音ごとの音量、全体の音量、曲の音量、消音
//
// ■ 担当しないこと
//   「どの出来事でどの音を鳴らすか」…… アプリの決めごとなので、アプリ側に書く
//   同じ音が重なったときの間引き …… 今は重なるに任せている
//
// ■ 使い方
//
//     engine.Sounds().Register("hit", "resources/Audio/hit.wav");
//     engine.Sounds().Register("bgm", "resources/Audio/bgm.wav", 0.6f);
//     engine.Sounds().LoadAll(engine.Audio());   // Registerし終えてから1回
//
//     engine.Sounds().Play("hit");               // 効果音
//     engine.Sounds().PlayMusic("bgm");          // 曲（ループする。同じ名前なら鳴らし直さない）
//
// Registerした名前は、部品の「触れたら鳴らす」にそのまま打ち込める。
// 読み込めなかった音は、鳴らそうとしても何も起きない（落ちはしない）
// ============================================================================
class SoundBank {
public:
    // ===== 調整用（コードから直接触れるようpublicにしてある）=====
    float masterVolume = 0.8f;
    float musicVolume = 0.5f; // 曲は効果音より下げる。曲が上に来ると効果音を聞き逃す
    bool muted = false;

    // 名前とWAVファイルの対応を登録する。まだ読み込みはしない。
    // volumeScaleは、その音だけの音量（もらった音は録った時点の大きさがまちまちなので、
    // 全体の音量ではなく音ごとに直す）
    void Register(const std::string& name, const std::string& filePath, float volumeScale = 1.0f) {
        Entry& entry = entries_[name];
        entry.filePath = filePath;
        entry.volumeScale = volumeScale;
    }

    // 登録した音をまとめて読み込む。Registerし終えてから1回だけ呼ぶ。
    // 読み込めなかったものは「無い音」として残り、鳴らしても何も起きない
    int LoadAll(AudioEngine& audio) {
        int loaded = 0;
        for (auto& [name, entry] : entries_) {
            if (entry.loaded) { ++loaded; continue; }
            entry.data = audio.LoadWave(entry.filePath);
            entry.loaded = (entry.data.pBuffer != nullptr);
            if (entry.loaded) { ++loaded; }
        }
        audio_ = &audio;
        return loaded;
    }

    void UnloadAll(AudioEngine& audio) {
        StopMusic();
        for (auto& [name, entry] : entries_) {
            if (entry.loaded) { audio.Unload(entry.data); entry.loaded = false; }
        }
        audio_ = nullptr;
    }

    // 効果音を1回鳴らす。名前が無ければ何もしない。
    // volumeScaleは、その場かぎりの上乗せ（遠くで鳴った音を小さくする、などに使う）
    void Play(const std::string& name, float volumeScale = 1.0f, float pitch = 1.0f) {
        if (muted || audio_ == nullptr) { return; }
        Entry* entry = Find(name);
        if (entry == nullptr || !entry->loaded) { return; }
        audio_->PlayOneShot(entry->data, entry->volumeScale * volumeScale * masterVolume, pitch);
    }

    // 曲を流す。すでに同じ名前が流れているなら何もしない
    //（場面ごとに毎フレーム呼んでも鳴らし直しにならないようにしてある）
    void PlayMusic(const std::string& name) {
        if (audio_ == nullptr) { return; }
        if (musicName_ == name && musicVoice_.IsValid()) {
            ApplyVolumes();
            return;
        }
        StopMusic();
        if (muted) { musicName_ = name; return; } // 消音中も「何を流すつもりか」は覚えておく
        Entry* entry = Find(name);
        if (entry == nullptr || !entry->loaded) { return; }
        musicVoice_ = audio_->Play(entry->data, true, entry->volumeScale * musicVolume * masterVolume);
        musicName_ = name;
    }

    void StopMusic() {
        if (musicVoice_.IsValid()) { musicVoice_.Stop(); }
        musicVoice_ = SoundVoice{};
        musicName_.clear();
    }

    // 消音の切り替え。流れている曲にも即座に効かせる
    void SetMuted(bool value) {
        if (muted == value) { return; }
        muted = value;
        if (muted) {
            if (musicVoice_.IsValid()) { musicVoice_.Stop(); musicVoice_ = SoundVoice{}; }
        } else if (!musicName_.empty()) {
            std::string again = musicName_;
            musicName_.clear();
            PlayMusic(again);
        }
    }

    // 全体・曲の音量を変えたあとに呼ぶと、流れている曲にも効く
    void ApplyVolumes() {
        if (musicVoice_.IsValid()) { musicVoice_.SetVolume(muted ? 0.0f : musicVolume * masterVolume); }
    }

    bool Has(const std::string& name) const { return entries_.find(name) != entries_.end(); }
    const std::string& PlayingMusic() const { return musicName_; }

private:
    struct Entry {
        std::string filePath;
        float volumeScale = 1.0f;
        SoundData data{};
        bool loaded = false;
    };

    Entry* Find(const std::string& name) {
        auto found = entries_.find(name);
        return (found == entries_.end()) ? nullptr : &found->second;
    }

    std::unordered_map<std::string, Entry> entries_;
    AudioEngine* audio_ = nullptr;  // LoadAllで受け取る。鳴らすときに使う
    SoundVoice musicVoice_;         // 今流している曲の持ち手（止めるときに使う）
    std::string musicName_;         // 今流している曲の名前（同じものを鳴らし直さないため）
};
