#ifndef H_CMEDIARIFFLEDIRECTORY
#define H_CMEDIARIFFLEDIRECTORY

#include "IDirectory.h"

class CVariant;

namespace XFILE
{

// A MediaRiffle server's video libraries, as the web app lists them (network\MediaRiffle.h). Under its source,
// mediariffle://host:port/ (mediariffles:// over HTTPS):
//   <source>                       Continue watching, and each Shows or Movies library
//   <source>continue/              what was stopped part way, most recent first
//   <source>shows/<library>/       its shows
//   <source>show/<show>/           its seasons (or, with only one, its episodes)
//   <source>show/<show>/<season>/  that season's episodes
//   <source>movies/<library>/      its movies (one in parts is a folder of them: <source>movie/<movie>/)
// What plays is <source>play/<file> or <source>movie/<movie>, a stream asked for when it starts.
//
// mediariffle:// itself is the MediaRiffle sources in Videos: the one there is, straight away; with none, it asks for a
// server (as Add network location does) and adds it.
class CMediaRiffleDirectory : public IDirectory
{
public:
	CMediaRiffleDirectory(void);
	virtual ~CMediaRiffleDirectory(void);

	virtual bool GetDirectory(const CStdString& strPath, CFileItemList &items);
	// What it lists has no file extensions to filter by: it's all video
	virtual bool IsAllowed(const CStdString& strFile) const { return true; }
	virtual DIR_CACHE_TYPE GetCacheType(const CStdString& strPath) const { return DIR_CACHE_NEVER; }

private:
	bool GetSources(CFileItemList &items);
	bool Get(const CStdString& path, CVariant& answer);
	void AddRoot(CFileItemList &items);
	void AddContinue(CFileItemList &items);
	bool AddShows(const CStdString& library, CFileItemList &items);
	bool AddShow(const CStdString& show, const CStdString& season, CFileItemList &items);
	bool AddMovies(const CStdString& library, CFileItemList &items);
	bool AddMovie(const CStdString& movie, CFileItemList &items);
	void AddFile(const CVariant& file, const CStdString& label, CFileItemList &items);
	CStdString Image(const CVariant& image) const;

	// The server listed, and its source, which the paths in it start with
	CStdString m_server;
	CStdString m_source;
};

}

#endif //H_CMEDIARIFFLEDIRECTORY
