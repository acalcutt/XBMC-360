#include "JellyfinDirectory.h"
#include "FileItem.h"
#include "URL.h"
#include "guilib\LocalizeStrings.h"
#include "network\Jellyfin.h"
#include "utils\Log.h"
#include "utils\StringUtils.h"
#include "utils\Variant.h"
#include "video\VideoInfoTag.h"

using namespace XFILE;

// The server's order first (by name, in episode order, most recent first), else by name
static void AddSortMethods(CFileItemList &items)
{
	items.AddSortMethod(SORT_METHOD_NONE, 571, LABEL_MASKS("%L", "%D", "%L", ""));
	items.AddSortMethod(SORT_METHOD_LABEL, 551, LABEL_MASKS("%L", "%D", "%L", ""));
}

CJellyfinDirectory::CJellyfinDirectory(void)
{
}

CJellyfinDirectory::~CJellyfinDirectory(void)
{
}

bool CJellyfinDirectory::GetDirectory(const CStdString& strPath, CFileItemList &items)
{
	m_server = CJellyfin::ServerOf(strPath);
	m_source = CJellyfin::SourceOf(strPath);
	if (m_server.IsEmpty())
		return false;

	// Not signed in yet: now, when there's someone to ask
	if (!g_jellyfin.IsSignedIn(m_server) && (!m_allowPrompting || !g_jellyfin.SignIn(m_server)))
		return false;
	m_userId = g_jellyfin.GetUserId(m_server);

	AddSortMethods(items);

	CStdStringArray parts;
	CStdString rest = CURL(strPath).GetFileName();
	rest.TrimRight("/");
	if (!rest.IsEmpty())
		CStringUtils::SplitString(rest, "/", parts);

	CStdString kind = parts.empty() ? "" : parts[0];
	if (kind.Equals("resume"))
		return AddItems("/UserItems/Resume?userId=" + m_userId + "&mediaTypes=Video",
			"/Users/" + m_userId + "/Items/Resume?mediaTypes=Video", items, true);
	if (kind.Equals("nextup"))
		return AddItems("/Shows/NextUp?userId=" + m_userId + "&limit=50", "", items, true);
	if (parts.size() < 2)
		return AddRoot(items);

	CStdString id = parts[1];
	if (kind.Equals("movies"))
	{
		items.SetContent("movies");
		return AddUserItems("parentId=" + id + "&recursive=true&includeItemTypes=Movie&sortBy=SortName", items);
	}
	if (kind.Equals("shows"))
	{
		items.SetContent("tvshows");
		return AddUserItems("parentId=" + id + "&recursive=true&includeItemTypes=Series&sortBy=SortName", items);
	}
	if (kind.Equals("series"))
		return AddSeries(id, parts.size() > 2 ? parts[2] : "", items);
	if (kind.Equals("folder"))
		return AddUserItems("parentId=" + id + "&sortBy=SortName", items);

	CLog::Log(LOGWARNING, "%s - unknown path %s", __FUNCTION__, strPath.c_str());
	return false;
}

// A GET of path, or where a server older than Jellyfin 10.9 (and Emby) has it when that's not found
bool CJellyfinDirectory::Get(const CStdString& path, const CStdString& older, CVariant& answer)
{
	long status = 0;
	if (g_jellyfin.Call(m_server, "GET", path, NULL, status, answer) && status == 404 && !older.IsEmpty())
		g_jellyfin.Call(m_server, "GET", older, NULL, status, answer);

	if (status != 200)
	{
		CLog::Log(LOGERROR, "%s - %s%s: %ld", __FUNCTION__, m_server.c_str(), path.c_str(), status);
		return false;
	}
	return true;
}

void CJellyfinDirectory::AddFolder(const CStdString& label, const CStdString& path, CFileItemList &items)
{
	CFileItemPtr item(new CFileItem(label));
	item->SetPath(m_source + path);
	item->m_bIsFolder = true;
	item->SetLabelPreformated(true);
	items.Add(item);
}

// Continue watching, Next up, then the user's video libraries (music, pictures, books and live TV have other places)
bool CJellyfinDirectory::AddRoot(CFileItemList &items)
{
	AddFolder(g_localizeStrings.Get(33520), "resume/", items);
	AddFolder(g_localizeStrings.Get(33552), "nextup/", items);

	CVariant views;
	if (!Get("/UserViews?userId=" + m_userId, "/Users/" + m_userId + "/Views", views))
		return false;

	const CVariant& list = views["Items"];
	for (unsigned int i = 0; i < list.size(); i++)
	{
		const CVariant& view = list[i];
		CStdString type = view["CollectionType"].asString();
		CStdString kind;
		if (type.Equals("movies"))
			kind = "movies";
		else if (type.Equals("tvshows"))
			kind = "shows";
		else if (type.IsEmpty() || type.Equals("homevideos") || type.Equals("boxsets") || type.Equals("folders") ||
			type.Equals("musicvideos") || type.Equals("mixed"))
			kind = "folder";
		else
			continue;

		CFileItemPtr item(new CFileItem(CStdString(view["Name"].asString())));
		item->SetPath(m_source + kind + "/" + CStdString(view["Id"].asString()) + "/");
		item->m_bIsFolder = true;
		item->SetLabelPreformated(true);
		item->SetThumbnailImage(Image(view));
		items.Add(item);
	}
	return true;
}

// Most answers are { Items: [ … ] }
bool CJellyfinDirectory::AddItems(const CStdString& path, const CStdString& older, CFileItemList &items, bool withShow)
{
	CVariant answer;
	if (!Get(path, older, answer))
		return false;

	const CVariant& list = answer.isMember("Items") ? answer["Items"] : answer;
	for (unsigned int i = 0; i < list.size(); i++)
		AddItem(list[i], items, withShow);
	return true;
}

// The user's items matching a query: /Items?userId=…, or /Users/<user>/Items on an older server
bool CJellyfinDirectory::AddUserItems(const CStdString& query, CFileItemList &items)
{
	return AddItems("/Items?userId=" + m_userId + "&" + query, "/Users/" + m_userId + "/Items?" + query, items);
}

// A show's seasons; or a season's episodes; or, when there's only the one season (or none), its episodes straight away
bool CJellyfinDirectory::AddSeries(const CStdString& series, const CStdString& season, CFileItemList &items)
{
	CStdString episodes = "/Shows/" + series + "/Episodes?userId=" + m_userId;

	if (season.IsEmpty())
	{
		CVariant seasons;
		if (!Get("/Shows/" + series + "/Seasons?userId=" + m_userId, "", seasons))
			return false;

		const CVariant& list = seasons["Items"];
		if (list.size() > 1)
		{
			for (unsigned int i = 0; i < list.size(); i++)
				AddItem(list[i], items, false);
			items.SetContent("seasons");
			return true;
		}
		if (list.size() == 1)
			episodes += "&seasonId=" + CStdString(list[0u]["Id"].asString());
	}
	else
		episodes += "&seasonId=" + season;

	items.SetContent("episodes");
	return AddItems(episodes, "", items);
}

// An item as the server lists it (BaseItemDto): a folder of its kind, or a video to play. withShow puts an
// episode's show before it, where episodes of different shows are listed together.
void CJellyfinDirectory::AddItem(const CVariant& item, CFileItemList &items, bool withShow)
{
	CStdString type = item["Type"].asString();
	CStdString id = item["Id"].asString();
	CStdString name = item["Name"].asString();
	CStdString path;
	bool folder = true;

	if (type.Equals("Series"))
		path = m_source + "series/" + id + "/";
	else if (type.Equals("Season"))
		path = m_source + "series/" + CStdString(item["SeriesId"].asString()) + "/" + id + "/";
	else if (item["IsFolder"].asBoolean())
		path = m_source + "folder/" + id + "/";
	else if (CStdString(item["MediaType"].asString()).Equals("Video"))
	{
		path = m_source + "play/" + id;
		folder = false;
	}
	else
		return;   // music, pictures, books: not for Videos

	CStdString label = name;
	if (type.Equals("Movie") && !item["ProductionYear"].isNull())
		label.Format("%s (%d)", name.c_str(), (int)item["ProductionYear"].asInteger());
	else if (type.Equals("Episode"))
	{
		if (!item["IndexNumber"].isNull())
			label.Format("%d. %s", (int)item["IndexNumber"].asInteger(), name.c_str());
		if (withShow && !item["SeriesName"].isNull())
			label = CStdString(item["SeriesName"].asString()) + " - " + label;
	}

	CFileItemPtr entry(new CFileItem(label));
	entry->SetPath(path);
	entry->m_bIsFolder = folder;
	entry->SetLabelPreformated(true);
	entry->SetThumbnailImage(Image(item));

	if (!folder)
	{
		entry->SetMimeType("video/mp2t");
		long seconds = (long)(item["RunTimeTicks"].asInteger() / 10000000);
		if (seconds > 0)
			entry->SetLabel2(CStringUtils::SecondsToTimeString(seconds));

		CVideoInfoTag* tag = entry->GetVideoInfoTag();
		tag->m_strTitle = label;
		if (item["UserData"]["Played"].asBoolean())
		{
			tag->m_playCount = 1;
			entry->SetOverlayImage(CGUIListItem::ICON_OVERLAY_WATCHED);
		}
	}
	items.Add(entry);
}

// The item's picture, which Jellyfin serves to anyone: its own or, for an episode or season without one, its show's
CStdString CJellyfinDirectory::Image(const CVariant& item) const
{
	CStdString id = item["Id"].asString();
	CStdString tag = item["ImageTags"]["Primary"].asString();
	if (tag.IsEmpty() && !item["SeriesPrimaryImageTag"].isNull())
	{
		id = item["SeriesId"].asString();
		tag = item["SeriesPrimaryImageTag"].asString();
	}
	if (tag.IsEmpty() || id.IsEmpty())
		return "";

	return m_server + "/Items/" + id + "/Images/Primary?fillHeight=360&quality=85&tag=" + tag;
}
