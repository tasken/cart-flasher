#include "filebrowser.h"

#include <nds.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <dirent.h>
#include <strings.h> // strcasecmp(); POSIX, so there's no <c...> spelling of it
#include <sys/stat.h>

#include "ui.h"
#include "menu.h"
#include "nds_platform.h"

namespace {

struct FileEntry {
	char name[256];
	bool isDir;
};

bool HasExtensionCI(const char* name, const char* ext) {
	size_t nameLen = strlen(name);
	size_t extLen = strlen(ext);
	if (extLen > nameLen) { return false; }
	return strcasecmp(name + (nameLen - extLen), ext) == 0;
}

void PathJoin(char* dest, size_t destSize, const char* base, const char* name) {
	if (strcmp(base, "/") == 0) {
		snprintf(dest, destSize, "/%s", name);
	} else {
		snprintf(dest, destSize, "%s/%s", base, name);
	}
}

void PathUp(char* path) {
	if (strcmp(path, "/") == 0) { return; }
	char* lastSlash = strrchr(path, '/');
	if (lastSlash == path) {
		path[1] = '\0';
	} else {
		*lastSlash = '\0';
	}
}

void ListDirectory(const char* path, const char* ext, std::vector<FileEntry>& outEntries) {
	outEntries.clear();

	std::vector<FileEntry> real;
	DIR* dir = opendir(path);
	if (dir) {
		struct dirent* ent;
		while ((ent = readdir(dir)) != nullptr) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) { continue; }

			char fullPath[512];
			PathJoin(fullPath, sizeof(fullPath), path, ent->d_name);

			struct stat st;
			if (stat(fullPath, &st) != 0) { continue; }
			bool isDir = S_ISDIR(st.st_mode);
			if (!isDir && !HasExtensionCI(ent->d_name, ext)) { continue; }

			FileEntry fe;
			strncpy(fe.name, ent->d_name, sizeof(fe.name) - 1);
			fe.name[sizeof(fe.name) - 1] = '\0';
			fe.isDir = isDir;
			real.push_back(fe);
		}
		closedir(dir);
	}

	std::sort(real.begin(), real.end(), [](const FileEntry& a, const FileEntry& b) {
		if (a.isDir != b.isDir) { return a.isDir; }
		return strcasecmp(a.name, b.name) < 0;
	});

	if (strcmp(path, "/") != 0) {
		FileEntry up;
		strcpy(up.name, "..");
		up.isDir = true;
		outEntries.push_back(up);
	}
	outEntries.insert(outEntries.end(), real.begin(), real.end());
}

void ResetDirectoryView(const char* path, const char* ext,
	std::vector<FileEntry>& entries, int& cursor, int& scrollTop) {
	ListDirectory(path, ext, entries);
	cursor = 0;
	scrollTop = 0;
}

void RenderList(const char* currentPath, const char* title,
				const char* ext, const std::vector<FileEntry>& entries,
				int cursor, int scrollTop, int visibleCount) {
	DrawHeader(TOP_SCREEN, title);

	// SCREEN_WIDTH/FONT_WIDTH (42) is one too many: DrawString starts drawing
	// at x=FONT_WIDTH (this line's own left margin), not x=0, so only 41
	// characters actually fit before it wraps to the next row -- a 42-char
	// path would spill its last character onto row 2, landing on the first
	// file-list entry. Verified directly: 6 + 41*6 = 252, +6 more > 256.
	const int maxPathChars = (SCREEN_WIDTH / FONT_WIDTH) - 1;
	char pathDisplay[64];
	int pathLen = strlen(currentPath);
	if (pathLen > maxPathChars) {
		snprintf(pathDisplay, sizeof(pathDisplay), "...%s", currentPath + pathLen - (maxPathChars - 3));
	} else {
		snprintf(pathDisplay, sizeof(pathDisplay), "%s", currentPath);
	}
	DrawString(TOP_SCREEN, FONT_WIDTH, FONT_HEIGHT, COLOR_GREY, pathDisplay);

	const int maxNameChars = (SCREEN_WIDTH / FONT_WIDTH) - 2;
	for (int i = 0; i < visibleCount; i++) {
		int idx = scrollTop + i;
		if (idx >= (int)entries.size()) { break; }
		const FileEntry& e = entries[idx];
		int y = FONT_HEIGHT * (2 + i);

		char display[64];
		int nameLen = strlen(e.name);
		if (nameLen > maxNameChars) {
			snprintf(display, sizeof(display), "%.*s~%s", maxNameChars - 2, e.name, e.isDir ? "/" : "");
		} else {
			snprintf(display, sizeof(display), "%s%s", e.name, e.isDir ? "/" : "");
		}
		DrawListRow(TOP_SCREEN, y, idx == cursor, COLOR_ACCENT, display);
	}

	bool hasParentEntry = !entries.empty() && strcmp(entries[0].name, "..") == 0;
	bool hasRealEntries = entries.size() > (hasParentEntry ? 1u : 0u);
	if (!hasRealEntries) {
		int emptyMsgY = FONT_HEIGHT * (2 + (hasParentEntry ? 1 : 0));
		char emptyMessage[64];
		snprintf(emptyMessage, sizeof(emptyMessage),
			"No %s files here.\nChoose another folder or press <B>.", ext);
		DrawString(TOP_SCREEN, FONT_WIDTH, emptyMsgY, COLOR_GREY, emptyMessage);
	}

	DrawString(TOP_SCREEN, FONT_WIDTH, SCREEN_HEIGHT - FONT_HEIGHT, COLOR_YELLOW, "<A> Select   <B> Back");
}

} // namespace

bool BrowseForFile(const char* startPath, const char* ext, const char* title,
					char* outPath, size_t outPathSize) {
	if (mount_fat() != ALL_OK) {
		DrawHeader(TOP_SCREEN, title);
		DrawString(TOP_SCREEN, FONT_WIDTH, (2 * FONT_HEIGHT), COLOR_RED,
			"Couldn't access the SD card.\nReinsert it, then try again.");
		DrawTopFooterAction("<B> Back");
		WaitPress(KEY_B);
		return false;
	}

	char currentPath[512];
	strncpy(currentPath, startPath, sizeof(currentPath) - 1);
	currentPath[sizeof(currentPath) - 1] = '\0';

	std::vector<FileEntry> entries;
	int cursor = 0;
	int scrollTop = 0;
	ResetDirectoryView(currentPath, ext, entries, cursor, scrollTop);
	const int visibleCount = (SCREEN_HEIGHT - FONT_HEIGHT * 3) / FONT_HEIGHT;
	bool dirty = true;
	bool result = false;

	while (true) {
		swiWaitForVBlank();
		if (dirty) {
			RenderList(currentPath, title, ext, entries, cursor, scrollTop, visibleCount);
			dirty = false;
		}

		scanKeys();
		u32 keys = keysDown();

		if (keys & KEY_DOWN && cursor < (int)entries.size() - 1) {
			cursor++;
			if (cursor >= scrollTop + visibleCount) { scrollTop = cursor - visibleCount + 1; }
			dirty = true;
		}
		if (keys & KEY_UP && cursor > 0) {
			cursor--;
			if (cursor < scrollTop) { scrollTop = cursor; }
			dirty = true;
		}
		if (keys & KEY_B) {
			if (strcmp(currentPath, "/") == 0) {
				result = false;
				break;
			}
			PathUp(currentPath);
			ResetDirectoryView(currentPath, ext, entries, cursor, scrollTop);
			dirty = true;
		}
		if (keys & KEY_A && !entries.empty()) {
			const FileEntry sel = entries[cursor];
			if (strcmp(sel.name, "..") == 0) {
				PathUp(currentPath);
				ResetDirectoryView(currentPath, ext, entries, cursor, scrollTop);
				dirty = true;
			} else if (sel.isDir) {
				char newPath[512];
				PathJoin(newPath, sizeof(newPath), currentPath, sel.name);
				strncpy(currentPath, newPath, sizeof(currentPath) - 1);
				currentPath[sizeof(currentPath) - 1] = '\0';
				ResetDirectoryView(currentPath, ext, entries, cursor, scrollTop);
				dirty = true;
			} else {
				char fullPath[512];
				PathJoin(fullPath, sizeof(fullPath), currentPath, sel.name);
				snprintf(outPath, outPathSize, "%s", fullPath);
				result = true;
				break;
			}
		}
	}

	unmount_fat();
	return result;
}
