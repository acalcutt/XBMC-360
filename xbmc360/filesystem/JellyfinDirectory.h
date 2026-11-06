#ifndef H_CJELLYFINDIRECTORY
#define H_CJELLYFINDIRECTORY

#include "IDirectory.h"

class CVariant;

namespace XFILE
{

// A Jellyfin server's video libraries (network\Jellyfin.h). Under its source, jellyfin://host:port/ (jellyfins:// over
// HTTPS):
//   <source>                         Continue watching, Next up, and each video library
//   <source>resume/                  what was stopped part way, most recent first
//   <source>nextup/                  the next episode of each show being watched
//   <source>movies/<library>/        a movies library's movies
//   <source>shows/<library>/         a shows library's shows
//   <source>series/<show>/           its seasons (or, with only one, its episodes)
//   <source>series/<show>/<season>/  that season's episodes
//   <source>folder/<folder>/         what's in any other library or folder (home videos, collections)
// What plays is <source>play/<item>. A library, a show, a season and a folder start with Play all and Shuffle
// (<source>queue/<id> and <source>shuffle/<id>, Jellyfin::PlayQueue).
class CJellyfinDirectory : public IDirectory
{
public:
	CJellyfinDirectory(void);
	virtual ~CJellyfinDirectory(void);

	virtual bool GetDirectory(const CStdString& strPath, CFileItemList &items);
	// What it lists has no file extensions to filter by: it's all video
	virtual bool IsAllowed(const CStdString& strFile) const { return true; }
	virtual DIR_CACHE_TYPE GetCacheType(const CStdString& strPath) const { return DIR_CACHE_NEVER; }

private:
	bool Get(const CStdString& path, const CStdString& older, CVariant& answer);
	bool AddRoot(CFileItemList &items);
	bool AddItems(const CStdString& path, const CStdString& older, CFileItemList &items, bool withShow = false);
	bool AddUserItems(const CStdString& query, CFileItemList &items);
	bool AddSeries(const CStdString& series, const CStdString& season, CFileItemList &items);
	void AddItem(const CVariant& item, CFileItemList &items, bool withShow);
	void AddFolder(const CStdString& label, const CStdString& path, CFileItemList &items);
	void AddQueueItems(const CStdString& parent, CFileItemList &items);
	CStdString Image(const CVariant& item) const;

	// The server listed, its source (which the paths in it start with), and the user signed in there
	CStdString m_server;
	CStdString m_source;
	CStdString m_userId;
};

}

#endif //H_CJELLYFINDIRECTORY
