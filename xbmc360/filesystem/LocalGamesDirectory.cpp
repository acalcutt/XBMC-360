#include "LocalGamesDirectory.h"
#include "FileItem.h"
#include "guilib\LocalizeStrings.h"
#include "xbox\Emulators.h"
#include "xbox\GameCopy.h"

using namespace XFILE;

const char* CLocalGamesDirectory::PATH = "localgames://";

CLocalGamesDirectory::CLocalGamesDirectory(void)
{
}

CLocalGamesDirectory::~CLocalGamesDirectory(void)
{
}

bool CLocalGamesDirectory::IsLocalGames(const CStdString& strPath)
{
	return strPath.Left(13).Equals(PATH);
}

bool CLocalGamesDirectory::GetDirectory(const CStdString& strPath, CFileItemList &items)
{
	CGameCopy::GetPlayable(items);
	CEmulators::GetCopied(items);
	items.SetLabel(g_localizeStrings.Get(13242));
	return true;
}

int CLocalGamesDirectory::Count()
{
	CFileItemList items;
	CGameCopy::GetPlayable(items);
	CEmulators::GetCopied(items);
	return items.Size();
}
