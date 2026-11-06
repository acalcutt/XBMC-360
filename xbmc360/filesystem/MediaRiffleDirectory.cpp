#include "MediaRiffleDirectory.h"
#include "FileItem.h"
#include "MediaSource.h"
#include "Settings.h"
#include "URL.h"
#include "guilib\LocalizeStrings.h"
#include "guilib\dialogs\GUIDialogNetworkSetup.h"
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
	m_server = CMediaRiffle::ServerOf(strPath);
	m_source = CMediaRiffle::SourceOf(strPath);
	if (m_server.IsEmpty())
		return GetSources(items);

	// Not linked yet: link now, when there's someone to ask
	if (!g_mediaRiffle.IsLinked(m_server) && (!m_allowPrompting || !g_mediaRiffle.Link(m_server)))
		return false;

	AddSortMethods(items);

	CStdStringArray parts;
	CStdString rest = CURL(strPath).GetFileName();
	rest.TrimRight("/");
	if (!rest.IsEmpty())
		CStringUtils::SplitString(rest, "/", parts);

	// The source, or a folder above the listings (shows/, from a library's path): the libraries
	CStdString kind = parts.empty() ? "" : parts[0];
	if (kind.Equals("continue"))
	{
		AddContinue(items);
		return true;
	}
	if (kind.Equals("categories") || kind.Equals("lists"))
		return AddTags(kind, items);
	if (parts.size() < 2)
	{
		AddRoot(items);
		return true;
	}

	if (kind.Equals("category") || kind.Equals("list"))
		return AddTagged(kind, parts[1], items);
	if (kind.Equals("shows"))
		return AddShows(parts[1], items);
	if (kind.Equals("show"))
		return AddShow(parts[1], parts.size() > 2 ? parts[2] : "", items);
	if (kind.Equals("movies"))
		return AddMovies(parts[1], items);
	if (kind.Equals("movie"))
		return AddMovie(parts[1], items);

	CLog::Log(LOGWARNING, "%s - unknown path %s", __FUNCTION__, strPath.c_str());
	return false;
}

// mediariffle://: the MediaRiffle sources in Videos. With one, it's listed straight away; with none, a server is asked
// for and added.
bool CMediaRiffleDirectory::GetSources(CFileItemList &items)
{
	VECSOURCES* sources = g_settings.GetSourcesFromType("video");
	VECSOURCES riffle;
	for (unsigned int i = 0; sources && i < sources->size(); i++)
	{
		if (!CMediaRiffle::ServerOf((*sources)[i].strPath).IsEmpty())
			riffle.push_back((*sources)[i]);
	}

	if (riffle.empty())
	{
		CStdString path = "mediariffle://";
		if (!m_allowPrompting || !CGUIDialogNetworkSetup::ShowAndGetNetworkAddress(path) || CMediaRiffle::SourceOf(path).IsEmpty())
			return false;

		CMediaSource source;
		source.strName = "MediaRiffle";
		source.strPath = CMediaRiffle::SourceOf(path);
		source.vecPaths.push_back(source.strPath);
		source.m_iDriveType = CMediaSource::SOURCE_TYPE_REMOTE;
		g_settings.AddShare("video", source);
		riffle.push_back(source);
	}

	if (riffle.size() == 1)
		return GetDirectory(riffle[0].strPath, items);

	for (unsigned int i = 0; i < riffle.size(); i++)
	{
		CFileItemPtr item(new CFileItem(riffle[i].strName));
		item->SetPath(riffle[i].strPath);
		item->m_bIsFolder = true;
		item->SetLabelPreformated(true);
		items.Add(item);
	}
	return true;
}

bool CMediaRiffleDirectory::Get(const CStdString& path, CVariant& answer)
{
	long status = 0;
	if (!g_mediaRiffle.Call(m_server, "GET", path, NULL, status, answer) || status != 200)
	{
		CLog::Log(LOGERROR, "%s - %s%s: %ld", __FUNCTION__, m_server.c_str(), path.c_str(), status);
		return false;
	}
	return true;
}

// A picture the server names /api/images/<kind>/<id>?v=…, as <source>image/<kind>/<id>?v=… (MediaRiffleFile)
CStdString CMediaRiffleDirectory::Image(const CVariant& image) const
{
	CStdString path = image.asString();
	if (path.Left(12) != "/api/images/")
		return "";
	return m_source + "image/" + path.Mid(12);
}

// A folder on this server: its path from the source
void CMediaRiffleDirectory::AddFolder(const CStdString& label, const CStdString& path, CFileItemList &items, const CStdString& label2)
{
	CFileItemPtr item(new CFileItem(label));
	item->SetPath(m_source + path);
	item->m_bIsFolder = true;
	item->SetLabel2(label2);
	item->SetLabelPreformated(true);
	items.Add(item);
}

// Play all and Shuffle, at the top: what a library, a show, a category (tag) or a list has, played in a row. Actions,
// not videos, so playing the folder leaves them out.
void CMediaRiffleDirectory::AddQueueItems(const CStdString& what, const CStdString& value, CFileItemList &items)
{
	const char* modes[] = { "queue", "shuffle" };
	const int labels[] = { 33529, 191 }; // Play all, Shuffle
	for (int i = 0; i < 2; i++)
	{
		CFileItemPtr item(new CFileItem(g_localizeStrings.Get(labels[i])));
		item->SetPath(m_source + modes[i] + "/" + what + "/" + value);
		item->m_bIsFolder = false;
		item->SetMimeType("video/mp2t");
		item->SetLabelPreformated(true);
		item->SetSpecialSort(SORT_ON_TOP);
		item->SetCanQueue(false);
		items.Add(item);
	}
}

void CMediaRiffleDirectory::AddRoot(CFileItemList &items)
{
	AddFolder(g_localizeStrings.Get(33520), "continue/", items);
	AddFolder(g_localizeStrings.Get(33524), "categories/", items);
	AddFolder(g_localizeStrings.Get(33525), "lists/", items);

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
		path.Format("%s%s/%d/", m_source.c_str(), kind.ToLower().c_str(), (int)library["id"].asInteger());
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

// The categories (/api/tags: { tag, files }) or the user's lists (/api/lists: { tag, items }), a folder each
bool CMediaRiffleDirectory::AddTags(const CStdString& kind, CFileItemList &items)
{
	bool categories = kind.Equals("categories");
	CVariant tags;
	if (!Get(categories ? "/api/tags" : "/api/lists", tags))
		return false;

	for (unsigned int i = 0; i < tags.size(); i++)
	{
		CStdString tag = tags[i]["tag"].asString();
		CStdString encoded = tag;
		CURL::Encode(encoded);

		CStdString count;
		count.Format(g_localizeStrings.Get(categories ? 33527 : 33528).c_str(),
			(int)tags[i][categories ? "files" : "items"].asInteger());
		AddFolder(tag, CStdString(categories ? "category/" : "list/") + encoded + "/", items, count);
	}
	return true;
}

// What's in a category or on a list: its shows and movies (music has its own place)
bool CMediaRiffleDirectory::AddTagged(const CStdString& kind, const CStdString& tag, CFileItemList &items)
{
	CVariant tagged;
	bool category = kind.Equals("category");
	if (!Get(CStdString(category ? "/api/tags/" : "/api/lists/") + tag, tagged))
		return false;

	AddQueueItems(category ? "tag" : "list", tag, items);
	AddShowTiles(tagged["shows"], items);
	AddMovieTiles(tagged["movies"], items);
	return true;
}

bool CMediaRiffleDirectory::AddShows(const CStdString& library, CFileItemList &items)
{
	CVariant shows;
	if (!Get("/api/libraries/" + library + "/shows", shows))
		return false;

	AddQueueItems("library", library, items);
	AddShowTiles(shows, items);
	items.SetContent("tvshows");
	return true;
}

// Shows as the server lists them (ShowTiles): a folder each, with how many episodes, leaving out those the user hid
void CMediaRiffleDirectory::AddShowTiles(const CVariant& shows, CFileItemList &items)
{
	for (unsigned int i = 0; i < shows.size(); i++)
	{
		const CVariant& show = shows[i];
		if (show["hidden"].asBoolean())
			continue;

		CStdString path, episodes;
		path.Format("%sshow/%d/", m_source.c_str(), (int)show["id"].asInteger());
		episodes.Format(g_localizeStrings.Get(33521).c_str(), (int)show["episodes"].asInteger());

		CFileItemPtr item(new CFileItem(CStdString(show["title"].asString())));
		item->SetPath(path);
		item->m_bIsFolder = true;
		item->SetLabel2(episodes);
		item->SetLabelPreformated(true);
		item->SetThumbnailImage(Image(show["image"]));
		items.Add(item);
	}
}

// A show's seasons; or a season's episodes; or, when there's only the one season, its episodes straight away
bool CMediaRiffleDirectory::AddShow(const CStdString& show, const CStdString& season, CFileItemList &items)
{
	CVariant details;
	if (!Get("/api/shows/" + show, details))
		return false;

	// The whole show, in season order or shuffled
	if (season.IsEmpty())
		AddQueueItems("show", show, items);

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
			path.Format("%sshow/%s/%d/", m_source.c_str(), show.c_str(), (int)s["id"].asInteger());
			CFileItemPtr item(new CFileItem(label));
			item->SetPath(path);
			item->m_bIsFolder = true;
			item->SetLabelPreformated(true);
			item->SetThumbnailImage(Image(details["image"]));
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

	AddQueueItems("library", library, items);
	AddMovieTiles(movies, items);
	items.SetContent("movies");
	return true;
}

// Movies as the server lists them (MovieTiles), with their year, leaving out those the user hid: one in parts is a
// folder of them, else the movie plays (its one part, found when it starts)
void CMediaRiffleDirectory::AddMovieTiles(const CVariant& movies, CFileItemList &items)
{
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

		bool inParts = movie["parts"].asInteger() > 1;
		CStdString path;
		path.Format(inParts ? "%smovie/%d/" : "%smovie/%d", m_source.c_str(), (int)movie["id"].asInteger());

		CFileItemPtr item(new CFileItem(label));
		item->SetPath(path);
		item->m_bIsFolder = inParts;
		item->SetLabelPreformated(true);
		item->SetThumbnailImage(Image(movie["image"]));
		if (!inParts)
			item->SetMimeType("video/mp2t");
		items.Add(item);
	}
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
	path.Format("%splay/%d", m_source.c_str(), (int)file["id"].asInteger());

	CFileItemPtr item(new CFileItem(label));
	item->SetPath(path);
	item->m_bIsFolder = false;
	item->SetMimeType("video/mp2t");
	item->SetLabelPreformated(true);
	item->SetLabel2(CStringUtils::SecondsToTimeString((long)file["duration"].asDouble()));
	item->SetThumbnailImage(Image(file["image"]));

	CVideoInfoTag* tag = item->GetVideoInfoTag();
	tag->m_strTitle = label;
	if (file["finished"].asBoolean())
	{
		tag->m_playCount = 1;
		item->SetOverlayImage(CGUIListItem::ICON_OVERLAY_WATCHED);
	}
	items.Add(item);
}
