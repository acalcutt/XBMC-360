#ifndef H_CTITLENAMES
#define H_CTITLENAMES

#include "utils\StdString.h"
#include "utils\Thread.h"

// The names of Xbox 360 games by title ID, for what's known by its title ID only: a disc without a label (most are),
// and a copy or a share's folder named for one. Asked of XboxUnity (xboxunity.net, which Aurora asks too) once for
// each game, one at a time on a thread of their own, and kept in special://profile/titlenames.txt.
class CTitleNames : public CThread
{
public:
	// The game's name; empty while it isn't known, and then it's asked for
	static CStdString Get(DWORD dwTitleId);
	// For text that's a title ID (8 hex digits), its game's name once that's known; else the text
	static CStdString NameFor(const CStdString& strText);

private:
	virtual void Process();

	static void Load();
	static void LookUp(DWORD dwTitleId);
};

#endif //H_CTITLENAMES
