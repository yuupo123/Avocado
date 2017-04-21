#pragma once
#include "device.h"
#include <deque>

namespace device {
namespace cdrom {

class CDROM : public Device {
    union StatusCode {
        enum class Mode { None, Reading, Seeking, Playing };
        struct {
            uint8_t error : 1;
            uint8_t motor : 1;
            uint8_t seekError : 1;
            uint8_t idError : 1;
            uint8_t shellOpen : 1;
            uint8_t read : 1;
            uint8_t seek : 1;
            uint8_t play : 1;
        };
        uint8_t _reg;

        void setMode(Mode mode) {
            error = seekError = idError = false;
            motor = read = seek = play = false;
            if (mode == Mode::Reading) {
                read = true;
            } else if (mode == Mode::Seeking) {
                seek = true;
            } else if (mode == Mode::Playing) {
                play = true;
            }
        }

        void toggleShell() {
            if (!shellOpen) {
                shellOpen = true;
                setMode(Mode::None);
            } else {
                shellOpen = false;
            }
        }

        StatusCode() : _reg(0) {}
    };

    union CDROM_Status {
        struct {
            uint8_t index : 2;
            uint8_t xaFifoEmpty : 1;
            uint8_t parameterFifoEmpty : 1;  // triggered before writing first byte
            uint8_t parameterFifoFull : 1;   // triggered after writing 16 bytes
            uint8_t responseFifoEmpty : 1;   // triggered after reading last byte
            uint8_t dataFifoEmpty : 1;       // triggered after reading last byte
            uint8_t transmissionBusy : 1;
        };
        uint8_t _reg;

        CDROM_Status() : _reg(0x18) {}
    };

    CDROM_Status status;
    uint8_t interruptEnable = 0;
    std::deque<uint8_t> CDROM_params;
    std::deque<uint8_t> CDROM_response;
    std::deque<uint8_t> CDROM_interrupt;

    bool sectorSize = false;  // 0 - 0x800, 1 - 0x924

    void *_cpu = nullptr;
    int readSector = 0;

    StatusCode stat;

    void cmdGetstat();
    void cmdSetloc();
    void cmdPlay();
    void cmdReadN();
    void cmdPause();
    void cmdInit();
    void cmdDemute();
    void cmdSetmode();
    void cmdGetTN();
    void cmdGetTD();
    void cmdSeekL();
    void cmdTest();
    void cmdGetId();
    void cmdReadS();
    void cmdReadTOC();
    void cmdUnlock();
    void handleCommand(uint8_t cmd);

    void writeResponse(uint8_t byte) {
        CDROM_response.push_back(byte);
        status.responseFifoEmpty = 1;
    }

    uint8_t readParam() {
        uint8_t param = CDROM_params.front();
        CDROM_params.pop_front();

        status.parameterFifoEmpty = CDROM_params.empty();
        status.parameterFifoFull = 1;

        return param;
    }

   public:
    CDROM();
    void step() override;
    uint8_t read(uint32_t address) override;
    void write(uint32_t address, uint8_t data) override;

    void setCPU(void *cpu) { this->_cpu = cpu; }

    void toggleShell() { stat.toggleShell(); }
    void ackMoreData() {
        CDROM_interrupt.push_back(1);
        writeResponse(stat._reg);
    }
};
}
}
