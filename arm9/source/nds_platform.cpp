#include "nds_platform.h"

#include <nds.h>
#include <nds/arm9/dldi.h> // dldiGetMode(), io_dldi_data -- not pulled in by nds.h
#include <fat.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <new>

#include <sys/stat.h>
#include <unistd.h>

#include "banner_ops.h"
#include "device.h"
#include "ui.h"
#include "blowfish_cartridge_ntr_bin.h"
#include "blowfish_ntrboot_ctr-D_bin.h"
#include "blowfish_ntrboot_ctr-R_bin.h"

int progressCount = 0;
static bool suppressDriverProgress = false;

namespace {

return_codes_t BannerValidationResult(banner_ops::SourceValidation validation) {
	if (validation == banner_ops::SourceValidation::WrongSize) {
		return BANNER_SIZE_INVALID;
	}
	return validation == banner_ops::SourceValidation::WrongVersion
		? BANNER_VERSION_INVALID : BANNER_CRC_INVALID;
}

void CreateBackupDirectories() {
	mkdir("/cart-backups", 0777);
	mkdir("/cart-backups/banners", 0777);
}

bool BuildBackupPath(char *path, size_t pathSize, const char *directory,
	const char *cartName, const char *kind, unsigned int suffix = 0) {
	const int written = suffix == 0
		? snprintf(path, pathSize, "%s/%s-%s.bin", directory, cartName, kind)
		: snprintf(path, pathSize, "%s/%s-%s-%u.bin",
			directory, cartName, kind, suffix);
	return written >= 0 && static_cast<size_t>(written) < pathSize;
}

void ClearStreamProgress() {
	SetProgressOverride(0, 0);
	SetProgressStatusOverride(nullptr);
}

return_codes_t FinishBannerBackup(uint8_t *banner, return_codes_t result) {
	delete[] banner;
	SetProgressStatusOverride(nullptr);
	return result;
}

} // namespace

bool file_exists(const char* filename) {
	return access(filename, F_OK) == 0;
}

return_codes_t mount_fat(void) {
	if(!file_exists("sd:/") && !file_exists("fat:/")) {
		if (!fatInitDefault()) {
			return FAT_MOUNT_FAILED;
		}
	}
	CreateBackupDirectories();
	return ALL_OK;
}

return_codes_t unmount_fat(void) {
	return ALL_OK;
}

void SetDriverProgressSuppressed(bool suppressed) {
	suppressDriverProgress = suppressed;
	if (suppressed) {
		progressCount = 0;
	}
}

namespace flashcart_core {
	namespace platform {
		void showProgress(std::uint32_t current, std::uint32_t total, const char *status) {
			if (suppressDriverProgress) {
				return;
			}
			if (progressCount < 1000) {
				progressCount++;
				return;
			}
			else {
				progressCount = 0;
			}
			ShowProgress(BOTTOM_SCREEN, current, total, status);
		}

		// Opens /cart-backups/cart_flasher.log for one or more writeLogLine()
		// calls sharing a
		// single open/close bracket. logMessage() below opens/closes once per
		// call, which is fine for isolated calls -- but LogHardwareProbe()'s
		// three related lines land close enough together that BlocksDS's FatFs
		// hadn't finished committing one fclose()'s file size before the very
		// next fopen("a") reopened the same path microseconds later: that write
		// landed at a stale (too-small) offset and stomped part of the previous
		// line's own tail bytes. Confirmed via hexdump -- a repeated
		// hardware-probe dump left "0262" and "AUXSPIC" fragments of the prior
		// write's tail sitting mid-file, immediately followed by unrelated
		// content with no newline between them. Batching into one bracket
		// removes the reopen between them entirely.
		static FILE *openLogFileForAppend()
		{
			// FAT stays mounted for the program's lifetime (main() already
			// mounted it, unmount_fat() is a no-op) -- avoids redundant
			// access()+mkdir() on every log call.
			static bool mounted = false;
			if (!mounted) {
				if (mount_fat() != ALL_OK) { return nullptr; }
				mounted = true;
			}

			// Must close on every call: on BlocksDS's FatFs, a file's size is
			// only committed by fclose() -- fflush()/fsync() didn't do it in
			// testing. Keeping the file open (tried first) left the log's
			// reported size at 0 bytes forever.
			static bool first_open = true;
			FILE *logfile = fopen("/cart-backups/cart_flasher.log",
				first_open ? "w" : "a");
			if (logfile) { first_open = false; }
			return logfile;
		}

		static int writeLogLineV(FILE *logfile, log_priority priority, const char *fmt,
			va_list args, bool force)
		{
			if (!force && priority < global_loglevel) { return 0; }

			const char *priority_str;
			switch (priority) {
				case LOG_DEBUG: priority_str = "DEBUG"; break;
				case LOG_INFO: priority_str = "INFO"; break;
				case LOG_NOTICE: priority_str = "NOTICE"; break;
				case LOG_WARN: priority_str = "WARN"; break;
				case LOG_ERR: priority_str = "ERROR"; break;
				default: priority_str = "UNKNOWN"; break;
			}

			char string_to_write[100]; //just do 100, should be enough for any kind of log message we get...
			snprintf(string_to_write, sizeof(string_to_write), "[%s]: %s\n", priority_str, fmt);

			return vfprintf(logfile, string_to_write, args);
		}

		static int writeLogLineForced(FILE *logfile, log_priority priority,
			const char *fmt, ...)
		{
			va_list args;
			va_start(args, fmt);
			int result = writeLogLineV(logfile, priority, fmt, args, true);
			va_end(args);
			return result;
		}

		int logMessage(log_priority priority, const char *fmt, ...)
		{
			if (priority < global_loglevel) { return 0; }

			FILE *logfile = openLogFileForAppend();
			if (!logfile) { return -1; }

			va_list args;
			va_start(args, fmt);
			int result = writeLogLineV(logfile, priority, fmt, args, false);
			va_end(args);

			fclose(logfile);
			return result;
		}

		auto getBlowfishKey(BlowfishKey key) -> const std::uint8_t(&)[0x1048]
		{
			switch (key) {
				default:
				case BlowfishKey::NTR:
					return *static_cast<const std::uint8_t(*)[0x1048]>(static_cast<const void *>(blowfish_cartridge_ntr_bin));
				case BlowfishKey::B9Retail:
					return *static_cast<const std::uint8_t(*)[0x1048]>(static_cast<const void *>(blowfish_ntrboot_ctr_R_bin));
				case BlowfishKey::B9Dev:
					return *static_cast<const std::uint8_t(*)[0x1048]>(static_cast<const void *>(blowfish_ntrboot_ctr_D_bin));
			}
		}
	}
}

// Hardware-state probe, opened on demand from the cart list and always logged
// when the SD is available. The timer measurement is CPU-speed-independent:
// it ticks at the fixed 33.5MHz bus clock, so ~8200 ticks means 67MHz, ~4100
// means 134MHz, regardless of whether SCFG is readable. EXMEMCNT bit 11 set
// means the ARM7 owns Slot-1, the BlocksDS DLDI-on-ARM7 failure mode that
// broke DS-mode detection.
//
// Goes to the log *and* the screen: a cart that won't detect leaves a
// readable log, but an SD card that won't mount leaves none at all.
void LogHardwareProbe(int firstRow)
{
	TIMER0_CR = 0;
	TIMER0_DATA = 0;
	TIMER0_CR = TIMER_ENABLE | TIMER_DIV_256;
	ncgc::delay(0x200000);
	TIMER0_CR = 0;
	u16 ticks = TIMER0_DATA;

	// EXMEMCNT bit 11 is spelled out rather than left as hex: it decides who owns
	// Slot-1, and it has explained every cart-detection failure so far.
	const bool arm9OwnsCart = !(REG_EXMEMCNT & 0x800);
	// DLDI: we force ARM9 so the ARM7 can't take Slot-1 out from under libncgc,
	// but that only works if the driver tolerates it. If the SD won't mount, the
	// mode and the ARM7_CAPABLE flag are the first things to look at.
	const DLDI_MODE mode = dldiGetMode();
	const bool arm7Capable = io_dldi_data
		&& (io_dldi_data->ioInterface.features & FEATURE_ARM7_CAPABLE);
	const char *const dldiModeStr =
		mode == DLDI_MODE_ARM9 ? "ARM9" : mode == DLDI_MODE_ARM7 ? "ARM7" : "auto";
	const char *const dldiName = io_dldi_data ? io_dldi_data->friendlyName : "(none)";

	// The log carries every field the screen shows, but packed for a log: dense,
	// one grep-friendly line per group, no colours. An on-demand probe must log
	// even when DEBUG is not selected; only an unavailable SD prevents it.
	//
	// These three lines share a single open/close bracket (not three separate
	// logMessage() calls) -- see openLogFileForAppend()'s comment for why:
	// reopening the same path in quick succession raced against BlocksDS's
	// FatFs file-size commit timing and corrupted each write's tail into the
	// next.
	FILE *probeLogFile = flashcart_core::platform::openLogFileForAppend();
	if (probeLogFile) {
		flashcart_core::platform::writeLogLineForced(probeLogFile, flashcart_core::LOG_DEBUG,
			"probe: dsi=%d SCFG_CLK=0x%04X SCFG_EXT=0x%08lX delayTicks=%u",
			isDSiMode(), REG_SCFG_CLK, (unsigned long)REG_SCFG_EXT, ticks);
		flashcart_core::platform::writeLogLineForced(probeLogFile, flashcart_core::LOG_DEBUG,
			"probe: EXMEMCNT=0x%04X ROMCTRL=0x%08lX AUXSPICNT=0x%04X cart=%s",
			REG_EXMEMCNT, (unsigned long)REG_ROMCTRL, REG_AUXSPICNT,
			arm9OwnsCart ? "ARM9" : "ARM7");
		flashcart_core::platform::writeLogLineForced(probeLogFile, flashcart_core::LOG_DEBUG,
			"probe: DLDI=%s arm7capable=%d name=%s", dldiModeStr, arm7Capable, dldiName);
		fclose(probeLogFile);
	}

	// The screen keeps its own layout: one field per line, colour-coded, spaced
	// for glancing at -- photograph it when there is no SD to log to.
	DrawStringF(BOTTOM_SCREEN, FONT_WIDTH, (firstRow + 0) * FONT_HEIGHT, COLOR_WHITE,
		"dsi=%d  clk=0x%04X  ticks=%u", isDSiMode(), REG_SCFG_CLK, ticks);
	DrawStringF(BOTTOM_SCREEN, FONT_WIDTH, (firstRow + 1) * FONT_HEIGHT, COLOR_WHITE,
		"SCFG_EXT=0x%08lX", (unsigned long)REG_SCFG_EXT);
	DrawStringF(BOTTOM_SCREEN, FONT_WIDTH, (firstRow + 2) * FONT_HEIGHT, arm9OwnsCart ? COLOR_GREEN : COLOR_RED,
		"EXMEMCNT=0x%04X  cart: %s", REG_EXMEMCNT, arm9OwnsCart ? "ARM9" : "ARM7");
	DrawStringF(BOTTOM_SCREEN, FONT_WIDTH, (firstRow + 3) * FONT_HEIGHT, COLOR_WHITE,
		"ROMCTRL=0x%08lX", (unsigned long)REG_ROMCTRL);
	DrawStringF(BOTTOM_SCREEN, FONT_WIDTH, (firstRow + 4) * FONT_HEIGHT, COLOR_WHITE,
		"AUXSPICNT=0x%04X", REG_AUXSPICNT);
	DrawStringF(BOTTOM_SCREEN, FONT_WIDTH, (firstRow + 5) * FONT_HEIGHT, COLOR_WHITE,
		"DLDI on %s, arm7capable=%d", dldiModeStr, arm7Capable);
	DrawStringF(BOTTOM_SCREEN, FONT_WIDTH, (firstRow + 6) * FONT_HEIGHT, COLOR_WHITE,
		"DLDI: %s", dldiName);
}

// Shared by DumpFlash()/WriteFlash() -- both stream the cart's flashrom to or
// from a file in the same 64 KiB-chunk shape (open, loop with a progress bar,
// close), the only real differences being which direction the bytes flow and
// the wording shown on screen while it happens. isRead selects "reading FROM
// the cart, writing TO the file" (DumpFlash's direction) vs "reading FROM the
// file, writing TO the cart" (WriteFlash's); whichever side is the flash chip
// maps to FLASH_OP_FAILED on failure, whichever side is the file maps to
// FILE_IO_FAILED, regardless of which direction that is.
static return_codes_t StreamFlash(flashcart_core::Flashcart* cart, const char* filepath, bool isRead)
{
	u32 Flash_size = cart->getMaxLength(); //Get the flashrom size
	// AK2i, DSTT, R4i Gold 3DS, and R4 SDHC Dual-Core erase/program in 64KB
	// units. Smaller app chunks either make their drivers read past the buffer
	// or erase a previously written half-block, so every stream chunk must
	// cover at least one complete driver page.
	const u32 chunkSize = 0x10000;

	// Cleared up front, not just before the progress loop: every early return
	// below (bad SD card, no memory, can't open the file, file too small)
	// used to leave the confirm/combo screen behind it, so menu.cpp's error
	// message at row 15 landed right next to a stale, now-dead "<A>.../<B>..."
	// prompt from the screen before it.
	DrawRectangle(TOP_SCREEN, 0, 2 * FONT_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - 2 * FONT_HEIGHT, COLOR_BLACK);

	if (mount_fat() != ALL_OK) { return FAT_MOUNT_FAILED; }
	if (Flash_size == 0) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"StreamFlash: cart has no restore-supported flash capacity");
		return FLASH_OP_FAILED;
	}

	// Allocated before fopen(), not after: for isRead (DumpFlash), the fopen()
	// below uses "wb", which truncates any pre-existing backup file the moment
	// it succeeds -- regardless of whether anything after it fails. Checking
	// the buffer first means a MEM_ALLOC_FAILED never costs the user their
	// previous backup (verified via agy's independent review, .brain/
	// streamflash-check-order-review.md -- flagged this exact ordering as a
	// real, if low-probability, data-loss regression from the original
	// DumpFlash(), which allocated first for the same reason).
	u8 *chunkBuffer = new(std::nothrow) u8[chunkSize];
	if (!chunkBuffer) {
		return MEM_ALLOC_FAILED;
	}
	FILE *file = fopen(filepath, isRead ? "wb" : "rb");
	if (!file) {
		delete[] chunkBuffer;
		return FILE_OPEN_FAILED;
	}
	auto closeStream = [&]() {
		delete[] chunkBuffer;
		return fclose(file);
	};

	if (!isRead) {
		// Validate size before touching the cart -- streaming would otherwise only
		// notice a truncated file mid-loop, aborting with the firmware half overwritten.
		if (fseek(file, 0, SEEK_END) != 0) {
			flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
				"StreamFlash: couldn't seek selected image %s", filepath);
			closeStream();
			return FILE_IO_FAILED;
		}
		long fileSize = ftell(file);
		if (fseek(file, 0, SEEK_SET) != 0) {
			flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
				"StreamFlash: couldn't rewind selected image %s", filepath);
			closeStream();
			return FILE_IO_FAILED;
		}
		if (fileSize < 0) {
			flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
				"StreamFlash: couldn't determine size of selected image %s", filepath);
			closeStream();
			return FILE_IO_FAILED;
		}
		if ((u32)fileSize < Flash_size) {
			flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
				"StreamFlash: expected at least %lu bytes, got %ld from %s",
				static_cast<unsigned long>(Flash_size), fileSize, filepath);
			closeStream();
			return FLASH_IMAGE_INVALID;
		}
	}

	const char *headerText = isRead ? "Backing up flashrom..." : "Writing flashrom...";
	const char *addrVerb = isRead ? "Reading" : "Writing";
	const char *progressLabel = isRead ? "Reading flash" : "Writing flash";

	DrawString(TOP_SCREEN, FONT_WIDTH, 2 * FONT_HEIGHT, COLOR_WHITE, headerText);
	DrawString(TOP_SCREEN, FONT_WIDTH, 3 * FONT_HEIGHT, COLOR_WHITE,
		"Do not power off or remove the cart.");

	progressCount = 0; // start the driver-side draw throttle from a known phase
	SetProgressStatusOverride(progressLabel);
	ShowProgress(BOTTOM_SCREEN, 0, Flash_size, progressLabel);
	auto abortStream = [&](return_codes_t result) {
		closeStream();
		ClearStreamProgress();
		return result;
	};

	for (u32 chunkOffset = 0; chunkOffset < Flash_size; chunkOffset += chunkSize) {
		SetProgressOverride(chunkOffset, Flash_size);
		char addrStr[64];
		sprintf(addrStr, "%s address: 0x%08lX", addrVerb, chunkOffset);
		DrawRectangle(TOP_SCREEN, FONT_WIDTH, 4 * FONT_HEIGHT, SCREEN_WIDTH - (2 * FONT_WIDTH), FONT_HEIGHT, COLOR_BLACK);
		DrawString(TOP_SCREEN, FONT_WIDTH, 4 * FONT_HEIGHT, COLOR_WHITE, addrStr);

		u32 currentChunkSize = std::min(chunkSize, Flash_size - chunkOffset);

		if (isRead) {
			if (!cart->readFlash(chunkOffset, currentChunkSize, chunkBuffer)) {
				return abortStream(FLASH_OP_FAILED);
			}
			if (fwrite(chunkBuffer, 1, currentChunkSize, file) != currentChunkSize) {
				return abortStream(FILE_IO_FAILED);
			}
		} else {
			if (fread(chunkBuffer, 1, currentChunkSize, file) != currentChunkSize) {
				return abortStream(FILE_IO_FAILED);
			}
			if (!cart->writeFlash(chunkOffset, currentChunkSize, chunkBuffer)) {
				return abortStream(FLASH_OP_FAILED);
			}
		}

		SetProgressOverride(0, 0); // Reset override before drawing absolute progress
		ShowProgress(BOTTOM_SCREEN, chunkOffset + currentChunkSize, Flash_size, progressLabel);
	}
	const int closeResult = closeStream();
	if (closeResult != 0) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"StreamFlash: couldn't finish %s", filepath);
		ClearStreamProgress();
		return FILE_IO_FAILED;
	}

	SetProgressOverride(0, 0); // Reset override before final absolute progress
	ShowProgress(BOTTOM_SCREEN, Flash_size, Flash_size, progressLabel);
	SetProgressStatusOverride(nullptr);

	return ALL_OK;
}

return_codes_t DumpFlash(flashcart_core::Flashcart* cart)
{
	char path[128];
	if (!BuildBackupPath(path, sizeof(path), "/cart-backups",
			cart->getShortName(), "backup")) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"DumpFlash: couldn't create a backup path");
		return FILE_OPEN_FAILED;
	}
	return StreamFlash(cart, path, true);
}

static bool BannerBackupPath(const char *cartName, char *path, size_t pathSize) {
	if (!BuildBackupPath(path, pathSize, "/cart-backups/banners",
			cartName, "banner")) {
		return false;
	}
	if (!file_exists(path)) {
		return true;
	}
	for (unsigned int suffix = 2; suffix <= 99; ++suffix) {
		if (!BuildBackupPath(path, pathSize, "/cart-backups/banners",
				cartName, "banner", suffix)) {
			return false;
		}
		if (!file_exists(path)) {
			return true;
		}
	}
	return false;
}

return_codes_t DumpBanner(flashcart_core::Flashcart* cart)
{
	if (!banner_ops::HasAvailableOperation(cart)) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"DumpBanner: cart has no valid banner backup profile");
		return FLASH_OP_FAILED;
	}
	if (mount_fat() != ALL_OK) { return FAT_MOUNT_FAILED; }
	DrawRectangle(TOP_SCREEN, 0, 2 * FONT_HEIGHT, SCREEN_WIDTH,
		SCREEN_HEIGHT - 2 * FONT_HEIGHT, COLOR_BLACK);
	DrawString(TOP_SCREEN, FONT_WIDTH, 2 * FONT_HEIGHT, COLOR_WHITE,
		"Backing up and validating the DS banner...");
	progressCount = 0;
	SetProgressStatusOverride("Backing up DS banner");
	ShowProgress(BOTTOM_SCREEN, 0, 1, "Backing up DS banner");

	uint8_t* const banner = new(std::nothrow) uint8_t[banner_ops::kSourceBannerSize];
	if (!banner) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"DumpBanner: banner buffer allocation failed");
		return FinishBannerBackup(banner, MEM_ALLOC_FAILED);
	}
	if (!banner_ops::ReadBanner(cart, banner, banner_ops::kSourceBannerSize)) {
		return FinishBannerBackup(banner, FLASH_OP_FAILED);
	}

	const banner_ops::SourceValidation validation = banner_ops::ValidateSourceBanner(
		banner, banner_ops::kSourceBannerSize);
	if (validation != banner_ops::SourceValidation::Valid) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"DumpBanner: driver returned an invalid v1 banner");
		return FinishBannerBackup(banner, BannerValidationResult(validation));
	}

	char path[128];
	if (!BannerBackupPath(cart->getShortName(), path, sizeof(path))) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"DumpBanner: couldn't choose a new backup path");
		return FinishBannerBackup(banner, FILE_OPEN_FAILED);
	}
	FILE* file = fopen(path, "wb");
	if (!file) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"DumpBanner: couldn't create %s", path);
		return FinishBannerBackup(banner, FILE_OPEN_FAILED);
	}
	const bool wrote = fwrite(banner, 1, banner_ops::kSourceBannerSize, file)
		== banner_ops::kSourceBannerSize;
	const int closeResult = fclose(file);
	if (!wrote || closeResult != 0) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"DumpBanner: couldn't finish %s", path);
		remove(path);
		return FinishBannerBackup(banner, FILE_IO_FAILED);
	}
	flashcart_core::platform::logMessage(flashcart_core::LOG_NOTICE,
		"DumpBanner: saved validated v1 banner to %s", path);
	ShowProgress(BOTTOM_SCREEN, 1, 1, "Backing up DS banner");
	return FinishBannerBackup(banner, ALL_OK);
}

return_codes_t ValidateFlashImage(flashcart_core::Flashcart* cart, const char* filepath)
{
	const size_t flashSize = cart->getMaxLength();
	if (flashSize == 0) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"FlashImage: cart has no restore-supported flash capacity");
		return FLASH_OP_FAILED;
	}

	if (mount_fat() != ALL_OK) { return FAT_MOUNT_FAILED; }
	flashcart_core::platform::logMessage(flashcart_core::LOG_NOTICE,
		"FlashImage: selected %s", filepath);

	FILE* file = fopen(filepath, "rb");
	if (!file) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"FlashImage: couldn't open %s", filepath);
		return FILE_OPEN_FAILED;
	}
	if (fseek(file, 0, SEEK_END) != 0) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"FlashImage: couldn't seek %s", filepath);
		fclose(file);
		return FILE_IO_FAILED;
	}
	const long fileSize = ftell(file);
	if (fileSize < 0) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"FlashImage: couldn't determine size of %s", filepath);
		fclose(file);
		return FILE_IO_FAILED;
	}
	if (static_cast<size_t>(fileSize) < flashSize) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"FlashImage: expected at least %lu bytes, got %ld",
			static_cast<unsigned long>(flashSize), fileSize);
		fclose(file);
		return FLASH_IMAGE_INVALID;
	}
	if (fclose(file) != 0) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"FlashImage: couldn't finish %s", filepath);
		return FILE_IO_FAILED;
	}

	// Some supported carts have historical oversized backups. Their leading
	// flashSize bytes remain a valid restore image, so only reject truncation.
	flashcart_core::platform::logMessage(flashcart_core::LOG_NOTICE,
		"FlashImage: accepted %ld-byte image for %lu-byte flash",
		fileSize, static_cast<unsigned long>(flashSize));
	return ALL_OK;
}

return_codes_t WriteFlash(flashcart_core::Flashcart* cart, const char* filepath)
{
	return StreamFlash(cart, filepath, false);
}

static return_codes_t LoadBannerFile(flashcart_core::Flashcart* cart,
	const char* filepath, const char* phase, uint8_t** outBanner, uint32_t* outBannerSize)
{
	*outBanner = nullptr;
	*outBannerSize = 0;
	if (!banner_ops::HasAvailableOperation(cart)) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"BannerFile[%s]: cart has no valid banner-write profile", phase);
		return FLASH_OP_FAILED;
	}
	const uint32_t bannerSize = banner_ops::kSourceBannerSize;

	if (mount_fat() != ALL_OK) { return FAT_MOUNT_FAILED; }
	flashcart_core::platform::logMessage(flashcart_core::LOG_NOTICE,
		"BannerFile[%s]: selected %s", phase, filepath);

	FILE* file = fopen(filepath, "rb");
	if (!file) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"BannerFile[%s]: couldn't open %s", phase, filepath);
		return FILE_OPEN_FAILED;
	}

	if (fseek(file, 0, SEEK_END) != 0) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"BannerFile[%s]: couldn't seek %s", phase, filepath);
		fclose(file);
		return FILE_IO_FAILED;
	}
	const long fileSize = ftell(file);
	if (fileSize < 0 || static_cast<uint32_t>(fileSize) != bannerSize) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"BannerFile[%s]: expected %lu bytes, got %ld",
			phase, static_cast<unsigned long>(bannerSize), fileSize);
		fclose(file);
		return BANNER_SIZE_INVALID;
	}

	uint8_t* const banner = new(std::nothrow) uint8_t[bannerSize];
	if (!banner) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"BannerFile[%s]: banner buffer allocation failed", phase);
		fclose(file);
		return MEM_ALLOC_FAILED;
	}

	if (fseek(file, 0, SEEK_SET) != 0
		|| fread(banner, 1, bannerSize, file) != bannerSize) {
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"BannerFile[%s]: couldn't read %s", phase, filepath);
		delete[] banner;
		fclose(file);
		return FILE_IO_FAILED;
	}
	fclose(file);

	const banner_ops::SourceValidation validation = banner_ops::ValidateSourceBanner(
		banner, bannerSize);
	if (validation != banner_ops::SourceValidation::Valid) {
		const char *reason = validation == banner_ops::SourceValidation::WrongSize
			? "size is not 2,112 bytes"
			: validation == banner_ops::SourceValidation::WrongVersion
				? "version is not Regular DS v1"
				: "checksum does not match";
		flashcart_core::platform::logMessage(flashcart_core::LOG_ERR,
			"BannerFile[%s]: %s", phase, reason);
		delete[] banner;
		return BannerValidationResult(validation);
	}
	flashcart_core::platform::logMessage(flashcart_core::LOG_NOTICE,
		"BannerFile[%s]: accepted NDS v1 banner (%lu bytes)",
		phase, static_cast<unsigned long>(bannerSize));
	*outBanner = banner;
	*outBannerSize = bannerSize;
	return ALL_OK;
}

return_codes_t ValidateBannerFile(flashcart_core::Flashcart* cart, const char* filepath)
{
	uint8_t* banner = nullptr;
	uint32_t bannerSize = 0;
	const return_codes_t result = LoadBannerFile(cart, filepath, "preflight",
		&banner, &bannerSize);
	delete[] banner;
	return result;
}

return_codes_t WriteBanner(flashcart_core::Flashcart* cart, const char* filepath)
{
	uint8_t* banner = nullptr;
	uint32_t bannerSize = 0;
	const return_codes_t loaded = LoadBannerFile(cart, filepath, "post-combo",
		&banner, &bannerSize);
	if (loaded != ALL_OK) {
		return loaded;
	}

	DrawRectangle(TOP_SCREEN, 0, 2 * FONT_HEIGHT, SCREEN_WIDTH,
		SCREEN_HEIGHT - 2 * FONT_HEIGHT, COLOR_BLACK);
	DrawString(TOP_SCREEN, FONT_WIDTH, 2 * FONT_HEIGHT, COLOR_WHITE,
		"Writing and verifying the DS banner...");
	DrawString(TOP_SCREEN, FONT_WIDTH, 3 * FONT_HEIGHT, COLOR_WHITE,
		"Do not power off or remove the cart.");
	progressCount = 0;
	SetProgressStatusOverride("Writing DS banner");
	ShowProgress(BOTTOM_SCREEN, 0, 1, "Writing DS banner");

	const bool written = banner_ops::WriteBanner(cart, banner, bannerSize);
	delete[] banner;
	if (!written) {
		SetProgressStatusOverride(nullptr);
		return FLASH_OP_FAILED;
	}

	flashcart_core::platform::logMessage(flashcart_core::LOG_NOTICE,
		"WriteBanner: completed");
	ShowProgress(BOTTOM_SCREEN, 1, 1, "Writing DS banner");
	SetProgressStatusOverride(nullptr);
	return ALL_OK;
}
