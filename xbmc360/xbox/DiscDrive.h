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
	// Once Home is up after a start: when that start came straight after Play started the disc's game (so the game
	// didn't start: the console fell back to its dashboard, which a softmod makes us), offers to copy an Xbox 360 game
	// to the hard drive and play it from there, which Peer Pressure allows
	static void CheckPlayed();
	// Plays a game or a DVD; opens a data disc in Videos
	static void Open(const Info& info);
	static void Eject();

	// Whether there's a disc in, and what it is, for the GUI to ask every frame. Kept up without spinning the drive:
	// the tray's state is asked once a second, and the disc is only read when that changes (again every few seconds
	// for a while, as a disc takes a moment to spin up).
	static bool HasDisc();
	static const Info& Current();
};

#endif //H_CDISCDRIVE
