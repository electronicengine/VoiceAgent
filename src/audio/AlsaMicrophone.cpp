#include "audio/AlsaMicrophone.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace voice_agent {

namespace {

void AppendLittleEndian16(std::vector<char>& output, std::uint16_t value) {
    output.push_back(static_cast<char>(value & 0xFF));
    output.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void AppendLittleEndian32(std::vector<char>& output, std::uint32_t value) {
    output.push_back(static_cast<char>(value & 0xFF));
    output.push_back(static_cast<char>((value >> 8) & 0xFF));
    output.push_back(static_cast<char>((value >> 16) & 0xFF));
    output.push_back(static_cast<char>((value >> 24) & 0xFF));
}

std::vector<char> BuildWavBytes(const std::vector<char>& pcmData, int sampleRate) {
    constexpr std::uint16_t channelCount = 1;
    constexpr std::uint16_t bitsPerSample = 16;
    constexpr std::uint16_t blockAlign = channelCount * (bitsPerSample / 8);

    const std::uint32_t dataSize = static_cast<std::uint32_t>(pcmData.size());
    const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate) * blockAlign;
    std::vector<char> wavData;
    wavData.reserve(44 + pcmData.size());

    wavData.insert(wavData.end(), {'R', 'I', 'F', 'F'});
    AppendLittleEndian32(wavData, 36u + dataSize);
    wavData.insert(wavData.end(), {'W', 'A', 'V', 'E'});
    wavData.insert(wavData.end(), {'f', 'm', 't', ' '});
    AppendLittleEndian32(wavData, 16);
    AppendLittleEndian16(wavData, 1);
    AppendLittleEndian16(wavData, channelCount);
    AppendLittleEndian32(wavData, static_cast<std::uint32_t>(sampleRate));
    AppendLittleEndian32(wavData, byteRate);
    AppendLittleEndian16(wavData, blockAlign);
    AppendLittleEndian16(wavData, bitsPerSample);
    wavData.insert(wavData.end(), {'d', 'a', 't', 'a'});
    AppendLittleEndian32(wavData, dataSize);
    wavData.insert(wavData.end(), pcmData.begin(), pcmData.end());

    return wavData;
}

std::size_t FramesFromMilliseconds(int sampleRate, int milliseconds) {
    return std::max<std::size_t>(1, static_cast<std::size_t>(sampleRate) * static_cast<std::size_t>(milliseconds) / 1000);
}

int PeakAmplitude(const char* chunkData, std::size_t capturedFrames, std::size_t bytesPerFrame) {
    const auto* samples = reinterpret_cast<const std::int16_t*>(chunkData);
    const std::size_t sampleCount = capturedFrames * bytesPerFrame / sizeof(std::int16_t);

    int maxAmplitude = 0;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        const int amplitude = std::abs(static_cast<int>(samples[index]));
        if (amplitude > maxAmplitude) {
            maxAmplitude = amplitude;
        }
    }

    return maxAmplitude;
}

void AppendChunk(std::vector<char>& destination, const char* chunkData, std::size_t capturedFrames, std::size_t bytesPerFrame) {
    const std::size_t byteCount = capturedFrames * bytesPerFrame;
    destination.insert(destination.end(), chunkData, chunkData + byteCount);
}

void AppendPreRoll(std::vector<char>& preRollData, const char* chunkData, std::size_t capturedFrames, std::size_t bytesPerFrame, std::size_t maxBytes) {
    AppendChunk(preRollData, chunkData, capturedFrames, bytesPerFrame);
    if (preRollData.size() <= maxBytes) {
        return;
    }

    preRollData.erase(preRollData.begin(), preRollData.begin() + static_cast<std::ptrdiff_t>(preRollData.size() - maxBytes));
}

std::size_t MillisecondsFromFrames(std::size_t frameCount, int sampleRate) {
    return frameCount * 1000 / static_cast<std::size_t>(sampleRate);
}

}  // namespace

AlsaMicrophone::AlsaMicrophone(const AppConfig& config)
    : sampleRate_(config.speechSampleRate),
      captureDurationSeconds_(config.captureDurationSeconds),
      vadEnabled_(config.vadEnabled),
      vadFrameMs_(config.vadFrameMs),
      vadStartSpeechMs_(config.vadStartSpeechMs),
      vadEndSilenceMs_(config.vadEndSilenceMs),
      vadMaxCaptureMs_(config.vadMaxCaptureMs),
      vadPreRollMs_(config.vadPreRollMs),
      vadAmplitudeThreshold_(config.vadAmplitudeThreshold),
      deviceName_(config.alsaCaptureDevice) {}

std::vector<char> AlsaMicrophone::CaptureWavBytes() const {
    constexpr unsigned int channelCount = 1;
    constexpr snd_pcm_format_t sampleFormat = SND_PCM_FORMAT_S16_LE;
    constexpr std::size_t bytesPerFrame = channelCount * sizeof(std::int16_t);

    const std::size_t fixedCaptureFrames = static_cast<std::size_t>(sampleRate_) * captureDurationSeconds_;
    const std::size_t vadChunkFrames = FramesFromMilliseconds(sampleRate_, vadFrameMs_);
    const snd_pcm_uframes_t chunkFrames = vadEnabled_
        ? static_cast<snd_pcm_uframes_t>(vadChunkFrames)
        : static_cast<snd_pcm_uframes_t>(1024);
    const std::size_t maxCaptureFrames = vadEnabled_
        ? FramesFromMilliseconds(sampleRate_, vadMaxCaptureMs_)
        : fixedCaptureFrames;
    std::vector<char> pcmData;
    pcmData.reserve(maxCaptureFrames * bytesPerFrame);

    snd_pcm_t* pcmHandle = nullptr;
    snd_pcm_hw_params_t* hardwareParams = nullptr;
    const char* deviceName = deviceName_.empty() ? "default" : deviceName_.c_str();

    int result = snd_pcm_open(&pcmHandle, deviceName, SND_PCM_STREAM_CAPTURE, 0);
    if (result < 0) {
        throw std::runtime_error(std::string("Failed to open ALSA capture device: ") + snd_strerror(result));
    }

    try {
        snd_pcm_hw_params_alloca(&hardwareParams);

        result = snd_pcm_hw_params_any(pcmHandle, hardwareParams);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to query ALSA capture params: ") + snd_strerror(result));
        }

        result = snd_pcm_hw_params_set_access(pcmHandle, hardwareParams, SND_PCM_ACCESS_RW_INTERLEAVED);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to set ALSA capture access mode: ") + snd_strerror(result));
        }

        result = snd_pcm_hw_params_set_format(pcmHandle, hardwareParams, sampleFormat);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to set ALSA capture format: ") + snd_strerror(result));
        }

        result = snd_pcm_hw_params_set_channels(pcmHandle, hardwareParams, channelCount);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to set ALSA capture channels: ") + snd_strerror(result));
        }

        unsigned int configuredRate = static_cast<unsigned int>(sampleRate_);
        result = snd_pcm_hw_params_set_rate_near(pcmHandle, hardwareParams, &configuredRate, nullptr);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to set ALSA capture sample rate: ") + snd_strerror(result));
        }
        if (configuredRate != static_cast<unsigned int>(sampleRate_)) {
            throw std::runtime_error("ALSA capture device could not be configured to the requested sample rate.");
        }

        snd_pcm_uframes_t configuredBufferFrames = chunkFrames;
        result = snd_pcm_hw_params_set_buffer_size_near(pcmHandle, hardwareParams, &configuredBufferFrames);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to set ALSA capture buffer size: ") + snd_strerror(result));
        }

        result = snd_pcm_hw_params(pcmHandle, hardwareParams);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to apply ALSA capture params: ") + snd_strerror(result));
        }

        result = snd_pcm_prepare(pcmHandle);
        if (result < 0) {
            throw std::runtime_error(std::string("Failed to prepare ALSA capture device: ") + snd_strerror(result));
        }

        std::vector<char> chunkBuffer(chunkFrames * bytesPerFrame);
        std::vector<char> preRollData;
        const std::size_t preRollBytes = FramesFromMilliseconds(sampleRate_, vadPreRollMs_) * bytesPerFrame;
        const std::size_t speechStartFrames = FramesFromMilliseconds(sampleRate_, vadStartSpeechMs_);
        const std::size_t trailingSilenceFrames = FramesFromMilliseconds(sampleRate_, vadEndSilenceMs_);
        std::size_t totalCapturedFrames = 0;
        std::size_t consecutiveSpeechFrames = 0;
        std::size_t consecutiveSilenceFrames = 0;
        int sessionPeakAmplitude = 0;
        bool detectedSpeech = false;

        if (vadEnabled_) {
            std::cerr << "[mic] Listening with VAD"
                      << " threshold=" << vadAmplitudeThreshold_
                      << " frameMs=" << vadFrameMs_
                      << " startSpeechMs=" << vadStartSpeechMs_
                      << " endSilenceMs=" << vadEndSilenceMs_
                      << " maxCaptureMs=" << vadMaxCaptureMs_
                      << "\n";
        } else {
            std::cerr << "[mic] Listening with fixed window captureDurationSeconds="
                      << captureDurationSeconds_
                      << "\n";
        }

        while (totalCapturedFrames < maxCaptureFrames) {
            const snd_pcm_uframes_t framesToRead = static_cast<snd_pcm_uframes_t>(
                std::min<std::size_t>(chunkFrames, maxCaptureFrames - totalCapturedFrames)
            );

            result = snd_pcm_readi(pcmHandle, chunkBuffer.data(), framesToRead);
            if (result == -EPIPE) {
                result = snd_pcm_prepare(pcmHandle);
                if (result < 0) {
                    throw std::runtime_error(std::string("Failed to recover ALSA capture overrun: ") + snd_strerror(result));
                }
                continue;
            }
            if (result < 0) {
                throw std::runtime_error(std::string("Failed to read PCM frames from ALSA: ") + snd_strerror(result));
            }

            const std::size_t capturedFrames = static_cast<std::size_t>(result);
            totalCapturedFrames += capturedFrames;

            if (!vadEnabled_) {
                AppendChunk(pcmData, chunkBuffer.data(), capturedFrames, bytesPerFrame);
                continue;
            }

            const int chunkPeakAmplitude = PeakAmplitude(chunkBuffer.data(), capturedFrames, bytesPerFrame);
            sessionPeakAmplitude = std::max(sessionPeakAmplitude, chunkPeakAmplitude);
            const bool chunkHasSpeech = chunkPeakAmplitude >= vadAmplitudeThreshold_;

            if (!detectedSpeech) {
                AppendPreRoll(preRollData, chunkBuffer.data(), capturedFrames, bytesPerFrame, preRollBytes);
                if (chunkHasSpeech) {
                    consecutiveSpeechFrames += capturedFrames;
                    if (consecutiveSpeechFrames >= speechStartFrames) {
                        detectedSpeech = true;
                        pcmData.insert(pcmData.end(), preRollData.begin(), preRollData.end());
                        preRollData.clear();
                        std::cerr << "[mic] Speech started after "
                                  << MillisecondsFromFrames(totalCapturedFrames, sampleRate_)
                                  << " ms peakAmplitude=" << sessionPeakAmplitude
                                  << "\n";
                    }
                } else {
                    consecutiveSpeechFrames = 0;
                }
                continue;
            }

            AppendChunk(pcmData, chunkBuffer.data(), capturedFrames, bytesPerFrame);
            if (chunkHasSpeech) {
                consecutiveSilenceFrames = 0;
            } else {
                consecutiveSilenceFrames += capturedFrames;
                if (consecutiveSilenceFrames >= trailingSilenceFrames) {
                    std::cerr << "[mic] Speech ended due to silence after "
                              << MillisecondsFromFrames(totalCapturedFrames, sampleRate_)
                              << " ms trailingSilenceMs="
                              << MillisecondsFromFrames(consecutiveSilenceFrames, sampleRate_)
                              << " peakAmplitude=" << sessionPeakAmplitude
                              << "\n";
                    break;
                }
            }
        }

        snd_pcm_close(pcmHandle);
        pcmHandle = nullptr;

        if (vadEnabled_ && pcmData.empty()) {
            std::cerr << "[mic] No speech detected before maxCaptureMs="
                      << vadMaxCaptureMs_
                      << " peakAmplitude=" << sessionPeakAmplitude
                      << " threshold=" << vadAmplitudeThreshold_
                      << "\n";
            return {};
        }

        if (vadEnabled_ && totalCapturedFrames >= maxCaptureFrames) {
            std::cerr << "[mic] Capture stopped at maxCaptureMs="
                      << vadMaxCaptureMs_
                      << " peakAmplitude=" << sessionPeakAmplitude
                      << " bytes=" << pcmData.size()
                      << "\n";
        }

        return BuildWavBytes(pcmData, sampleRate_);
    } catch (...) {
        if (pcmHandle != nullptr) {
            snd_pcm_close(pcmHandle);
        }
        throw;
    }
}

}  // namespace voice_agent