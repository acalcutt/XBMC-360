#ifndef H_CMEDIARIFFLEDIRECTORY
#define H_CMEDIARIFFLEDIRECTORY

#include "IDirectory.h"

class CVariant;

namespace XFILE
{

// A MediaRiffle server's video libraries, as the web app lists them (network\MediaRiffle.h):
//   mediariffle://                       Continue watching, and each Shows or Movies library
//   mediariffle://continue/              what was stopped part way, most recent first
//   mediariffle://shows/<library>/       its shows
//   mediariffle://show/<show>/           its seasons (or, with only one, its episodes)
//   mediariffle://show/<show>/<season>/  that season's episodes
//   mediariffle://movies/<library>/      its movies (one in parts is a folder of them: mediariffle://movie/<movie>/)
// What plays is mediariffle://play/<file> or mediariffle://movie/<movie>, a stream asked for when it starts.
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
	bool Get(const CStdString& path, CVariant& answer);
	void AddRoot(CFileItemList &items);
	void AddContinue(CFileItemList &items);
	bool AddShows(const CStdString& library, CFileItemList &items);
	bool AddShow(const CStdString& show, const CStdString& season, CFileItemList &items);
	bool AddMovies(const CStdString& library, CFileItemList &items);
	bool AddMovie(const CStdString& movie, CFileItemList &items);
	void AddFile(const CVariant& file, const CStdString& label, CFileItemList &items);
};

}

#endif //H_CMEDIARIFFLEDIRECTORY
