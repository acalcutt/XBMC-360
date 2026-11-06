#ifndef H_CDISCDRIVE
#define H_CDISCDRIVE

#include "utils\StdString.h"

// What's in the console's disc drive, as read from the disc itself
class CDiscDrive
{
public:
	enum DiscType
	{
		DISC_NONE = 0,     // Empty, open, or nothing on it can be read
		DISC_XBOX360_GAME, // default.xex at the root
		DISC_XBOX_GAME,    // default.xbe at the root: an original Xbox game
		DISC_DVD_VIDEO,    // VIDEO_TS\VIDEO_TS.IFO
		DISC_DATA          // Anything else that can be read
	};

	struct Info
	{
		DiscType type;
		CStdString strLabel; // Volume label, often empty on game discs
		DWORD dwTitleId;     // Xbox 360 games, from default.xex
		int iDiscNumber;     // Xbox 360 games: disc iDiscNumber of iDiscCount
		int iDiscCount;
	};

	static const char* ROOT;

	static bool Detect(Info& info);
	static CStdString GetTypeName(DiscType type);
	static CStdString GetName(const Info& info);
	static void Play(const Info& info);
	static void Eject();
};

#endif //H_CDISCDRIVE
