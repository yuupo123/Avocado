#include "spu.h"
#include <array>
#include <vector>
#include "sound/adpcm.h"
#include "system.h"

using namespace spu;

SPU::SPU(System* sys) : sys(sys) {
    ram.fill(0);
    audioBufferPos = 0;
}

// Convert normalized float to int16_T
int16_t floatToInt(float val) {
    if (val > 0)
        return val * static_cast<int16_t>(INT16_MAX);
    else
        return -val * static_cast<int16_t>(INT16_MIN);
}

// Convert int16_t to normalized float
float intToFloat(int16_t val) {
    if (val > 0)
        return static_cast<float>(val) / static_cast<float>(INT16_MAX);
    else
        return -static_cast<float>(val) / static_cast<float>(INT16_MIN);
}

float clamp(float val, float min, float max) {
    if (val > max) return max;
    if (val < min) return min;
    return val;
}

void SPU::step() {
    float sumLeft = 0;
    float sumRight = 0;
    for (int v = 0; v < VOICE_COUNT; v++) {
        Voice& voice = voices[v];

        if (voice.state == Voice::State::Off ) continue;

        if (voice.decodedSamples.empty()) {
            auto readAddress = voice.currentAddress._reg * 8;
            if (control.irqEnable && readAddress == irqAddress._reg * 8) {
                SPUSTAT._reg |= 1 << 6;
                sys->interrupt->trigger(interrupt::SPU);
            }
            voice.decodedSamples = ADPCM::decode(&ram[readAddress], voice.prevSample);
        }

        // Modify volume
        float sample = intToFloat(voice.decodedSamples[(int)voice.subAddress]);

        if (voice.adsrWaitCycles > 0) {
            voice.adsrWaitCycles--;
        }

        if (voice.state == Voice::State::Attack) {
            auto adsr = voice.ADSR;
            auto cycles = 1 << std::max(0, adsr.attackShift - 11);
            auto step = (4+adsr.attackStep) << std::max(0, 11-adsr.attackShift);

            // 1 - exponential
            // Direction - always increase
            if (adsr.attackMode == 1 && 1 && voice.ADSRVolume._reg > 0x6000) {
                cycles *= 4;
            }
            if (adsr.attackMode == 1 && 0) {
                step = step * static_cast<float>(voice.ADSRVolume._reg)/0x8000;
            }

            // Wait cycles
            if (voice.adsrWaitCycles == 0) {
                voice.adsrWaitCycles = cycles;
                voice.ADSRVolume._reg = std::min(0x7fff,(int32_t)voice.ADSRVolume._reg + step);

                if (voice.ADSRVolume._reg == 0x7fff) {
                    voice.state = Voice::State::Decay;
                    voice.adsrWaitCycles = 0;
                }
            }
        }
        if (voice.state == Voice::State::Decay) {
            auto adsr = voice.ADSR;
            auto cycles = 1 << std::max(0, adsr.decayShift - 11);
            auto step = (-8) << std::max(0, 11-adsr.decayShift);

            // 1 - always exponential
            // Direction - always decrease
                step = (float)step * static_cast<float>(voice.ADSRVolume._reg)/static_cast<float>(0x8000);


            // Wait cycles
            if (voice.adsrWaitCycles == 0) {
                voice.adsrWaitCycles = cycles;
                voice.ADSRVolume._reg = std::min(0x7fff,(int32_t)voice.ADSRVolume._reg + step);

                if (voice.ADSRVolume._reg <= ((voice.ADSR.sustainLevel+1) * 0x800)) {
                    voice.state = Voice::State::Sustain;
                    voice.adsrWaitCycles = 0;
                }
            }
        }
        if (voice.state == Voice::State::Sustain) {
            auto adsr = voice.ADSR;
            auto cycles = 1 << std::max(0, adsr.sustainShift - 11);
            int step;
            if (adsr.sustainDirection == 1) { // decrease
                step = (-5 - adsr.sustainStep) << std::max(0, 11-adsr.sustainShift);
            }  else { // increase
                step = (4 + adsr.sustainStep) << std::max(0, 11-adsr.sustainShift);
            }

            // 1 - exponential
            // 0 - increase, 1 - decrease
            if (adsr.sustainMode == 1 && adsr.sustainDirection == 0 && voice.ADSRVolume._reg > 0x6000) {
                cycles *= 4;
            }
            if (adsr.sustainMode == 1 && adsr.sustainDirection == 1) {
                step = (float)step * static_cast<float>(voice.ADSRVolume._reg)/static_cast<float>(0x8000);
            }

            // Wait cycles
            if (voice.adsrWaitCycles == 0) {
                voice.adsrWaitCycles = cycles;
                voice.ADSRVolume._reg = std::min(0x7fff,(int32_t)voice.ADSRVolume._reg + step);
            }
        }
        if (voice.state == Voice::State::Release) {
            auto adsr = voice.ADSR;
            auto cycles = 1 << std::max(0, adsr.releaseShift - 11);
            auto step = (-8) << std::max(0, 11-adsr.releaseShift);

            // 1 - exponential
            // 0 - increase, 1 - decrease
            if (adsr.releaseMode == 1) {
                step = (float)step * static_cast<float>(voice.ADSRVolume._reg)/static_cast<float>(0x8000);
            }

            // Wait cycles
            if (voice.adsrWaitCycles == 0) {
                voice.adsrWaitCycles = cycles;
                voice.ADSRVolume._reg = std::min(0x7fff,(int32_t)voice.ADSRVolume._reg + step);
                if ((int16_t)voice.ADSRVolume._reg <= 0) {
                    voice.state = Voice::State::Off;
                }
            }
        }

        sample *= intToFloat(voice.ADSRVolume._reg);

        sumLeft += sample * voice.volume.getLeft();
        sumRight += sample * voice.volume.getRight();

        int prevAddr = voice.subAddress;
        voice.subAddress += (float)std::min((uint16_t)0x4000, voice.sampleRate._reg) / 4096.f;

        if ((int)prevAddr == (int)voice.subAddress) continue;

        if (voice.subAddress >= 28) {
            voice.subAddress -= 28;
            voice.currentAddress._reg += 2;
            voice.decodedSamples.clear();

            if (voice.loadRepeatAddress) {
                voice.loadRepeatAddress = false;
                voice.currentAddress._reg = voice.repeatAddress._reg;
            }
            continue;
        }

        uint8_t flag = ram[voice.currentAddress._reg * 8 + 1];

        if (flag & 4) {  // Loop start
            voice.repeatAddress._reg = voice.currentAddress._reg;
        }


        if (flag & 1) {  // Loop end
            voice.loopEnd = true;
            voice.loadRepeatAddress = true;

            if (!(flag & 2)) {  // Loop repeat
                voice.ADSRVolume._reg = 0;
                voice.state = Voice::State::Release;
            }
            // TODO: address should be copied after playing this block
        }
    }

    sumLeft *= mainVolume.getLeft();
    sumRight *= mainVolume.getRight();

    audioBuffer[audioBufferPos] = floatToInt(clamp(sumLeft, -1.f, 1.f));
    audioBuffer[audioBufferPos + 1] = floatToInt(clamp(sumRight, -1.f, 1.f));

    audioBufferPos += 2;
    if (audioBufferPos >= AUDIO_BUFFER_SIZE) {
        audioBufferPos = 0;
        bufferReady = true;
    }

    if (control.irqEnable && irqAddress._reg < 0x200) {
        static int cnt = 0;

        if (++cnt == 0x100) {
            cnt = 0;
            SPUSTAT._reg |= 1 << 6;
            sys->interrupt->trigger(interrupt::SPU);
        }
    }
}

uint8_t SPU::readVoice(uint32_t address) const {
    int voice = address / 0x10;
    int reg = address % 0x10;

    switch (reg) {
        case 0:
        case 1:
        case 2:
        case 3: return voices[voice].volume.read(reg);

        case 4:
        case 5: return voices[voice].sampleRate.read(reg - 4);

        case 6:
        case 7: return voices[voice].startAddress.read(reg - 6);

        case 8:
        case 9:
        case 10:
        case 11: return voices[voice].ADSR.read(reg - 8);

        case 12:
        case 13:
        return 0;//voices[voice].ADSRVolume.read(reg - 12);

        case 14:
        case 15: return voices[voice].repeatAddress.read(reg - 14);

        default: return 0;
    }
}

void SPU::writeVoice(uint32_t address, uint8_t data) {
    int voice = address / 0x10;
    int reg = address % 0x10;

    switch (reg) {
        case 0:
        case 1:
        case 2:
        case 3:
            voices[voice].volume.write(reg, data);
            if (reg == 3 && voices[voice].volume._reg & 0x80000000) {
                printf("[SPU][WARN] Volume Sweep enabled for voice %d\n", voice);
            }
            return;

        case 4:
        case 5: voices[voice].sampleRate.write(reg - 4, data); return;

        case 6:
        case 7:
            voices[voice].subAddress = 0.f;
            voices[voice].startAddress.write(reg - 6, data);
            return;

        case 8:
        case 9:
        case 10:
        case 11: voices[voice].ADSR.write(reg - 8, data); return;

        case 12:
        case 13: voices[voice].ADSRVolume.write(reg - 12, data); return;

        case 14:
        case 15: voices[voice].repeatAddress.write(reg - 14, data); return;

        default: return;
    }
}

void SPU::voiceKeyOn(int i) {
    Voice& voice = voices[i];

    voice.ADSRVolume._reg = 0;
    voice.repeatAddress._reg = voice.startAddress._reg;
    voice.currentAddress._reg = voice.startAddress._reg;
    voice.state = Voice::State::Attack;
    voice.loopEnd = false;
    voice.loadRepeatAddress = false;
    voice.adsrWaitCycles = 0;
}

void SPU::voiceKeyOff(int i) {
    Voice& voice = voices[i];

    voice.state = Voice::State::Release;
    voice.adsrWaitCycles = 0;
}

uint8_t SPU::read(uint32_t address) {
    address += BASE_ADDRESS;

    if (address >= 0x1f801c00 && address < 0x1f801c00 + 0x10 * VOICE_COUNT) {
        return readVoice(address - 0x1f801c00);
    }

    if (address >= 0x1f801da6 && address <= 0x1f801da7) {  // Data address
        return dataAddress.read(address - 0x1f801da6);
    }

    if (address >= 0x1f801daa && address <= 0x1f801dab) {  // SPUCNT
        // printf("SPUCNT READ 0x%04x\n", SPUCNT._reg);
        return control._byte[address - 0x1f801daa];
    }

    if (address >= 0x1f801d9c && address <= 0x1f801d9f) {  // Voice ON/OFF status (ENDX)
        static Reg32 status;
        if (address == 0x1f801d9c) {
            for (int v = 0; v < VOICE_COUNT; v++) {
                status.setBit(v, voices[v].loopEnd);
            }
        }

        return status._byte[address - 0x1f801d9c];
    }

    if (address >= 0x1f801d98 && address <= 0x1f801d9b) {  // Voice Reverb
        static Reg32 reverb;
        if (address == 0x1f801d98) {
            for (int v = 0; v < VOICE_COUNT; v++) {
                reverb.setBit(v, voices[v].reverb);
            }
        }

        return reverb.read(address - 0x1f801d98);
    }

    if (address >= 0x1f801dac && address <= 0x1f801dad) {  // Data Transfer Control
        return dataTransferControl._byte[address - 0x1f801dac];
    }

    if (address >= 0x1f801dae && address <= 0x1f801daf) {  // SPUSTAT
        SPUSTAT._reg &= 0x0FC0;
        SPUSTAT._reg |= (control._reg & 0x3F);
        return SPUSTAT.read(address - 0x1f801dae);
    }

    //    printf("UNHANDLED SPU READ AT 0x%08x\n", address);

    return 0;
}

void SPU::write(uint32_t address, uint8_t data) {
    address += BASE_ADDRESS;

    if (address >= 0x1f801c00 && address < 0x1f801c00 + 0x10 * VOICE_COUNT) {
        writeVoice(address - 0x1f801c00, data);
        return;
    }

    if (address >= 0x1f801d80 && address <= 0x1f801d83) {  // Main Volume L/R
        mainVolume.write(address - 0x1f801d80, data);
        return;
    }

    if (address >= 0x1f801d84 && address <= 0x1f801d87) {  // Reverb Volume L/R
        reverbVolume.write(address - 0x1f801d84, data);
        return;
    }

    if (address >= 0x1f801d88 && address <= 0x1f801d8b) {  // Voices Key On
        static Reg32 keyOn;

        keyOn.write(address - 0x1f801d88, data);
        if (address == 0x1f801d8b) {
            for (int i = 0; i < VOICE_COUNT; i++) {
                if (keyOn.getBit(i)) voiceKeyOn(i);
            }
        }
        return;
    }

    if (address >= 0x1f801d8c && address <= 0x1f801d8f) {  // Voices Key Off
        static Reg32 keyOff;

        keyOff.write(address - 0x1f801d8c, data);
        if (address == 0x1f801d8c) {
            for (int i = 0; i < VOICE_COUNT; i++) {
                if (keyOff.getBit(i)) voiceKeyOff(i);
            }
        }
        return;
    }

    if (address >= 0x1F801D90 && address <= 0x1F801D93) {  // Pitch modulation enable flags
        static Reg32 pitchModulation;

        pitchModulation.write(address - 0x1F801D90, data);
        if (address == 0x1F801D93) {
            for (int v = 0; v < VOICE_COUNT; v++) {
                voices[v].pitchModulation = pitchModulation.getBit(v);
            }
        }
        return;
    }

    if (address >= 0x1F801D94 && address <= 0x1F801D97) {  // Voice noise mode
        static Reg32 noiseEnabled;
        noiseEnabled.write(address - 0x1F801D94, data);

        if (address == 0x1F801D97) {
            for (int v = 0; v < VOICE_COUNT; v++) {
                voices[v].mode = noiseEnabled.getBit(v) ? Voice::Mode::Noise : Voice::Mode::ADSR;
            }
        }
        return;
    }

    if (address >= 0x1f801d98 && address <= 0x1f801d9b) {  // Voice Reverb
        static Reg32 reverb;
        reverb.write(address - 0x1f801d98, data);

        if (address == 0x1f801d9b) {
            for (int v = 0; v < VOICE_COUNT; v++) {
                voices[v].reverb = reverb.getBit(v);
            }
        }
        return;
    }

    if (address >= 0x1F801DA2 && address <= 0x1F801DA3) {  // Reverb Work area start
        reverbBase.write(address - 0x1F801DA2, data);
        return;
    }

    if (address >= 0x1f801da4 && address <= 0x1f801da5) {  // IRQ Address
        irqAddress.write(address - 0x1f801da4, data);
        return;
    }

    if (address >= 0x1f801da6 && address <= 0x1f801da7) {  // Data address
        dataAddress.write(address - 0x1f801da6, data);
        currentDataAddress = dataAddress._reg * 8;

        sys->interrupt->trigger(interrupt::SPU);
        return;
    }

    if (address >= 0x1f801da8 && address <= 0x1f801da9) {  // Data FIFO
        if (currentDataAddress >= RAM_SIZE) {
            currentDataAddress %= RAM_SIZE;
        }
        ram[currentDataAddress++] = data;
        return;
    }

    if (address >= 0x1f801daa && address <= 0x1f801dab) {  // SPUCNT
        SPUSTAT._reg &= ~(1<<6);
        control._byte[address - 0x1f801daa] = data;
        return;
    }

    if (address >= 0x1f801dac && address <= 0x1f801dad) {  // Data Transfer Control
        dataTransferControl._byte[address - 0x1f801dac] = data;
        return;
    }

    if (address >= 0x1f801dae && address <= 0x1f801daf) {  // SPUSTAT
        // SPUSTAT.write(address - 0x1f801dae, data);
        return;
    }

    if (address >= 0x1f801db0 && address <= 0x1f801db3) {  // CD Volume L/R
        cdVolume.write(address - 0x1f801db0, data);
        return;
    }

    if (address >= 0x1F801DB4 && address <= 0x1F801DB7) {  // External input Volume L/R
        extVolume.write(address - 0x1F801DB4, data);
        return;
    }

    if (address >= 0x1F801DC0 && address <= 0x1F801DFF) {  // Reverb registers
        auto reg = (address - 0x1F801DC0) / 2;
        auto byte = (address - 0x1F801DC0) % 2;
        reverbRegisters[reg].write(byte, data);
        return;
    }

    //    printf("UNHANDLED SPU WRITE AT 0x%08x: 0x%02x\n", address, data);
}

void SPU::dumpRam() {
    std::vector<uint8_t> ram;
    ram.assign(this->ram.begin(), this->ram.end());
    putFileContents("spu.bin", ram);
}