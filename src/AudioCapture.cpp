#include "AudioCapture.h"
#include "Resampler.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

// Ensure GUIDs are defined
#include <initguid.h>
DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);
DEFINE_GUID(IID_IMMDeviceEnumerator,  0xa95664d2, 0x9614, 0x4f35, 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6);
DEFINE_GUID(IID_IAudioClient,         0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2);
DEFINE_GUID(IID_IAudioCaptureClient,  0xc8adbd64, 0xe71e, 0x48a0, 0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17);

namespace whisperflow {

class AudioCapture::Impl {
public:
    explicit Impl(AudioHandler onAudioData)
        : onAudioData_(std::move(onAudioData)) {}

    ~Impl() {
        stopRecording();
    }

    void startRecording() {
        if (isRecording_.load()) {
            return;
        }

        isRecording_.store(true);
        workerThread_ = std::thread(&Impl::captureLoop, this);
    }

    void stopRecording() {
        if (!isRecording_.load()) {
            return;
        }

        isRecording_.store(false);
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
    }

    bool isRecording() const noexcept {
        return isRecording_.load();
    }

private:
    void captureLoop() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool coInitialized = SUCCEEDED(hr);

        IMMDeviceEnumerator* pEnumerator = nullptr;
        IMMDevice* pDevice = nullptr;
        IAudioClient* pAudioClient = nullptr;
        IAudioCaptureClient* pCaptureClient = nullptr;
        WAVEFORMATEX* pWfx = nullptr;
        HANDLE hEvent = nullptr;

        try {
            hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator);
            if (FAILED(hr)) throw std::runtime_error("Failed to create MMDeviceEnumerator (HRESULT: " + std::to_string(static_cast<unsigned long>(hr)) + ")");

            hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
            if (FAILED(hr)) throw std::runtime_error("No default microphone found or access denied (HRESULT: " + std::to_string(static_cast<unsigned long>(hr)) + ")");

            hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&pAudioClient);
            if (FAILED(hr)) throw std::runtime_error("Failed to activate audio client (HRESULT: " + std::to_string(static_cast<unsigned long>(hr)) + ")");

            hr = pAudioClient->GetMixFormat(&pWfx);
            if (FAILED(hr)) throw std::runtime_error("Failed to get audio mix format (HRESULT: " + std::to_string(static_cast<unsigned long>(hr)) + ")");

            // Desired target sample rate for Whisper: 16000 Hz, mono float
            uint32_t inSampleRate = pWfx->nSamplesPerSec;
            WORD channels = pWfx->nChannels;

            // Initialize in shared mode with event callback
            REFERENCE_TIME hnsBufferDuration = 10000000; // 1 second buffer
            hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, hnsBufferDuration, 0, pWfx, nullptr);
            if (FAILED(hr)) throw std::runtime_error("Failed to initialize audio client (HRESULT: " + std::to_string(static_cast<unsigned long>(hr)) + ")");

            hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (!hEvent) throw std::runtime_error("Failed to create capture event handle");

            hr = pAudioClient->SetEventHandle(hEvent);
            if (FAILED(hr)) throw std::runtime_error("Failed to set audio event handle (HRESULT: " + std::to_string(static_cast<unsigned long>(hr)) + ")");

            hr = pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&pCaptureClient);
            if (FAILED(hr)) throw std::runtime_error("Failed to get audio capture client (HRESULT: " + std::to_string(static_cast<unsigned long>(hr)) + ")");

            hr = pAudioClient->Start();
            if (FAILED(hr)) throw std::runtime_error("Failed to start audio capture (HRESULT: " + std::to_string(static_cast<unsigned long>(hr)) + ")");

            Resampler resampler(inSampleRate, 16000);
            std::vector<float> frameBuffer;

            while (isRecording_.load()) {
                DWORD waitResult = WaitForSingleObject(hEvent, 200);
                if (!isRecording_.load()) break;

                if (waitResult == WAIT_OBJECT_0) {
                    UINT32 packetLength = 0;
                    hr = pCaptureClient->GetNextPacketSize(&packetLength);

                    while (SUCCEEDED(hr) && packetLength > 0 && isRecording_.load()) {
                        BYTE* pData = nullptr;
                        UINT32 numFramesToRead = 0;
                        DWORD flags = 0;

                        hr = pCaptureClient->GetBuffer(&pData, &numFramesToRead, &flags, nullptr, nullptr);
                        if (FAILED(hr)) break;

                        if (numFramesToRead > 0) {
                            frameBuffer.clear();
                            frameBuffer.reserve(numFramesToRead);

                            // Determine format and convert to mono float32 [-1.0, 1.0]
                            bool isFloat = (pWfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                                           (pWfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                                            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pWfx)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

                            bool isPcm = (pWfx->wFormatTag == WAVE_FORMAT_PCM) ||
                                         (pWfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                                          reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pWfx)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM);

                            WORD bitsPerSample = pWfx->wBitsPerSample;

                            for (UINT32 i = 0; i < numFramesToRead; ++i) {
                                float monoSample = 0.0f;

                                if (isFloat && bitsPerSample == 32) {
                                    const float* framePtr = reinterpret_cast<const float*>(pData + i * pWfx->nBlockAlign);
                                    float sum = 0.0f;
                                    for (WORD c = 0; c < channels; ++c) {
                                        sum += framePtr[c];
                                    }
                                    monoSample = sum / static_cast<float>(channels);
                                } else if (isPcm && bitsPerSample == 16) {
                                    const int16_t* framePtr = reinterpret_cast<const int16_t*>(pData + i * pWfx->nBlockAlign);
                                    int32_t sum = 0;
                                    for (WORD c = 0; c < channels; ++c) {
                                        sum += framePtr[c];
                                    }
                                    monoSample = static_cast<float>(sum / channels) / 32768.0f;
                                } else if (isPcm && bitsPerSample == 32) {
                                    const int32_t* framePtr = reinterpret_cast<const int32_t*>(pData + i * pWfx->nBlockAlign);
                                    int64_t sum = 0;
                                    for (WORD c = 0; c < channels; ++c) {
                                        sum += framePtr[c];
                                    }
                                    monoSample = static_cast<float>(sum / channels) / 2147483648.0f;
                                }

                                frameBuffer.push_back(monoSample);
                            }

                            auto resampled = resampler.process(frameBuffer.data(), frameBuffer.size());
                            if (!resampled.empty() && onAudioData_) {
                                onAudioData_(resampled);
                            }
                        }

                        pCaptureClient->ReleaseBuffer(numFramesToRead);
                        pCaptureClient->GetNextPacketSize(&packetLength);
                    }
                }
            }

            if (pAudioClient) {
                pAudioClient->Stop();
            }

        } catch (const std::exception& e) {
            std::cerr << "[AudioCapture Error] " << e.what() << '\n';
        }

        // Cleanup resources
        if (hEvent) CloseHandle(hEvent);
        if (pCaptureClient) pCaptureClient->Release();
        if (pAudioClient) pAudioClient->Release();
        if (pWfx) CoTaskMemFree(pWfx);
        if (pDevice) pDevice->Release();
        if (pEnumerator) pEnumerator->Release();

        if (coInitialized) {
            CoUninitialize();
        }

        isRecording_.store(false);
    }

    AudioHandler onAudioData_;
    std::atomic<bool> isRecording_{false};
    std::thread workerThread_;
};

// AudioCapture public API delegation
AudioCapture::AudioCapture(AudioHandler onAudioData)
    : pImpl_(std::make_unique<Impl>(std::move(onAudioData))) {}

AudioCapture::~AudioCapture() = default;

AudioCapture::AudioCapture(AudioCapture&&) noexcept = default;
AudioCapture& AudioCapture::operator=(AudioCapture&&) noexcept = default;

void AudioCapture::startRecording() {
    pImpl_->startRecording();
}

void AudioCapture::stopRecording() {
    pImpl_->stopRecording();
}

bool AudioCapture::isRecording() const noexcept {
    return pImpl_->isRecording();
}

}  // namespace whisperflow
