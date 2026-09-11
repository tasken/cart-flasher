#include "../device.h"

#include <stdlib.h>
#include <cstring>

namespace flashcart_core {
using platform::logMessage;
using platform::showProgress;

class AK2i : Flashcart {
protected:
    static const uint8_t ak2i_cmdWaitFlashBusy[8];
    static const uint8_t ak2i_cmdGetHWRevision[8];
    static const uint8_t ak2i_cmdSetMapTableAddress[8];
    static const uint8_t ak2i_cmdActiveFatMap[8];
    static const uint8_t ak2i_cmdUnlockFlash[8];
    static const uint8_t ak2i_cmdLockFlash[8];
    static const uint8_t ak2i_cmdUnlockASIC[8];
    static const uint8_t ak2i_cmdReadFlash[8];
    static const uint8_t ak2i_cmdEraseFlash[8];
    static const uint8_t ak2i_cmdWriteByteFlash[8];
    static const uint8_t ak2i_cmdSetFlash1681_81[8];
    static const uint8_t ak2i_cmdEraseFlash81[8];
    static const uint8_t ak2i_cmdWriteByteFlash81[8];

    static const uint32_t page_size = 0x10000;

    uint32_t m_ak2i_hwrevision;

    bool a2ki_command(const uint8_t (&command)[8], void *response, size_t response_size,
                      uint32_t flags, const char *operation) {
        const ncgc::Err err = m_card->sendCommand(command, response, response_size, flags);
        if (err) {
            logMessage(LOG_ERR, "AK2i: %s failed: %d", operation, err.errNo());
            return false;
        }
        return true;
    }

    bool a2ki_wait_flash_busy() {
        uint32_t state;
        do {
            // I've been trying to get down to the bottom of this delay for a while
            // hopefully soon it will no longer be needed.
            // ioDelay( 16 * 10 );
            if (!a2ki_command(ak2i_cmdWaitFlashBusy, &state, 4, 4, "waitFlashBusy")) {
                return false;
            }
            logMessage(LOG_DEBUG, "AK2i: waitFlashBusy = 0x%08x", state);
        } while ((state & 1) != 0);
        return true;
    }

    bool a2ki_read(uint8_t *outbuf, uint32_t address) {
        uint8_t cmdbuf[8] = {0};
        logMessage(LOG_DEBUG, "AK2i: read(0x%08x)", address);
        memcpy(cmdbuf, ak2i_cmdReadFlash, 8);
        cmdbuf[1] = (address >> 24) & 0xFF;
        cmdbuf[2] = (address >> 16) & 0xFF;
        cmdbuf[3] = (address >>  8) & 0xFF;
        cmdbuf[4] = (address >>  0) & 0xFF;

        if (!a2ki_command(cmdbuf, outbuf, 0x200, 2, "read flash")) {
            logMessage(LOG_ERR, "AK2i: read failed at 0x%08x", address);
            return false;
        }
        // a2ki_wait_flash_busy();
        return true;
    }

    bool a2ki_erase(uint32_t address) {
        uint8_t cmdbuf[8] = {0};

        logMessage(LOG_DEBUG, "AK2i: erase(0x%08x)", address);
        if (m_ak2i_hwrevision == 0x44444444)
        {
            memcpy(cmdbuf, ak2i_cmdEraseFlash, 8);
            cmdbuf[1] = (address >> 16) & 0x1F;
        }
        else if (m_ak2i_hwrevision == 0x81818181)
        {
            memcpy(cmdbuf, ak2i_cmdEraseFlash81, 8);
            cmdbuf[1] = (address >> 16) & 0xFF;
        }

        cmdbuf[2] = (address >>  8) & 0xFF;
        cmdbuf[3] = (address >>  0) & 0xFF;

        if (!a2ki_command(cmdbuf, nullptr, 0,
                          (m_ak2i_hwrevision == 0x81818181) ? 20 : 0, "erase flash")) {
            logMessage(LOG_ERR, "AK2i: erase failed at 0x%08x", address);
            return false;
        }
        if (!a2ki_wait_flash_busy()) {
            logMessage(LOG_ERR, "AK2i: erase poll failed at 0x%08x", address);
            return false;
        }
        return true;
    }

    bool a2ki_writebyte(uint32_t address, uint8_t value) {
        uint8_t cmdbuf[8] = {0};

        logMessage(LOG_DEBUG, "AK2i: write(0x%08x) = 0x%02x", address, value);
        if (m_ak2i_hwrevision == 0x44444444)
        {
            memcpy(cmdbuf, ak2i_cmdWriteByteFlash, 8);
            cmdbuf[1] = (address >> 16) & 0x1F;
        }
        else if (m_ak2i_hwrevision == 0x81818181)
        {
            memcpy(cmdbuf, ak2i_cmdWriteByteFlash81, 8);
            cmdbuf[1] = (address >> 16) & 0xFF;
        }

        cmdbuf[2] = (address >>  8) & 0xFF;
        cmdbuf[3] = (address >>  0) & 0xFF;
        cmdbuf[4] = value;

        if (!a2ki_command(cmdbuf, nullptr, 0, 20, "write flash byte")) {
            logMessage(LOG_ERR, "AK2i: write failed at 0x%08x", address);
            return false;
        }
        if (!a2ki_wait_flash_busy()) {
            logMessage(LOG_ERR, "AK2i: write poll failed at 0x%08x", address);
            return false;
        }
        return true;
    }

public:
    AK2i() : Flashcart("Acekard 2i", "ak2i", 0x200000) { }

    const char *getAuthor() { return "Kitlith, Normmatt, ApacheThunder, tasken"; }
    const char *getDescription() {
        return "Works with:\n"
               " * Acekard 2i HW-44\n"
               " * Acekard 2i HW-81 (2 MiB)\n"
               " * R4i Ultra (r4ultra.com)";
    }

    size_t getMaxLength()
    {
        if (m_ak2i_hwrevision == 0x44444444) return 0x200000;
        // HW-81 has an SST 39VF1681: 16 *megabit*, so 2MB, not the 16MB this
        // used to return (see ak2i_cmdSetFlash1681_81 -- the part was always
        // named right here). Only hit backup/restore, since ntrboot injection
        // never writes past 2MB. writeFlash() doesn't compare before writing,
        // so the extra 14MB just erased and rewrote the same 2MB over and over.
        if (m_ak2i_hwrevision == 0x81818181) return 0x200000;
        return 0x0;
    }

    bool initialize()
    {
        logMessage(LOG_INFO, "AK2i: Init");
        if (!a2ki_command(ak2i_cmdGetHWRevision, &m_ak2i_hwrevision, 4, 0,
                          "get hardware revision")) {
            return false;
        }
        logMessage(LOG_NOTICE, "AK2i: HW Revision = %08x", m_ak2i_hwrevision);

        if (m_ak2i_hwrevision == 0x44444444)
        {
            if (!a2ki_command(ak2i_cmdSetMapTableAddress, nullptr, 0, 0,
                              "set map table address") ||
                !a2ki_command(ak2i_cmdActiveFatMap, nullptr, 4, 0, "activate FAT map") ||
                !a2ki_command(ak2i_cmdUnlockASIC, nullptr, 0, 0, "unlock ASIC")) {
                return false;
            }
        }
        else if (m_ak2i_hwrevision == 0x81818181)
        {
            if (!a2ki_command(ak2i_cmdSetFlash1681_81, nullptr, 0, 20,
                              "select flash 1681") ||
                !a2ki_command(ak2i_cmdActiveFatMap, nullptr, 4, 0, "activate FAT map") ||
                !a2ki_command(ak2i_cmdUnlockFlash, nullptr, 0, 0, "unlock flash") ||
                !a2ki_command(ak2i_cmdUnlockASIC, nullptr, 0, 0, "unlock ASIC") ||
                !a2ki_command(ak2i_cmdSetMapTableAddress, nullptr, 0, 0,
                              "set map table address")) {
                return false;
            }
        } else {
            return false;
        }

        return true;
    }

    void shutdown()
    {
        logMessage(LOG_INFO, "AK2i: Shutdown");
        a2ki_command(ak2i_cmdLockFlash, nullptr, 0, 0, "shutdown lock flash");
        a2ki_command(ak2i_cmdSetMapTableAddress, nullptr, 0, 0,
                     "shutdown set map table address");
        a2ki_command(ak2i_cmdActiveFatMap, nullptr, 4, 4, "shutdown activate FAT map");
    }

    bool readFlash(uint32_t address, uint32_t length, uint8_t *buffer)
    {
        logMessage(LOG_INFO, "AK2i: readFlash(addr=0x%08x, size=0x%x)", address, length);
        if (!a2ki_command(ak2i_cmdLockFlash, nullptr, 0, 0, "read lock flash")) {
            return false;
        }

        if (m_ak2i_hwrevision == 0x81818181 &&
            !a2ki_command(ak2i_cmdSetFlash1681_81, nullptr, 0, 20, "read select flash 1681")) {
            return false;
        }
        if (!a2ki_command(ak2i_cmdSetMapTableAddress, nullptr, 0, 0,
                          "read set map table address")) {
            return false;
        }

        for (uint32_t curpos=0; curpos < length; curpos+=0x200) {
            if (!a2ki_read(buffer + curpos, address + curpos)) {
                return false;
            }
            showProgress(curpos+1,length, "Reading");
        }

        return true;
    }

    bool writeFlash(uint32_t address, uint32_t length, const uint8_t *buffer)
    {
        logMessage(LOG_INFO, "AK2i: writeFlash(addr=0x%08x, size=0x%x)", address, length);
        if (!a2ki_command(ak2i_cmdUnlockFlash, nullptr, 0, 0, "write unlock flash") ||
            !a2ki_command(ak2i_cmdUnlockASIC, nullptr, 0, 0, "write unlock ASIC")) {
            return false;
        }

        if (m_ak2i_hwrevision == 0x81818181 &&
            !a2ki_command(ak2i_cmdSetFlash1681_81, nullptr, 0, 20, "write select flash 1681")) {
            return false;
        }
        if (!a2ki_command(ak2i_cmdSetMapTableAddress, nullptr, 0, 0,
                          "write set map table address")) {
            return false;
        }

        for (uint32_t addr=0; addr < length; addr+=page_size)
        {
            if (!a2ki_erase(address + addr)) {
                return false;
            }

            for (uint32_t i=0; i < page_size; i++) {
                if (!a2ki_writebyte(address + addr + i, buffer[addr + i])) {
                    return false;
                }
                showProgress(addr+i+1,length, "Writing");
            }
        }

        return true;
    }

    bool injectNtrBoot(uint8_t *blowfish_key, uint8_t *firm, uint32_t firm_size)
    {
        // This function follows a read-modify-write cycle:
        //  - Read from flash to prevent accidental erasure of things not overwritten
        //  - Modify the data read, mostly by memcpying data in, perhaps 'encrypting' it first.
        //  - Write the data back to flash, now that we have made our modifications.
        const uint32_t blowfish_adr = 0x80000;
        const uint32_t firm_offset = 0x9E00;
        const uint32_t chipid_offset = 0x1FC0;

        uint32_t buf_size = PAGE_ROUND_UP(firm_offset + firm_size, page_size);
        uint8_t *buf = (uint8_t *)calloc(buf_size, sizeof(uint8_t));

        logMessage(LOG_INFO, "AK2i: Injecting Ntrboot");
        if (!buf) {
            logMessage(LOG_ERR, "AK2i: ntrboot buffer allocation failed");
            return false;
        }
        if (!readFlash(blowfish_adr, buf_size, buf)) {
            free(buf);
            return false;
        }
        memcpy(buf, blowfish_key, 0x1048);
        memcpy(buf + firm_offset, firm, firm_size);

        uint8_t chipid_and_length[8] = {0x00, 0x00, 0x0F, 0xC2, 0x00, 0xB4, 0x17, 0x00};
        memcpy(buf + chipid_offset, chipid_and_length, 8);

        const bool written = writeFlash(blowfish_adr, buf_size, buf);
        free(buf);

        return written;
    }
};

const uint8_t AK2i::ak2i_cmdWaitFlashBusy[8] = {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t AK2i::ak2i_cmdGetHWRevision[8] = {0xD1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t AK2i::ak2i_cmdSetMapTableAddress[8] = {0xD0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t AK2i::ak2i_cmdActiveFatMap[8] = {0xC2, 0x55, 0xAA, 0x55, 0xAA, 0x00, 0x00, 0x00};
const uint8_t AK2i::ak2i_cmdUnlockFlash[8] = {0xC2, 0xAA, 0x55, 0xAA, 0x55, 0x00, 0x00, 0x00};
const uint8_t AK2i::ak2i_cmdLockFlash[8] = {0xC2, 0xAA, 0xAA, 0x55, 0x55, 0x00, 0x00, 0x00};
const uint8_t AK2i::ak2i_cmdUnlockASIC[8] = {0xC2, 0xAA, 0x55, 0x55, 0xAA, 0x00, 0x00, 0x00};
const uint8_t AK2i::ak2i_cmdReadFlash[8] = {0xB7, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00};
const uint8_t AK2i::ak2i_cmdEraseFlash[8] = {0xD4, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
const uint8_t AK2i::ak2i_cmdWriteByteFlash[8] = {0xD4, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
const uint8_t AK2i::ak2i_cmdSetFlash1681_81[8] = {0xD8, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x06};
const uint8_t AK2i::ak2i_cmdEraseFlash81[8] = {0xD4, 0x00, 0x00, 0x00, 0x30, 0x80, 0x00, 0x35};
const uint8_t AK2i::ak2i_cmdWriteByteFlash81[8] = {0xD4, 0x00, 0x00, 0x00, 0x30, 0xa0, 0x00, 0x63};

AK2i ak2i;
}
