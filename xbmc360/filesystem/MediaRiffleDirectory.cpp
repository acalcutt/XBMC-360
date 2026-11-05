#include "MediaRiffleDirectory.h"
#include "FileItem.h"
#include "URL.h"
#include "guilib\LocalizeStrings.h"
#include "network\MediaRiffle.h"
#include "utils\Log.h"
#include "utils\StringUtils.h"
#include "utils\Variant.h"
#include "video\VideoInfoTag.h"

using namespace XFILE;

// The server's order first (most recent first, episode order, by title), else by name
static void AddSortMethods(CFileItemList &items)
{
	items.AddSortMethod(SORT_METHOD_NONE, 571, LABEL_MASKS("%L", "%D", "%L", ""));
	items.AddSortMethod(SORT_METHOD_LABEL, 551, LABEL_MASKS("%L", "%D", "%L", ""));
}

CMediaRiffleDirectory::CMediaRiffleDirectory(void)
{
}

CMediaRiffleDirectory::~CMediaRiffleDirectory(void)
{
}

bool CMediaRiffleDirectory::GetDirectory(const CStdString& strPath, CFileItemList &items)
{
	// Not linked yet: link now, when there's someone to ask
	if (!g_mediaRiffle.IsLinked() && (!m_allowPrompting || !g_mediaRiffle.Link()))
		return false;

	AddSortMethods(items);

	CURL url(strPath);
	CStdString kind = url.GetHostName();
	CStdStringArray parts;
	CStdString rest = url.GetFileName();
	rest.TrimRight("/");
	if (!rest.IsEmpty())
		CStringUtils::SplitString(rest, "/", parts);

	if (kind.IsEmpty())
	{
		AddRoot(items);
		return true;
	}
	if (kind.Equals("continue"))
	{
		AddContinue(items);
		return true;
	}
	if (parts.empty())
		return false;

	if (kind.Equals("shows"))
		return AddShows(parts[0], items);
	if (kind.Equals("show"))
		return AddShow(parts[0], parts.size() > 1 ? parts[1] : "", items);
	if (kind.Equals("movies"))
		return AddMovies(parts[0], items);
	if (kind.Equals("movie"))
		return AddMovie(parts[0], items);

	CLog::Log(LOGWARNING, "%s - unknown path %s", __FUNCTION__, strPath.c_str());
	return false;
}

bool CMediaRiffleDirectory::Get(const CStdString& path, CVariant& answer)
{
	long status = 0;
	if (!g_mediaRiffle.Call("GET", path, NULL, status, answer) || status != 200)
	{
		CLog::Log(LOGERROR, "%s - %s: %ld", __FUNCTION__, path.c_str(), status);
		return false;
	}
	return true;
}

void CMediaRiffleDirectory::AddRoot(CFileItemList &items)
{
	CFileItemPtr resume(new CFileItem(g_localizeStrings.Get(33520)));
	resume->SetPath("mediariffle://continue/");
	resume->m_bIsFolder = true;
	resume->SetLabelPreformated(true);
	items.Add(resume);

	// Its video libraries (music has its own place)
	CVariant libraries;
	if (!Get("/api/libraries", libraries))
		return;

	for (unsigned int i = 0; i < libraries.size(); i++)
	{
		const CVariant& library = libraries[i];
		CStdString kind = library["kind"].asString();
		if (!kind.Equals("Shows") && !kind.Equals("Movies"))
			continue;

		CStdString path;
		path.Format("mediariffle://%s/%d/", kind.ToLower().c_str(), (int)library["id"].asInteger());
		CFileItemPtr item(new CFileItem(CStdString(library["name"].asString())));
		item->SetPath(path);
		item->m_bIsFolder = true;
		item->SetLabelPreformated(true);
		items.Add(item);
	}
}

void CMediaRiffleDirectory::AddContinue(CFileItemList &items)
{
	CVariant recent;
	if (!Get("/api/continue", recent))
		return;

	// An episode as its show and its title
	for (unsigned int i = 0; i < recent.size(); i++)
	{
		const CVariant& file = recent[i]["file"];
		CStdString label = file["title"].asString();
		if (!recent[i]["show"].isNull())
			label = CStdString(recent[i]["show"].asString()) + " - " + label;

		AddFile(file, label, items);
	}
	items.SetContent("episodes");
}

bool CMediaRiffleDirectory::AddShows(const CStdString& library, CFileItemList &items)
{
	CVariant shows;
	if (!Get("/api/libraries/" + library + "/shows", shows))
		return false;

	for (unsigned int i = 0; i < shows.size(); i++)
	{
		const CVariant& show = shows[i];
		if (show["hidden"].asBoolean())
			continue;

		CStdString path, episodes;
		path.Format("mediariffle://show/%d/", (int)show["id"].asInteger());
		episodes.Format(g_localizeStrings.Get(33521).c_str(), (int)show["episodes"].asInteger());

		CFileItemPtr item(new CFileItem(CStdString(show["title"].asString())));
		item->SetPath(path);
		item->m_bIsFolder = true;
		item->SetLabel2(episodes);
		item->SetLabelPreformated(true);
		item->SetThumbnailImage("mediariffle://image" + CStdString(show["image"].asString()).Mid(11));
		items.Add(item);
	}
	items.SetContent("tvshows");
	return true;
}

// A show's seasons; or a season's episodes; or, when there's only the one season, its episodes straight away
bool CMediaRiffleDirectory::AddShow(const CStdString& show, const CStdString& season, CFileItemList &items)
{
	CVariant details;
	if (!Get("/api/shows/" + show, details))
		return false;

	const CVariant& seasons = details["seasons"];
	if (season.IsEmpty() && seasons.size() > 1)
	{
		for (unsigned int i = 0; i < seasons.size(); i++)
		{
			const CVariant& s = seasons[i];
			CStdString label = s["title"].asString();
			if (label.IsEmpty())
				label.Format(g_localizeStrings.Get(33522).c_str(), (int)s["number"].asInteger());

			CStdString series = s["series"].asString();
			if (!series.IsEmpty())
				label = series + " - " + label;

			CStdString path;
			path.Format("mediariffle://show/%s/%d/", show.c_str(), (int)s["id"].asInteger());
			CFileItemPtr item(new CFileItem(label));
			item->SetPath(path);
			item->m_bIsFolder = true;
			item->SetLabelPreformated(true);
			item->SetThumbnailImage("mediariffle://image" + CStdString(details["image"].asString()).Mid(11));
			items.Add(item);
		}
		items.SetContent("seasons");
		return true;
	}

	for (unsigned int i = 0; i < seasons.size(); i++)
	{
		const CVariant& s = seasons[i];
		if (!season.IsEmpty() && (int)s["id"].asInteger() != atoi(season.c_str()))
			continue;

		const CVariant& episodes = s["episodes"];
		for (unsigned int e = 0; e < episodes.size(); e++)
		{
			const CVariant& episode = episodes[e];
			if (episode["hidden"].asBoolean())
				continue;

			CStdString label = episode["title"].asString();
			if (!episode["episodeNumber"].isNull())
			{
				CStdString numbered;
				numbered.Format("%d. %s", (int)episode["episodeNumber"].asInteger(), label.c_str());
				label = numbered;
			}
			AddFile(episode, label, items);
		}
	}
	items.SetContent("episodes");
	return true;
}

bool CMediaRiffleDirectory::AddMovies(const CStdString& library, CFileItemList &items)
{
	CVariant movies;
	if (!Get("/api/libraries/" + library + "/movies", movies))
		return false;

	for (unsigned int i = 0; i < movies.size(); i++)
	{
		const CVariant& movie = movies[i];
		if (movie["hidden"].asBoolean())
			continue;

		CStdString label = movie["title"].asString();
		if (!movie["year"].isNull())
		{
			CStdString withYear;
			withYear.Format("%s (%d)", label.c_str(), (int)movie["year"].asInteger());
			label = withYear;
		}

		// In parts: a folder of them. Else the movie plays (its one part, found when it starts)
		bool inParts = movie["parts"].asInteger() > 1;
		CStdString path;
		path.Format(inParts ? "mediariffle://movie/%d/" : "mediariffle://movie/%d", (int)movie["id"].asInteger());

		CFileItemPtr item(new CFileItem(label));
		item->SetPath(path);
		item->m_bIsFolder = inParts;
		item->SetLabelPreformated(true);
		item->SetThumbnailImage("mediariffle://image" + CStdString(movie["image"].asString()).Mid(11));
		if (!inParts)
			item->SetMimeType("video/mp2t");
		items.Add(item);
	}
	items.SetContent("movies");
	return true;
}

bool CMediaRiffleDirectory::AddMovie(const CStdString& movie, CFileItemList &items)
{
	CVariant details;
	if (!Get("/api/movies/" + movie, details))
		return false;

	const CVariant& parts = details["parts"];
	for (unsigned int i = 0; i < parts.size(); i++)
	{
		CStdString label;
		label.Format(g_localizeStrings.Get(33523).c_str(), CStdString(details["title"].asString()).c_str(), i + 1);
		AddFile(parts[i], label, items);
	}
	items.SetContent("movies");
	return true;
}

// A file to play: its length beside it, the watched mark once it was seen to the end, and its frame as its picture
void CMediaRiffleDirectory::AddFile(const CVariant& file, const CStdString& label, CFileItemList &items)
{
	if (!file["playable"].asBoolean(true))
		return;

	CStdString path;
	path.Format("mediariffle://play/%d", (int)file["id"].asInteger());

	CFileItemPtr item(new CFileItem(label));
	item->SetPath(path);
	item->m_bIsFolder = false;
	item->SetMimeType("video/mp2t");
	item->SetLabelPreformated(true);
	item->SetLabel2(CStringUtils::SecondsToTimeString((long)file["duration"].asDouble()));
	item->SetThumbnailImage("mediariffle://image" + CStdString(file["image"].asString()).Mid(11));

	CVideoInfoTag* tag = item->GetVideoInfoTag();
	tag->m_strTitle = label;
	if (file["finished"].asBoolean())
	{
		tag->m_playCount = 1;
		item->SetOverlayImage(CGUIListItem::ICON_OVERLAY_WATCHED);
	}
	items.Add(item);
}
