#include "menu.h"

#include <nds.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib> // rand(); the seed itself lives in main()
#include <cstring>
#include <strings.h> // strcasecmp(); POSIX, so there's no <c...> spelling of it

#include "ui.h"
#include "nds_platform.h"
#include "banner_ops.h"
#include "key_combo_sequence.h"
#include "device.h"
#include "filebrowser.h"

// Wording follows Sanras's flashcart guide, which quotes this tool verbatim
// ("flashrom", "key combo", "Back up flash", "Write flash") -- don't reword.
#define bootmsg "This tool writes directly to your\n" 					\
				"flashcart's flashrom. A bad write\n" 					\
				"can brick it, so keep a backup.\n\n" 					\
				"Not every cart is supported. If\n" 					\
				"Back up flash fails or its file\n" 					\
				"looks wrong, STOP. Do not use\n" 						\
				"Write flash; open a GitHub issue."

using namespace flashcart_core;
using namespace ncgc;

int global_loglevel = 1; //https://github.com/ntrteam/flashcart_core/blob/master/platform.h#L6

// <START> power-off shortcut, checked once per frame from the boot splash
// and the cart list -- deliberately not everywhere in the app (see the
// switch-flash confirm/combo screens, which don't offer it). GodMode9i's own
// startMenu() was checked as precedent for the *action* itself: it offers
// exactly "Power off" (systemShutDown()) and a DSi-specific "Reboot", never
// a generic "return to loader" action. That's deliberate, not an oversight
// -- libnds's exit(0) can hang on flashcart menus (confirmed with AKMenu)
// that don't properly support or clear its loader-return protocol, so this
// app doesn't offer that path anywhere either. Never returns if <START> was
// pressed.
void HandlePowerOffShortcut(void)
{
	if (keysDown() & KEY_START)
	{
		systemShutDown();
		while (true) { swiWaitForVBlank(); }
	}
}

void print_boot_msg(void)
{
	// Plain black + the same blue header bar every other screen uses, not a
	// full alarm-red screen — this is a heads-up, not a hazard warning.
	char header_title[64];
	sprintf(header_title, "Cart-Flasher %s", CART_FLASHER_VERSION);
	DrawHeader(TOP_SCREEN, header_title);
	DrawString(TOP_SCREEN, FONT_WIDTH, FONT_HEIGHT * 2, COLOR_WHITE, bootmsg);
	DrawTopFooterAction("<A> Continue   <START> Power off");
	// Keep one blank text row between the three credit lines and the footer.
	// The old y=160 position put the final 10px line at y=180, overlapping
	// the footer at 182.
	DrawStringF(TOP_SCREEN, FONT_WIDTH, SCREEN_HEIGHT - (FONT_HEIGHT * 5),
		COLOR_GREY, "Developed by @tasken\n%s build - Commit: %s\nBased on work by jason0597 & DS-Homebrew",
		CART_FLASHER_BUILD_KIND, CART_FLASHER_COMMIT);

	while (true)
	{
		swiWaitForVBlank();
		scanKeys();
		HandlePowerOffShortcut();
		if (keysDown() & KEY_A)
		{
			break;
		}
	}
}

void WaitPress(u32 keyMask) {
	while (true) { swiWaitForVBlank(); scanKeys(); if (keysDown() & keyMask) { break; } }
}

static void WaitRelease(u32 keyMask) {
	while (keysHeld() & keyMask) { swiWaitForVBlank(); scanKeys(); }
}

// <A> to go ahead, <B> to back out. WaitPress() accepts a key mask but doesn't
// report which key was pressed, so it can't express this choice.
static bool WaitConfirm(void) {
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_A) { return true; }
		if (keysDown() & KEY_B) { return false; }
	}
}

static void DrawTopStatusAt(const char *title, const char *message,
	u16 color, const char *action, int contentRow) {
	DrawHeader(TOP_SCREEN, title);
	DrawString(TOP_SCREEN, FONT_WIDTH, contentRow * FONT_HEIGHT, color, message);
	DrawTopFooterAction(action);
}

static void DrawTopStatus(const char *title, const char *message,
	u16 color, const char *action) {
	DrawTopStatusAt(title, message, color, action, 2);
}

static void DrawCartOperationMenu(Flashcart *cart) {
	DrawHeader(TOP_SCREEN, cart->getName());
	DrawTopFooterAction("<A> Select   <B> Back");
}

static bool IsBannerValidationFailure(return_codes_t result) {
	return result == BANNER_SIZE_INVALID
		|| result == BANNER_VERSION_INVALID
		|| result == BANNER_CRC_INVALID;
}

static void DrawBannerValidationError(return_codes_t result, const char *action) {
	const char *reason = result == BANNER_SIZE_INVALID
		? "The file is not 2,112 bytes."
		: result == BANNER_VERSION_INVALID
			? "It is not a Regular DS v1 banner."
			: "The banner checksum is invalid.";
	char message[128];
	snprintf(message, sizeof(message), "Choose another .bin DS banner.\n\n%s", reason);
	DrawTopStatus("DS banner file rejected", message, COLOR_RED, action);
}

static void DrawFlashImageValidationError(const char *action) {
	DrawTopStatus("Flash image rejected",
		"Choose a file at least as large\nas this cart's flashrom.",
		COLOR_RED, action);
}

static bool ConfirmRecoveryHeader(const char *profilePrompt) {
	DrawHeader(TOP_SCREEN, "Cart not detected");
	DrawString(TOP_SCREEN, FONT_WIDTH, 2 * FONT_HEIGHT, COLOR_WHITE,
		"Normal detection failed.\n\n"
		"Try alternate detection?");
	DrawString(TOP_SCREEN, FONT_WIDTH, 7 * FONT_HEIGHT, COLOR_WHITE,
		profilePrompt);
	DrawString(TOP_SCREEN, FONT_WIDTH, 10 * FONT_HEIGHT, COLOR_WHITE,
		"No changes are made until\nyou select Write flash.");
	DrawTopFooterAction("<A> Try alternate detection   <B> Back");
	return WaitConfirm();
}

static int DrawWrappedText(u16 *screen, int x, int y, u16 color, const char *text) {
	const int maxColumns = (SCREEN_WIDTH - x) / FONT_WIDTH;
	char line[SCREEN_WIDTH / FONT_WIDTH + 1];
	int lineLength = 0;

	while (*text && y < SCREEN_HEIGHT) {
		if (*text == '\n') {
			line[lineLength] = '\0';
			DrawString(screen, x, y, color, line);
			y += FONT_HEIGHT;
			lineLength = 0;
			++text;
			continue;
		}

		while (*text == ' ') {
			++text;
		}
		if (!*text || *text == '\n') {
			continue;
		}

		const char *word = text;
		while (*text && *text != ' ' && *text != '\n') {
			++text;
		}
		int wordLength = text - word;
		if (lineLength && lineLength + 1 + wordLength > maxColumns) {
			line[lineLength] = '\0';
			DrawString(screen, x, y, color, line);
			y += FONT_HEIGHT;
			lineLength = 0;
		}
		while (wordLength > maxColumns && y < SCREEN_HEIGHT) {
			memcpy(line, word, maxColumns);
			line[maxColumns] = '\0';
			DrawString(screen, x, y, color, line);
			y += FONT_HEIGHT;
			word += maxColumns;
			wordLength -= maxColumns;
		}
		if (lineLength) {
			line[lineLength++] = ' ';
		}
		for (int i = 0; i < wordLength; ++i) {
			line[lineLength++] = word[i];
		}
	}

	if (lineLength && y < SCREEN_HEIGHT) {
		line[lineLength] = '\0';
		DrawString(screen, x, y, color, line);
		y += FONT_HEIGHT;
	}
	return y;
}

static int CountTextLines(const char *text) {
	int lineCount = 1;
	for (const char *p = text; *p; ++p) {
		if (*p == '\n') { ++lineCount; }
	}
	return lineCount;
}

static int DrawCenteredTextBlock(u16 *screen, int y, u16 color, const char *text) {
	DrawStringCentered(screen, y, color, text);
	return y + CountTextLines(text) * FONT_HEIGHT;
}

static bool ConfirmDestructiveWrite(const char *message) {
	// Center the complete message/title/combo group in the rows between the
	// header and footer. Callers provide deliberately balanced lines, so no
	// trailing fragment makes an otherwise centered modal look lopsided.
	const int modalHeight = (CountTextLines(message) + 4) * FONT_HEIGHT;
	const int contentHeight = SCREEN_HEIGHT - (2 * FONT_HEIGHT);
	const int freeHeight = contentHeight - modalHeight;
	const int topRows = freeHeight > 0
		? (freeHeight + FONT_HEIGHT) / (2 * FONT_HEIGHT)
		: 0;
	const int contentY = FONT_HEIGHT + topRows * FONT_HEIGHT;
	const int nextY = DrawCenteredTextBlock(TOP_SCREEN, contentY,
		COLOR_WHITE, message);
	const int titleY = nextY + FONT_HEIGHT;
	const int comboY = titleY + (2 * FONT_HEIGHT);

	DrawStringCentered(TOP_SCREEN, titleY, COLOR_YELLOW,
		"Enter the key combo to confirm:");
	DrawTopFooterAction("<B> Cancel");
	return d0k3_buttoncombo(titleY, comboY);
}

static void DrawFlashcartInfo(Flashcart *cart) {
	DrawHeader(BOTTOM_SCREEN, "Flashcart info");
	int y = 2 * FONT_HEIGHT;
	DrawString(BOTTOM_SCREEN, FONT_WIDTH, y, COLOR_GREY, "Driver credits");
	y = DrawWrappedText(BOTTOM_SCREEN, FONT_WIDTH, y + FONT_HEIGHT, COLOR_WHITE,
		cart->getAuthor());
	DrawWrappedText(BOTTOM_SCREEN, FONT_WIDTH, y + FONT_HEIGHT, COLOR_WHITE,
		cart->getDescription());
}

bool ntrCardReset()
{
	if (isDSiMode())
	{
		// Reset card slot
		disableSlot1();
		for(int i = 0; i < 25; i++) { swiWaitForVBlank(); }
		enableSlot1();
		for(int i = 0; i < 15; i++) { swiWaitForVBlank(); }
	}
	else
	{
		REG_ROMCTRL = 0;
		REG_AUXSPICNT = 0;
		for (int i = 0; i < 25; i++) swiWaitForVBlank();
		REG_AUXSPICNT = CARD_CR1_ENABLE | CARD_CR1_IRQ;
		REG_ROMCTRL = CARD_nRESET | CARD_SEC_SEED;
		while (REG_ROMCTRL & CARD_BUSY) ;
		cardReset();
		while (REG_ROMCTRL & CARD_BUSY) ;
	}
	return true;
}

void menu_lvl1(Flashcart* cart)
{
	// Remove R4iSDHC.hk if not in DSi mode
	if (!isDSiMode()) {
		for (auto it = flashcart_list->begin(); it != flashcart_list->end(); ) {
			if (strcmp((*it)->getShortName(), "R4iSDHC.hk") == 0) {
				it = flashcart_list->erase(it);
			} else {
				++it;
			}
		}
	}

	// Sort alphabetically by name
	std::sort(flashcart_list->begin(), flashcart_list->end(), [](Flashcart* a, Flashcart* b) {
		return strcasecmp(a->getName(), b->getName()) < 0;
	});

	u32 menu_sel = 0;
	
	NTRCard card(ntrCardReset);
	DrawHeader(TOP_SCREEN, "Choose your flashcart");
	DrawFooter(global_loglevel);
	DrawFlashcartInfo(flashcart_list->at(0));
	u32 flashcart_list_size = flashcart_list->size();

	// Redraws only on change, not every frame -- full-width highlight bars
	// redrawn every frame with no vsync tear visibly on hardware.
	for (u32 i = 0; i < flashcart_list_size; i++)
	{
		DrawListRow(TOP_SCREEN, (i + 2) * FONT_HEIGHT, i == menu_sel, COLOR_ACCENT, flashcart_list->at(i)->getName());
	}

	while (true) //This will be our MAIN loop
	{
		swiWaitForVBlank();
		bool reprintFlag = false;

		scanKeys();
		HandlePowerOffShortcut();
		if (keysDown() & KEY_DOWN && menu_sel < (flashcart_list_size - 1))
		{
			menu_sel++;
			reprintFlag = true;
		}
		if (keysDown() & KEY_UP   && menu_sel > 0)
		{
			menu_sel--;
			reprintFlag = true;
		}
		if (keysDown() & KEY_Y) {
			if (global_loglevel == 4) {
				global_loglevel = 0; //if you scroll past the end it puts you back at the top
			}
			else {
				global_loglevel++;
			}
			DrawFooter(global_loglevel);
		}
		if (keysDown() & KEY_SELECT) {
			// The probe is explicitly requested, so it stays independent of
			// the log threshold and restores the unchanged footer on return.
			DrawTopFooterAction("<A>/<B> Back to cart list");
			DrawHeader(BOTTOM_SCREEN, "Hardware probe");
			LogHardwareProbe(2);
			WaitPress(KEY_A | KEY_B);
			WaitRelease(KEY_A | KEY_B);
			DrawFooter(global_loglevel);
			reprintFlag = true; // redraws the flashcart info the probe covered
		}
		else if (keysDown() & KEY_A)
		{
			cart = flashcart_list->at(menu_sel); //Set the cart equal to whatever we had selected from before

			// One row past the last cart -- fixed would sit flush against the
			// list in DSi mode, since R4iSDHC.hk is hidden outside it. Also
			// reused by the detection-error message below, which overwrites
			// this same row once it's known whether detection succeeded.
			const int errorRow = flashcart_list_size + 3;

			// card.init()/cart->initialize() below do real SPI/cart-bus probing,
			// which can take a noticeable moment -- without this, the screen just
			// sits on the list with no sign the button press registered, which
			// reads as a freeze rather than a wait. White, in the content area:
			// matches every other in-progress status line in this app (e.g.
			// DumpFlash/WriteFlash's "Backing up.../Writing to..."). Yellow is
			// reserved for button prompts everywhere else, never plain status.
			// Footer hidden while this runs: it's a blocking call with no
			// scanKeys() polling underneath it, so "<A> Select   <Y> Log: %s"
			// would sit there doing nothing -- restored by the DrawFooter()
			// call on both paths once detection resolves.
			DrawRectangle(TOP_SCREEN, 0, SCREEN_HEIGHT - FONT_HEIGHT, SCREEN_WIDTH, FONT_HEIGHT, COLOR_BLACK);
			DrawStringF(TOP_SCREEN, FONT_WIDTH, errorRow * FONT_HEIGHT, COLOR_CYAN, "Detecting %s...", cart->getName());

			if (!cart->requiresCardInitialization()) {
				// App-owned debug carts exercise UI/filesystem flows without
				// touching the physical Slot-1 bus.
			} else if (isDSiMode() || strcmp(cart->getShortName(), "DSTT") == 0) {
				// __ncgc_must_check. Not fatal here -- initialize() below fails
				// too and shows the detection error -- but the reason only
				// exists here, so log it instead of discarding it.
				const Err err = card.init();
				if (err) {
					platform::logMessage(LOG_ERR, "menu: card.init failed: %s", err.desc());
				}
			} else {
				// DS mode, non-DSTT: assume the cart's own menu already took the
				// card through KEY1 into KEY2, so just record that instead of
				// redoing the handshake. Breaks under nds-bootstrap -- no KEY2
				// to inherit there.
				card.state(NTRState::Key2);
			}
			bool initialized = cart->initialize(&card);
			bool recoveryDeclined = false;
			if (!initialized && cart->hasRecoveryProfile()) {
				if (!ConfirmRecoveryHeader(cart->getRecoveryPrompt())) {
					DrawHeader(TOP_SCREEN, "Choose your flashcart");
					DrawFooter(global_loglevel);
					reprintFlag = true;
					recoveryDeclined = true;
				}
				else {
					// Keep the recovery explanation visible while this blocking
					// attempt runs. Leave one blank row after it, then replace
					// the now-inactive footer action with an empty footer.
					DrawString(TOP_SCREEN, FONT_WIDTH, 14 * FONT_HEIGHT, COLOR_CYAN,
						"Trying alternate detection...");
					DrawTopFooterAction("");
					initialized = cart->initializeRecovery(&card);
				}
			}

			if (!initialized && !recoveryDeclined) //If cart initialization fails, do all this and then break to main menu
			{
				DrawTopStatusAt("Detection failed",
					"Couldn't detect this flashcart.\nReinsert it, then try again.",
					COLOR_RED, "<B> Back to cart list", 2);
				WaitPress(KEY_B);
				ClearScreen(TOP_SCREEN, COLOR_BLACK);
				DrawHeader(TOP_SCREEN, "Choose your flashcart");
				DrawFooter(global_loglevel);
				reprintFlag = true;
			}
			else if (initialized)
			{
				menu_lvl2(cart); //There is a while loop over at menu_lvl2(), the statements underneath won't get executed immediately
				DrawHeader(TOP_SCREEN, "Choose your flashcart");
				DrawFooter(global_loglevel);
				reprintFlag = true;
			}
		}

		if (reprintFlag)
		{
			for (u32 i = 0; i < flashcart_list_size; i++)
			{
				DrawListRow(TOP_SCREEN, (i + 2) * FONT_HEIGHT, i == menu_sel, COLOR_ACCENT, flashcart_list->at(i)->getName());
			}
			cart = flashcart_list->at(menu_sel);
			DrawFlashcartInfo(cart);
		}
	}
}

void menu_lvl2(Flashcart* cart)
{
	DrawCartOperationMenu(cart);
	int menu_sel = 0;
	bool dirty = true;
	const bool hasBannerTools = banner_ops::HasAvailableOperation(cart);
	const int menuItemCount = hasBannerTools ? 4 : 2;

	while (true)
	{
		swiWaitForVBlank();
		// Only redraw the highlight-bar rows when the selection (or the screen
		// underneath them) actually changed, not every single frame — redrawing
		// full-width rectangles unconditionally with no vsync tears visibly.
		if (dirty) {
			DrawString(TOP_SCREEN, FONT_WIDTH, 2 * FONT_HEIGHT, COLOR_GREY, "Flashrom operations");
			DrawListRow(TOP_SCREEN, 3 * FONT_HEIGHT, menu_sel == 0, COLOR_ACCENT, "Back up flash");	//0
			DrawListRow(TOP_SCREEN, 4 * FONT_HEIGHT, menu_sel == 1, COLOR_TINTEDRED, "Write flash");	//1
			if (hasBannerTools) {
				DrawString(TOP_SCREEN, FONT_WIDTH, 6 * FONT_HEIGHT, COLOR_GREY, "DS banner operations");
				DrawListRow(TOP_SCREEN, 7 * FONT_HEIGHT, menu_sel == 2, COLOR_ACCENT, "Back up DS banner");	//2
				DrawListRow(TOP_SCREEN, 8 * FONT_HEIGHT, menu_sel == 3, COLOR_TINTEDRED, "Write DS banner");	//3
			}
			dirty = false;
		}

		scanKeys();

		if (keysDown() & KEY_DOWN && menu_sel < menuItemCount - 1)
		{
			menu_sel++;
			dirty = true;
		}
		if (keysDown() & KEY_UP   && menu_sel > 0)
		{
			menu_sel--;
			dirty = true;
		}
		if (keysDown() & KEY_B)
		{
			break;
		}
		return_codes_t ntrboot_return = ALL_OK;

		if (keysDown() & KEY_A)
		{
			char writePath[512];
			const bool isBackup = menu_sel == 0;
			const bool isBannerBackup = hasBannerTools && menu_sel == 2;
			const bool isBannerWrite = hasBannerTools && menu_sel == 3;
			if (!isBackup && !isBannerBackup) {
				if (!BrowseForFile(isBannerWrite ? "/cart-backups/banners" : "/cart-backups", ".bin",
					isBannerWrite ? "Choose a .bin DS banner" : "Choose a .bin flash file",
					writePath, sizeof(writePath))) {
					DrawCartOperationMenu(cart);
					dirty = true;
					continue;
				}
				{
					// Validate the selected source before asking for the destructive
					// combo. The write path loads it again after confirmation so a
					// file replacement on the SD card can't bypass this check.
					const return_codes_t validation = isBannerWrite
						? ValidateBannerFile(cart, writePath)
						: ValidateFlashImage(cart, writePath);
					if (validation != ALL_OK) {
						DrawHeader(TOP_SCREEN, cart->getName());
						if (isBannerWrite && IsBannerValidationFailure(validation)) {
							DrawBannerValidationError(validation, "<B> Back to banner list");
						} else if (isBannerWrite && validation == FLASH_OP_FAILED) {
							DrawTopStatus("DS banner write unavailable",
								"This cart's DS banner layout is\nno longer recognized.\n\nNo changes were made.",
								COLOR_RED, "<B> Back to banner list");
						} else if (!isBannerWrite && validation == FLASH_OP_FAILED) {
							DrawTopStatus("Flash write unavailable",
								"This cart's flashrom size is\nunknown.\n\nNo changes were made.",
								COLOR_RED, "<B> Back to flash list");
						} else if (!isBannerWrite && validation == FLASH_IMAGE_INVALID) {
							DrawFlashImageValidationError("<B> Back to flash list");
						} else {
							DrawTopStatus(isBannerWrite ? "DS banner check failed" : "Flash image check failed",
								isBannerWrite
									? "Couldn't read the selected DS banner.\nCheck the SD card, then try again."
									: "Couldn't read the selected file.\nCheck the SD card, then try again.",
								COLOR_RED, isBannerWrite
									? "<B> Back to banner list"
									: "<B> Back to flash list");
						}
						WaitPress(KEY_B);
						DrawCartOperationMenu(cart);
						dirty = true;
						continue;
					}
				}
				// No DrawHeader here: the confirm below redraws it anyway, and
				// clears the file browser off the screen while it's at it.
			}

			// Confirm takes the whole screen -- DrawHeader clears the menu
			// behind it (footer included) for free. Destructive messages use
			// the same one-character content inset as the bottom screen.
			//
			// Only writing is gated behind the combo: reading can't damage the
			// cart (no driver reaches erase/program from readFlash), and
			// gating it the same way would train people to mash through the
			// combo before the destructive path.
			DrawHeader(TOP_SCREEN, cart->getName());
			bool confirmed;
			if (isBackup)
			{
				DrawString(TOP_SCREEN, FONT_WIDTH, 2 * FONT_HEIGHT, COLOR_WHITE,
					"Back up this cart's flashrom to\n/cart-backups on your SD card.\n\nNothing is written to the cart.\nAn existing backup with this\ncart's name will be replaced.\n\nIf it fails or looks wrong, STOP.\nDo not use Write flash; open an\nissue.");
				DrawTopFooterAction("<A> Start backup   <B> Cancel");
				confirmed = WaitConfirm();
			}
			else if (isBannerBackup)
			{
				DrawString(TOP_SCREEN, FONT_WIDTH, 2 * FONT_HEIGHT, COLOR_WHITE,
					"Back up this cart's DS banner to\n"
					"/cart-backups/banners.\n\n"
					"Nothing is written to the cart.");
				DrawTopFooterAction("<A> Back up DS banner   <B> Cancel");
				confirmed = WaitConfirm();
			}
			else if (isBannerWrite)
			{
				confirmed = ConfirmDestructiveWrite(
					"Change this cart's DS banner?\n\n"
					"Only the DS banner changes.\n"
					"The rest of the flashrom stays intact.\n\n"
					"Custom banners require CFW\n"
					"to launch on DSi or 3DS.");
			}
			else
			{
				// Banner/icon note is write-only: restoring an untouched dump
				// leaves the banner byte-identical, so it can't break stock
				// DSi/3DS loading.
				confirmed = ConfirmDestructiveWrite(
					"Replace this cart's flashrom?\n\n"
					"This overwrites the cart's flashrom.\n"
					"Keep its original backup to restore it.");
			}

			if (confirmed)
			{
				ClearScreen(BOTTOM_SCREEN, COLOR_BLACK);
				if (isBackup) {
					ntrboot_return = DumpFlash(cart);
				} else if (isBannerBackup) {
					ntrboot_return = DumpBanner(cart);
				} else if (isBannerWrite) {
					ntrboot_return = WriteBanner(cart, writePath);
				} else {
					ntrboot_return = WriteFlash(cart, writePath);
				}

				switch (ntrboot_return) {
					case FAT_MOUNT_FAILED:
						DrawTopStatus("SD card unavailable",
							"Couldn't access the SD card.\nReinsert it, press <B>, then\ntry again.",
							COLOR_RED, "<B> Back to cart list");
						WaitPress(KEY_B);
						break;

					case FILE_OPEN_FAILED:
						if (isBackup) {
							DrawTopStatus("Flash backup failed",
								"Couldn't create the flash backup.\nFree SD card space or unlock it,\nthen try again.",
								COLOR_RED, "<B> Back to cart list");
						} else if (isBannerBackup) {
							DrawTopStatus("DS banner backup failed",
								"Couldn't create the DS banner backup.\nFree SD card space or unlock it,\nthen try again.",
								COLOR_RED, "<B> Back to cart list");
						} else if (isBannerWrite) {
							DrawTopStatus("DS banner write failed",
								"Couldn't open the DS banner file.\nIt may have moved. Choose it again.",
								COLOR_RED, "<B> Back to cart list");
						} else {
							DrawTopStatus("Flash write failed",
								"Couldn't open the selected flash file.\nIt may have moved. Choose it again.",
								COLOR_RED, "<B> Back to cart list");
						}
						WaitPress(KEY_B);
						break;

					case FILE_IO_FAILED:
						if (isBackup) {
							DrawTopStatus("Flash backup failed",
								"Flash backup may be incomplete.\nDo not use Write flash.\n\nCheck the SD card, then\nselect Back up flash again.",
								COLOR_RED, "<B> Back to cart list");
						} else if (isBannerBackup) {
							DrawTopStatus("DS banner backup failed",
								"Couldn't save the DS banner backup.\nFree SD card space, then try\nagain.",
								COLOR_RED, "<B> Back to cart list");
						} else if (isBannerWrite) {
							DrawTopStatus("DS banner write failed",
								"Couldn't read the DS banner file.\nReinsert the SD card, then\ntry again.",
								COLOR_RED, "<B> Back to cart list");
						} else {
							DrawTopStatus("Flash write failed",
								"Flash write may have started.\nDo not retry.\n\nPower off. Restore a verified\nbackup.",
								COLOR_RED, "<B> Back to cart list");
						}
						WaitPress(KEY_B);
						break;

					case FLASH_OP_FAILED:
						if (isBackup) {
							DrawTopStatus("Flash backup failed",
								"Flash backup stopped before completion.\nReinsert the cart and retry.\n\nIf it fails again, do not use\nWrite flash; open an issue.",
								COLOR_RED, "<B> Back to cart list");
						} else if (isBannerBackup) {
							DrawTopStatus("DS banner backup failed",
								"DS banner backup unavailable.\nNo changes were made. Go back,\nselect the cart, and try again.",
								COLOR_RED, "<B> Back to cart list");
						} else if (isBannerWrite) {
							DrawTopStatus("DS banner write failed",
								"DS banner write or check failed.\nThe DS banner may be partly changed.\n\nDo not retry. Restore a verified\nflashrom backup.",
								COLOR_RED, "<B> Back to cart list");
						} else {
							DrawTopStatus("Flash write failed",
								"Flash write stopped partway through.\nDo not retry.\n\nPower off, check the cart, then\nrestore a verified backup.",
								COLOR_RED, "<B> Back to cart list");
						}
						WaitPress(KEY_B);
						break;

					case BANNER_SIZE_INVALID:
					case BANNER_VERSION_INVALID:
					case BANNER_CRC_INVALID:
						if (isBannerBackup) {
							DrawTopStatus("DS banner backup failed",
								"The cart's DS banner could not be\nchecked, so it was not saved.",
								COLOR_RED, "<B> Back to cart list");
						} else {
							DrawBannerValidationError(ntrboot_return, "<B> Back to cart list");
						}
						WaitPress(KEY_B);
						break;

					case FLASH_IMAGE_INVALID:
						DrawFlashImageValidationError("<B> Back to cart list");
						WaitPress(KEY_B);
						break;

					case MEM_ALLOC_FAILED:
						DrawTopStatus("Not enough memory",
							"Not enough console memory.\nRestart Cart-Flasher and try again.\n\nIf it repeats, open an issue.",
							COLOR_RED, "<B> Back to cart list");
						WaitPress(KEY_B);
						break;

					case ALL_OK:
						if (isBackup) {
							DrawTopStatus("Flash backup complete",
								"Saved in /cart-backups.\nCopy the flash backup off the\nSD card for safekeeping.",
								COLOR_GREEN, "<A> Continue");
						} else if (isBannerBackup) {
							DrawTopStatus("DS banner backup complete",
								"Saved in /cart-backups/banners.",
								COLOR_GREEN, "<A> Continue");
						} else if (isBannerWrite) {
							DrawTopStatus("DS banner updated",
								"DS banner updated and verified.\nOnly DS banner data changed.",
								COLOR_GREEN, "<A> Continue");
						} else {
							DrawTopStatus("Flash write complete",
								"Flashrom write complete.\nKeep your original backup safe.",
								COLOR_GREEN, "<A> Continue");
						}
						WaitPress(KEY_A);
						ClearScreen(BOTTOM_SCREEN, COLOR_BLACK);
						break;
				}
				// A completed operation (whatever the result) always returns
				// to the cart list, not just this cart's own menu -- matches
				// the ALL_OK/error screens above, which all wait for a
				// keypress then fall through to here.
				break;
			}
			// Cancelled at the confirm/combo screen -- same landing spot as
			// cancelling the file browser above: back to this cart's own
			// Back up/Write flash list, not all the way out to the cart
			// list. No separate "nothing was touched" screen: <B> already
			// means cancel on both prompts.
			DrawCartOperationMenu(cart);
			dirty = true;
			continue;
		}
	}
}

// Prints a GodMode9-style "input this combo" prompt and checks the presses.
const char rancombo_symbols[5] = { '\x1B', '\x18', '\x1A', '\x19', 'A' }; // Left, Up, Right, Down
const u32 rancombo_inputs[5] = { KEY_LEFT, KEY_UP, KEY_RIGHT, KEY_DOWN, KEY_A };

static void PrepareButtonCombo(int sequence[key_combo::kLength],
	char symbols[key_combo::kLength], u32 inputs[key_combo::kLength],
	const int rejected[key_combo::kLength])
{
	key_combo::Generate(sequence, rejected);
	for (int i = 0; i < key_combo::kLength; ++i) {
		symbols[i] = rancombo_symbols[sequence[i]];
		inputs[i] = rancombo_inputs[sequence[i]];
	}
}

bool d0k3_buttoncombo(int titleY, int comboY)
{
	// Always 5 slots wide, so it centres itself instead of making callers work
	// the column out; the last slot has no trailing gap.
	const int combo_width = (4 * (4 * FONT_WIDTH)) + (3 * FONT_WIDTH);
	const int cur_c = (SCREEN_WIDTH - combo_width) / 2;

	// Seeded once in main(), not here -- re-seeding per call from time(NULL)
	// tied the combo to the clock second instead of advancing it.
	//
	// No symbol repeats back-to-back, matching GodMode9 (ui.c: `while (lsh ==
	// lastlsh) lsh = (PRNG & 0x3)`) -- doubled arrows misread as one, and this
	// keeps the double-tap tolerance below unambiguous.
	int num_rancombo[key_combo::kLength] = {};
	char print_rancombo[key_combo::kLength] = {};
	u32 check_rancombo[key_combo::kLength] = {};
	PrepareButtonCombo(num_rancombo, print_rancombo, check_rancombo, nullptr);
	int depth = 0; // combo progress, 0-based

	while (true) {
		if (depth == key_combo::kLength + 1) {
			return true;
		}
		int temp_c = cur_c;
		u16 cur_color = COLOR_GREEN;
		for (int i = 0; i < key_combo::kLength; i++) {
			if (i >= depth) { cur_color = COLOR_WHITE; }
			d0k3_buttoncombo_print_chars(temp_c, comboY, cur_color, print_rancombo[i]);
			temp_c += 4 * FONT_WIDTH; //3 for our printout ('<', 'arrow', '>'), and one for the space that follows it
		}

		scanKeys();
		if (keysDown()) {
			if (depth < key_combo::kLength
				&& (keysDown() & check_rancombo[depth])) {
				depth++;
			}
			else if (keysDown() & KEY_B) {
				return false;
			}
			else if (depth > 0 && (keysDown() & check_rancombo[depth - 1])) {
				// Double-tap forgiveness, matching GodMode9 (ui.c: `!(pad_state &
				// sequence[lvl-1])`). Safe since no symbol repeats back-to-back;
				// this only suppresses a reset, it never advances the combo.
			}
			else {
				int rejected[key_combo::kLength];
				std::memcpy(rejected, num_rancombo, sizeof(rejected));
				PrepareButtonCombo(num_rancombo, print_rancombo, check_rancombo,
					rejected);
				depth = 0;
				DrawRectangle(TOP_SCREEN, 0, titleY, SCREEN_WIDTH, FONT_HEIGHT,
					COLOR_BLACK);
				DrawStringCentered(TOP_SCREEN, titleY, COLOR_RED,
					"Wrong combo. Try this one:");
			}
		}

		// this is sorta hacky but otherwise the A button doesnt go green
		if (depth == key_combo::kLength) {
			depth++;
		}
	}
}

void d0k3_buttoncombo_print_chars(int collumn, int row, u16 color, char character)
{
	DrawCharacter(TOP_SCREEN, '<', collumn, row, color);
	collumn += FONT_WIDTH;
	DrawCharacter(TOP_SCREEN, character, collumn, row, color);
	collumn += FONT_WIDTH;
	DrawCharacter(TOP_SCREEN, '>', collumn, row, color);
}
