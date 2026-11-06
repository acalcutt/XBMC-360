#ifndef H_CLOCALGAMESDIRECTORY
#define H_CLOCALGAMESDIRECTORY

#include "IDirectory.h"

namespace XFILE
{

// localgames://: On this console, the games copied to the console from shares and discs that start as they are, without
// their share or disc, by their names: Xbox 360 games and the emulators' games. It's listed afresh each time, so it
// shows what's there now.
class CLocalGamesDirectory : public IDirectory
{
public:
	CLocalGamesDirectory(void);
	virtual ~CLocalGamesDirectory(void);

	virtual bool GetDirectory(const CStdString& strPath, CFileItemList &items);
	virtual DIR_CACHE_TYPE GetCacheType(const CStdString& strPath) const { return DIR_CACHE_NEVER; }

	static const char* PATH;
	static bool IsLocalGames(const CStdString& strPath);
	// How many games it lists, for Home's tile
	static int Count();
};

}

#endif //H_CLOCALGAMESDIRECTORY
